// Metal backend for moppe/render.  One command buffer per frame:
//
//   scene pass   MSAA -> resolve into sceneA (reversed-Z depth)
//   post passes  underwater grade / motion-blur ghosts on sceneA/B
//   present pass fullscreen quad of the final scene + HUD overlay
//
// Terrain is vertex-pulled from height/normal textures; the shadow
// map is a one-time Depth16Unorm ortho render.  All texture uploads
// go through staging buffers + blit (simulator requires private
// storage; it's the right call on TBDR devices anyway).

#import <Metal/Metal.h>
#import <MetalKit/MetalKit.h>
#import <QuartzCore/QuartzCore.h>
#import <TargetConditionals.h>
#if TARGET_OS_IPHONE
#import <UIKit/UIKit.h>
#else
#import <AppKit/AppKit.h>
#import <ImageIO/ImageIO.h>
#endif

#include <moppe/profile.hh>
#include <moppe/render/metal/metal_renderer.hh>
#include <moppe/render/metal/shader_types.h>

#if defined(TRACY_ENABLE) && !TARGET_OS_IPHONE
#include <tracy/TracyMetal.hmm>
#define MOPPE_METAL_ZONE(context, descriptor, name)                            \
  TracyMetalZone (context, descriptor, name)
#define MOPPE_METAL_ZONE_DISABLED(context, descriptor, name) ((void)0)
#else
#define MOPPE_METAL_ZONE(context, descriptor, name) ((void)0)
#define MOPPE_METAL_ZONE_DISABLED(context, descriptor, name) ((void)0)
#endif

#include <algorithm>
#include <array>
#include <atomic>
#include <bit>
#include <chrono>
#include <cmath>
#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <fstream>
#include <iostream>
#include <limits>
#include <memory>
#include <mutex>
#include <sstream>
#include <vector>

namespace moppe {
  namespace render {
    namespace {
      const int FRAMES_IN_FLIGHT = 3;
#if TARGET_OS_TV
      // At television viewing distance 2x MSAA preserves stable terrain edges
      // while halving the dominant scene-pass color and depth sample traffic.
      const int MSAA_SAMPLES = 2;
#else
      const int MSAA_SAMPLES = 4;
#endif
      const int CHUNK_CELLS = 128;
      const int TERRAIN_LOD_COUNT = (int)TerrainLod::Count;
      const int TERRAIN_NATIVE_LOD = (int)TerrainLod::Native;
      const float TERRAIN_LOD_STEP[TERRAIN_LOD_COUNT] = {
        0.25f, 1.0f, 2.0f, 4.0f, 8.0f
      };
      const int TERRAIN_LOD_VERTS[TERRAIN_LOD_COUNT] = { CHUNK_CELLS * 4 + 1,
                                                         CHUNK_CELLS + 1,
                                                         CHUNK_CELLS / 2 + 1,
                                                         CHUNK_CELLS / 4 + 1,
                                                         CHUNK_CELLS / 8 + 1 };
      const int PROBE_W = 32; // auto-exposure luminance probe
      const int PROBE_H = 16;
      const int MAX_TIMESTAMP_SAMPLES = 64;

      double cpu_time () {
        using Clock = std::chrono::steady_clock;
        return std::chrono::duration<double> (Clock::now ().time_since_epoch ())
          .count ();
      }

      float scene_render_scale (float backing_scale,
                                float requested_scale,
                                float scale_override) {
#if TARGET_OS_TV
        // When a 4K television uses a 2x UIKit backing scale, keep the
        // expensive 3D scene relative to point resolution and let the
        // inexpensive present/HUD pass target the native drawable.
        float scale = requested_scale / std::max (1.0f, backing_scale);
#elif TARGET_OS_IPHONE
        (void)backing_scale;
        float scale = requested_scale;
#else
        float scale = requested_scale / std::max (1.0f, backing_scale);
#endif
        if (scale_override > 0.0f)
          scale = scale_override;
        return std::clamp (scale, 0.25f, 1.0f);
      }

      enum class GpuPass {
        Terrain,
        Sky,
        Water,
        Scene,
        Post,
        Bloom,
        Exposure,
        Present,
        Count
      };
      constexpr int GPU_PASS_COUNT = static_cast<int> (GpuPass::Count);
      const char* GPU_PASS_NAMES[GPU_PASS_COUNT] = { "terrain",  "sky",
                                                     "water",    "scene",
                                                     "post",     "bloom",
                                                     "exposure", "present" };

      struct FrameTiming {
        std::mutex mutex;
        double interval_start = 0;
        double gpu_total_ms = 0;
        double gpu_min_ms = std::numeric_limits<double>::max ();
        double gpu_max_ms = 0;
        std::array<double, GPU_PASS_COUNT> pass_total_ms {};
        int frames = 0;
      };

      struct BenchmarkSample {
        uint32_t mask;
        uint32_t partition_mask;
        uint32_t epoch;
        uint32_t frame;
        double gpu_ms;
      };

      struct BenchmarkOutput {
        std::mutex mutex;
        std::vector<BenchmarkSample> samples;
        std::atomic<int> completed { 0 };
        int expected = 0;
        std::string path;
        std::vector<std::string> feature_names;
      };

#if !TARGET_OS_IPHONE
      float half_to_float (std::uint16_t half) {
        const std::uint32_t sign = (half & 0x8000u) << 16;
        std::uint32_t exponent = (half >> 10) & 0x1fu;
        std::uint32_t mantissa = half & 0x03ffu;
        std::uint32_t bits;
        if (exponent == 0) {
          if (mantissa == 0) {
            bits = sign;
          } else {
            exponent = 113;
            while ((mantissa & 0x0400u) == 0) {
              mantissa <<= 1;
              --exponent;
            }
            mantissa &= 0x03ffu;
            bits = sign | (exponent << 23) | (mantissa << 13);
          }
        } else if (exponent == 31) {
          bits = sign | 0x7f800000u | (mantissa << 13);
        } else {
          bits = sign | ((exponent + 112) << 23) | (mantissa << 13);
        }
        return std::bit_cast<float> (bits);
      }

      unsigned char linear_byte (float value) {
        value = std::clamp (value, 0.0f, 1.0f);
        const float srgb = value <= 0.0031308f
                             ? value * 12.92f
                             : 1.055f * std::pow (value, 1.0f / 2.4f) - 0.055f;
        return static_cast<unsigned char> (srgb * 255.0f + 0.5f);
      }

      bool write_capture_png (const std::string& path,
                              int width,
                              int height,
                              std::size_t source_row_bytes,
                              const void* source) {
        std::vector<unsigned char> pixels (static_cast<std::size_t> (width) *
                                           height * 4);
        const auto* bytes = static_cast<const unsigned char*> (source);
        for (int y = 0; y < height; ++y) {
          const auto* row = reinterpret_cast<const std::uint16_t*> (
            bytes + static_cast<std::size_t> (y) * source_row_bytes);
          for (int x = 0; x < width; ++x) {
            unsigned char* pixel =
              pixels.data () + (static_cast<std::size_t> (y) * width + x) * 4;
            pixel[0] = linear_byte (half_to_float (row[x * 4]));
            pixel[1] = linear_byte (half_to_float (row[x * 4 + 1]));
            pixel[2] = linear_byte (half_to_float (row[x * 4 + 2]));
            pixel[3] = 255;
          }
        }

        CGDataProviderRef provider = CGDataProviderCreateWithData (
          nullptr, pixels.data (), pixels.size (), nullptr);
        CGColorSpaceRef color_space =
          CGColorSpaceCreateWithName (kCGColorSpaceSRGB);
        CGImageRef image = CGImageCreate (
          width,
          height,
          8,
          32,
          static_cast<std::size_t> (width) * 4,
          color_space,
          static_cast<CGBitmapInfo> (
            static_cast<std::uint32_t> (kCGImageAlphaLast) |
            static_cast<std::uint32_t> (kCGBitmapByteOrderDefault)),
          provider,
          nullptr,
          false,
          kCGRenderingIntentDefault);
        NSString* filename = [NSString stringWithUTF8String:path.c_str ()];
        NSURL* url = [NSURL fileURLWithPath:filename];
        CGImageDestinationRef destination = CGImageDestinationCreateWithURL (
          (__bridge CFURLRef)url, CFSTR ("public.png"), 1, nullptr);
        bool written = false;
        if (destination && image) {
          CGImageDestinationAddImage (destination, image, nullptr);
          written = CGImageDestinationFinalize (destination);
        }
        if (destination)
          CFRelease (destination);
        if (image)
          CGImageRelease (image);
        CGColorSpaceRelease (color_space);
        CGDataProviderRelease (provider);
        return written;
      }
#endif

      MoppeFloat4 f4 (const Vec3& v, float w = 0.0f) {
        MoppeFloat4 r;
        r.x = v[0];
        r.y = v[1];
        r.z = v[2];
        r.w = w;
        return r;
      }

      MoppeFloat4 f4 (DisplayColor color, float w = 0.0f) {
        MoppeFloat4 r;
        r.x = color.red;
        r.y = color.green;
        r.z = color.blue;
        r.w = w;
        return r;
      }

      // Game-side colors are authored in display space; the scene
      // lights in linear.  Decode at the uniform boundary.
      MoppeFloat4 f4lin (DisplayColor color, float w = 0.0f) {
        MoppeFloat4 r;
        r.x = std::pow (std::max (color.red, 0.0f), 2.2f);
        r.y = std::pow (std::max (color.green, 0.0f), 2.2f);
        r.z = std::pow (std::max (color.blue, 0.0f), 2.2f);
        r.w = w;
        return r;
      }

      MoppeMat4 m4 (const Mat4& m) {
        MoppeMat4 r;
        std::memcpy (&r, m.m, sizeof (r));
        return r;
      }

      struct MetalTexture : public Texture {
        id<MTLTexture> texture = nil;
        id<MTLSamplerState> sampler = nil;
      };

      struct MetalMesh : public Mesh {
        id<MTLBuffer> vertices = nil;
        std::vector<DrawList::Run> runs;
      };

      // The private Metal backend keeps long-lived GPU state in concrete
      // owners.  Passes borrow these owners explicitly below; this is a fixed
      // game-shaped frame path, not a render-graph abstraction.
      struct MetalPipelines {
        id<MTLRenderPipelineState> uber_opaque = nil, uber_blend = nil;
        id<MTLRenderPipelineState> uber_add = nil;
        id<MTLRenderPipelineState> hud = nil;
        id<MTLRenderPipelineState> present = nil, ghost = nil;
        id<MTLRenderPipelineState> copy = nil;
        id<MTLRenderPipelineState> underwater = nil;
        id<MTLRenderPipelineState> bloom_bright = nil;
        id<MTLRenderPipelineState> bloom_blur = nil;
        id<MTLRenderPipelineState> probe = nil;
        id<MTLRenderPipelineState> terrain = nil, terrain_shadow = nil;
        id<MTLRenderPipelineState> sky = nil, ocean = nil;
        id<MTLRenderPipelineState> dust_soft = nil, dust_add = nil;
        id<MTLRenderPipelineState> dust_mesh_soft = nil;
        id<MTLRenderPipelineState> dust_mesh_add = nil;
        id<MTLRenderPipelineState> water_tiles = nil;
        id<MTLRenderPipelineState> river = nil;
        bool mesh_shaders_ok = false;

        // Depth-stencil: index [test][write], reversed-Z (>=).
        id<MTLDepthStencilState> depth[2][2] {};
        id<MTLDepthStencilState> shadow_depth = nil;
        id<MTLDepthStencilState> river_depth = nil;
        id<MTLSamplerState> sampler_repeat = nil;
        id<MTLSamplerState> sampler_clamp = nil;
        TexturePtr white;
      };

      struct MetalTerrainResources {
        // Shadow maps are published with the completed terrain and remain
        // available to terrain, water, and immediate scene geometry.
        id<MTLTexture> shadow_map = nil;
        id<MTLTexture> previous_shadow_map = nil;
        Mat4 light_biased;
        bool have_shadow = false;

        id<MTLTexture> heights = nil, previous_heights = nil;
        id<MTLTexture> normals = nil;
        float height_transition_start = 0.0f;
        bool height_transition_active = false;
        uint64_t height_transition_generation = 0;
        uint64_t shadow_transition_generation = 0;
        id<MTLBuffer> indices[TERRAIN_LOD_COUNT] {};
        uint32_t index_count[TERRAIN_LOD_COUNT] {};
        TerrainParams params;
        bool have_terrain = false;
        TexturePtr grass, dirt, rock, snow;
        id<MTLTexture> overlay = nil;
        TerrainOverlayParams overlay_params {};
        bool have_overlay = false;

        // These raster fields share the terrain domain.  Water borrows the
        // geology field but does not own terrain presentation data.
        id<MTLTexture> moisture = nil;
        bool have_moisture = false;
        id<MTLTexture> forest = nil;
        bool have_forest = false;
        id<MTLTexture> snow_support = nil;
        bool have_snow_support = false;
        id<MTLTexture> channel_flux = nil;
        bool have_channel_flux = false;
        id<MTLTexture> geology = nil;
        bool have_geology = false;
        id<MTLTexture> shore = nil;
        bool have_shore = false;
        id<MTLTexture> paths = nil;
        bool have_paths = false;
      };

      struct MetalWaterResources {
        id<MTLBuffer> ocean_verts = nil;
        uint32_t ocean_vcount = 0;
        float ocean_level = 0;
        id<MTLTexture> water_levels = nil;
        bool have_water_levels = false;
        id<MTLTexture> water_flow = nil;
        bool have_water_flow = false;
      };

      struct MetalSceneResources {
        id<MTLBuffer> sky_verts = nil;
        uint32_t sky_vcount = 0;
      };

      struct MetalFrameTargets {
        // Scene-scaled; bloom is quarter scene resolution.
        id<MTLTexture> msaa_color = nil, msaa_depth = nil;
        id<MTLTexture> scene_a = nil, scene_b = nil;
        id<MTLTexture> prev_frame = nil;
        id<MTLTexture> bloom_a = nil, bloom_b = nil;
        bool prev_valid = false;

        // The luminance probe returns through the in-flight ring before it
        // updates exposure, so it belongs with the temporal targets.
        id<MTLTexture> probe_tex = nil;
        id<MTLBuffer> probe_buf[FRAMES_IN_FLIGHT] {};
        float exposure = 1.0f;
        int width = 0, height = 0;
      };

      struct MetalFrameEncoding {
        // A single scene encoder is shared by terrain, water, and scene
        // passes.  The stream is likewise shared by world DrawLists and HUD.
        dispatch_semaphore_t inflight;
        int slot = 0;
        id<MTLBuffer> stream[FRAMES_IN_FLIGHT] {};
        size_t stream_used = 0;
        id<MTLCommandBuffer> command_buffer = nil;
        id<MTLRenderCommandEncoder> scene_encoder = nil;
        id<CAMetalDrawable> drawable = nil;
        id<CAMetalDrawable> pending_drawable = nil;
        id<MTLTexture> current_scene = nil;
        bool scene_pass_done = false;
        FrameParams params;
        MoppeFrameUniforms uniforms;
        int width_pts = 0, height_pts = 0;
        float scale = 1.0f;
        float edr_headroom = 1.0f;
        std::string screenshot_path;

        // Timestamp state stays whole-frame so pass labels retain their
        // benchmark meaning even though their encoder ownership is split.
        bool profile_this_frame = false;
        id<MTLCounterSampleBuffer> timestamp_samples[FRAMES_IN_FLIGHT] {};
        id<MTLBuffer> timestamp_results[FRAMES_IN_FLIGHT] {};
        std::vector<GpuPass> sample_passes;
        int timestamp_count = 0;
        GpuPass current_gpu_pass = GpuPass::Count;
        bool draw_boundary_timestamps = false;
        bool stage_boundary_timestamps = false;
#if defined(TRACY_ENABLE) && !TARGET_OS_IPHONE
        TracyMetalCtx* tracy_metal = nullptr;
#endif

#if !TARGET_OS_IPHONE
        bool capture_active = false;
        int capture_frames = 0;
        int capture_frame_limit = 120;
        std::string capture_path;
#endif
      };

      void record_gpu_pass_start (MetalFrameEncoding& frame,
                                  id<MTLRenderCommandEncoder> encoder,
                                  GpuPass pass) {
        if (!frame.draw_boundary_timestamps || !frame.profile_this_frame ||
            !frame.timestamp_samples[frame.slot] ||
            pass == frame.current_gpu_pass ||
            frame.timestamp_count >= MAX_TIMESTAMP_SAMPLES - 1)
          return;
        [encoder sampleCountersInBuffer:frame.timestamp_samples[frame.slot]
                          atSampleIndex:frame.timestamp_count
                            withBarrier:YES];
        ++frame.timestamp_count;
        frame.sample_passes.push_back (pass);
        frame.current_gpu_pass = pass;
      }

      void record_gpu_pass_end (MetalFrameEncoding& frame,
                                id<MTLRenderCommandEncoder> encoder) {
        if (!frame.draw_boundary_timestamps || !frame.profile_this_frame ||
            !frame.timestamp_samples[frame.slot] ||
            frame.timestamp_count == 0 ||
            frame.timestamp_count >= MAX_TIMESTAMP_SAMPLES)
          return;
        [encoder sampleCountersInBuffer:frame.timestamp_samples[frame.slot]
                          atSampleIndex:frame.timestamp_count
                            withBarrier:YES];
        ++frame.timestamp_count;
        frame.current_gpu_pass = GpuPass::Count;
      }

      void attach_gpu_pass_timing (MetalFrameEncoding& frame,
                                   MTLRenderPassDescriptor* descriptor,
                                   GpuPass pass) {
        if (!frame.stage_boundary_timestamps || !frame.profile_this_frame ||
            !frame.timestamp_samples[frame.slot] ||
            frame.timestamp_count + 2 > MAX_TIMESTAMP_SAMPLES)
          return;
        MTLRenderPassSampleBufferAttachmentDescriptor* attachment =
          descriptor.sampleBufferAttachments[
#if defined(TRACY_ENABLE) && !TARGET_OS_IPHONE
            1
#else
            0
#endif
        ];
        attachment.sampleBuffer = frame.timestamp_samples[frame.slot];
        attachment.startOfVertexSampleIndex = frame.timestamp_count++;
        attachment.endOfFragmentSampleIndex = frame.timestamp_count++;
        frame.sample_passes.push_back (pass);
      }

#if defined(TRACY_ENABLE) && !TARGET_OS_IPHONE
      TracyMetalCtx* metal_tracy (MetalFrameEncoding& frame) {
        return frame.tracy_metal;
      }
#else
      [[maybe_unused]] void* metal_tracy (MetalFrameEncoding&) {
        return nullptr;
      }
#endif

      struct MetalTerrainPassInputs {
        id<MTLRenderCommandEncoder> encoder;
        const MetalPipelines& pipelines;
        MetalTerrainResources& terrain;
        const MetalWaterResources& water;
        const MetalFrameEncoding& frame;
      };

      struct MetalWaterPassInputs {
        id<MTLRenderCommandEncoder> encoder;
        const MetalPipelines& pipelines;
        const MetalTerrainResources& terrain;
        const MetalWaterResources& water;
        const MetalFrameEncoding& frame;
      };

      struct MetalDrawListInputs {
        id<MTLDevice> device;
        id<MTLRenderCommandEncoder> encoder;
        const MetalPipelines& pipelines;
        const MetalTerrainResources& terrain;
        MetalFrameEncoding& frame;
      };

      struct MetalScenePassInputs {
        id<MTLDevice> device;
        id<MTLRenderCommandEncoder> encoder;
        const MetalPipelines& pipelines;
        const MetalTerrainResources& terrain;
        MetalSceneResources& scene;
        MetalFrameEncoding& frame;
      };

      struct MetalPostPassInputs {
        const MetalPipelines& pipelines;
        MetalFrameTargets& targets;
        MetalFrameEncoding& frame;
      };

      struct MetalHudPassInputs {
        id<MTLDevice> device;
        const MetalPipelines& pipelines;
        const MetalTerrainResources& terrain;
        MetalFrameTargets& targets;
        MetalFrameEncoding& frame;
      };

      class MetalTerrainPass {
      public:
        static void draw (const MetalTerrainPassInputs& inputs,
                          const ChunkDraw* chunks,
                          int count);
      };

      class MetalWaterPass {
      public:
        static void draw_ocean (const MetalWaterPassInputs& inputs,
                                const OceanParams& params);
        static void draw_rivers (const MetalWaterPassInputs& inputs,
                                 const Mesh& mesh,
                                 const Mat4& model);
      };

      class MetalDrawListEncoder {
      public:
        static void play (const MetalDrawListInputs& inputs,
                          const std::vector<Vertex>& vertices,
                          const std::vector<DrawList::Run>& runs,
                          bool hud);

        static size_t stream_vertices (id<MTLDevice> device,
                                       MetalFrameEncoding& frame,
                                       const std::vector<Vertex>& vertices);
        static void set_run_state (id<MTLRenderCommandEncoder> encoder,
                                   const MetalPipelines& pipelines,
                                   const MetalTerrainResources& terrain,
                                   const DrawState& state,
                                   const Texture* texture,
                                   bool hud);
      };

      class MetalScenePass {
      public:
        static void draw_sky (const MetalScenePassInputs& inputs,
                              const SkyParams& params);
        static void draw_dust (const MetalScenePassInputs& inputs,
                               std::span<const DustEmission> emissions,
                               float logical_time);
        static void draw_list (const MetalScenePassInputs& inputs,
                               const DrawList& list);
        static void draw_mesh (const MetalScenePassInputs& inputs,
                               const Mesh& mesh,
                               const Mat4& model);
      };

      class MetalPostPass {
      public:
        static void apply_underwater (const MetalPostPassInputs& inputs,
                                      float time);
        static void apply_motion_blur (const MetalPostPassInputs& inputs,
                                       float strength);
        static void apply_scene_blur (const MetalPostPassInputs& inputs);
      };

      class MetalHudPass {
      public:
        static void draw (const MetalHudPassInputs& inputs,
                          const DrawList& list);
      };
    }

    class MetalRenderer : public Renderer {
    public:
      MetalRenderer (MTKView* view, const std::string& lib_path);
      ~MetalRenderer () override;

      // resources
      TexturePtr create_texture (const TextureDesc& desc,
                                 const void* pixels) override;
      MeshPtr create_mesh (const DrawList& recorded) override;

      // world setup
      void set_terrain (const TerrainParams& params,
                        const float* heights,
                        const Vec3* normals) override;
      void set_terrain_topology_overlay (bool enabled) override;
      void set_terrain_textures (TexturePtr grass,
                                 TexturePtr dirt,
                                 TexturePtr rock,
                                 TexturePtr snow) override;
      void set_terrain_overlay (const TerrainOverlayParams& params,
                                std::span<const float> values) override;
      void clear_terrain_overlay () override;
      void render_terrain_shadow (const Mat4& light_view_proj) override;
      void set_ocean (const OceanSetup& setup,
                      std::span<const float> water_levels) override;
      void set_water_flow (std::span<const float> flow) override;
      void set_terrain_moisture (std::span<const float> moisture) override;
      void set_terrain_forest (std::span<const float> cover) override;
      void set_terrain_snow_support (std::span<const float> support) override;
      void set_terrain_channel_flux (std::span<const float> flux) override;
      void set_terrain_geology (std::span<const float> geology) override;
      void set_terrain_shore (std::span<const float> distance) override;
      void
      set_terrain_paths (std::span<const float> influence,
                         std::span<const float> home_base_influence) override;

      // frame
      bool begin_frame (const FrameParams& params) override;
      void set_next_drawable (id<CAMetalDrawable> drawable) {
        m_frame.pending_drawable = drawable;
      }
      void draw_terrain (const ChunkDraw* chunks, int count) override;
      void draw_sky (const SkyParams& params) override;
      void draw_ocean (const OceanParams& params) override;
      void draw_dust (std::span<const DustEmission> emissions,
                      float logical_time) override;
      void draw_rivers (const Mesh& mesh, const Mat4& model) override;
      void draw_mesh (const Mesh& mesh, const Mat4& model) override;
      void draw_list (const DrawList& list) override;
      void apply_underwater (float time) override;
      void apply_motion_blur (float strength) override;
      void apply_scene_blur () override;
      void draw_hud (const DrawList& list) override;
      void request_screenshot (const std::string& path) override;
      void end_frame () override;
      bool benchmark_complete () const override;
      void reset_temporal_state () override;
      void write_benchmark_results () override;

      int width_pts () const override {
        return m_frame.width_pts;
      }
      int height_pts () const override {
        return m_frame.height_pts;
      }
      float scale_factor () const override {
        return m_frame.scale;
      }

    private:
      void build_pipelines ();
      void ensure_targets (float requested_scale, float scale_override);
      id<MTLTexture> make_target (
        MTLPixelFormat fmt, int w, int h, int samples, bool memoryless);
      void upload_texture (id<MTLTexture> tex,
                           const void* pixels,
                           int w,
                           int h,
                           int bytes_per_pixel,
                           bool gen_mips);
      id<MTLRenderPipelineState> make_pipeline (NSString* vs,
                                                NSString* fs,
                                                MTLPixelFormat color,
                                                MTLPixelFormat depth,
                                                int samples,
                                                bool blend,
                                                bool additive = false);

      // scene-pass encoding helpers
      id<MTLRenderCommandEncoder> scene_encoder ();
      void end_scene_encoder ();
      void update_exposure ();
      void begin_gpu_pass (id<MTLRenderCommandEncoder> enc, GpuPass pass);
      void finish_gpu_pass (id<MTLRenderCommandEncoder> enc);
      void attach_gpu_pass (MTLRenderPassDescriptor* desc, GpuPass pass);

      MTKView* m_view;
      id<MTLDevice> m_device;
      id<MTLCommandQueue> m_queue;
      id<MTLLibrary> m_library;

      MetalPipelines m_pipelines;
      MetalTerrainResources m_terrain_resources;
      MetalWaterResources m_water_resources;
      MetalSceneResources m_scene_resources;
      MetalFrameTargets m_targets;
      MetalFrameEncoding m_frame;
      bool m_memoryless_ok = false;
      bool m_profile_gpu = false;
      bool m_profile_gpu_passes = false;
      std::shared_ptr<FrameTiming> m_frame_timing;
      std::shared_ptr<BenchmarkOutput> m_benchmark;
      bool m_profile_cpu = false;
      double m_cpu_frame_start = 0;
      double m_cpu_encode_start = 0;
      double m_cpu_interval_start = 0;
      double m_cpu_targets_total = 0;
      double m_cpu_inflight_total = 0;
      double m_cpu_drawable_total = 0;
      double m_cpu_encode_total = 0;
      int m_cpu_frames = 0;
    };

    // ------------------------------------------------------------------

    MetalRenderer::MetalRenderer (MTKView* view, const std::string& lib_path) {
      m_view = view;
      m_device = view.device ? view.device : MTLCreateSystemDefaultDevice ();
      m_queue = [m_device newCommandQueue];
#if defined(TRACY_ENABLE) && !TARGET_OS_IPHONE
      m_frame.tracy_metal = TracyMetalContext (m_device);
      if (m_frame.tracy_metal)
        TracyMetalContextName (m_frame.tracy_metal, "Moppe Metal", 11);
#endif

      const bool requested_gpu_passes =
        ::getenv ("MOPPE_PROFILE_GPU") != nullptr;
#if defined(TRACY_ENABLE) && !TARGET_OS_IPHONE
      // Metal cannot reliably resolve Tracy's descriptor timestamps while a
      // second profiler samples draw/stage boundaries in the same encoders.
      // Keep Moppe's whole-command-buffer timing and let Tracy own pass data.
      m_profile_gpu_passes = false;
      if (requested_gpu_passes)
        std::cerr << "moppe: Tracy owns Metal encoder timestamps; "
                  << "using command-buffer timing for MOPPE_PROFILE_GPU\n";
#else
      m_profile_gpu_passes = requested_gpu_passes;
#endif
      m_profile_gpu = requested_gpu_passes ||
                      ::getenv ("MOPPE_PROFILE_GPU_SIMPLE") != nullptr;
      m_profile_cpu = ::getenv ("MOPPE_PROFILE_CPU") != nullptr;
      if (m_profile_gpu)
        m_frame_timing = std::make_shared<FrameTiming> ();
      if (const char* path = ::getenv ("MOPPE_BENCHMARK_OUTPUT")) {
        m_benchmark = std::make_shared<BenchmarkOutput> ();
        m_benchmark->path = path;
        if (const char* expected = ::getenv ("MOPPE_BENCHMARK_EXPECTED"))
          m_benchmark->expected = std::max (1, ::atoi (expected));
        if (const char* names = ::getenv ("MOPPE_BENCHMARK_FEATURES")) {
          std::istringstream input (names);
          std::string name;
          while (std::getline (input, name, ','))
            m_benchmark->feature_names.push_back (name);
        }
      }

      if (m_profile_gpu_passes) {
        if (@available (macOS 11.0, iOS 14.0, tvOS 14.0, *)) {
          id<MTLCounterSet> timestamps = nil;
          for (id<MTLCounterSet> set in m_device.counterSets)
            if ([set.name isEqualToString:MTLCommonCounterSetTimestamp]) {
              timestamps = set;
              break;
            }
          m_frame.draw_boundary_timestamps =
            timestamps &&
            [m_device
              supportsCounterSampling:MTLCounterSamplingPointAtDrawBoundary];
          m_frame.stage_boundary_timestamps =
            timestamps &&
            [m_device
              supportsCounterSampling:MTLCounterSamplingPointAtStageBoundary];
          if (timestamps && (m_frame.draw_boundary_timestamps ||
                             m_frame.stage_boundary_timestamps)) {
            if (!m_frame.draw_boundary_timestamps &&
                m_frame.stage_boundary_timestamps)
              std::cerr
                << "moppe: GPU pass timing uses encoder-stage timestamps; "
                << "spans may overlap" << std::endl;
            for (int i = 0; i < FRAMES_IN_FLIGHT; ++i) {
              MTLCounterSampleBufferDescriptor* desc =
                [[MTLCounterSampleBufferDescriptor alloc] init];
              desc.counterSet = timestamps;
              desc.storageMode = MTLStorageModePrivate;
              desc.sampleCount = MAX_TIMESTAMP_SAMPLES;
              desc.label = @"Moppe frame pass timestamps";
              NSError* error = nil;
              m_frame.timestamp_samples[i] =
                [m_device newCounterSampleBufferWithDescriptor:desc
                                                         error:&error];
              m_frame.timestamp_results[i] =
                [m_device newBufferWithLength:MAX_TIMESTAMP_SAMPLES *
                                              sizeof (MTLCounterResultTimestamp)
                                      options:MTLResourceStorageModeShared];
              if (!m_frame.timestamp_samples[i]) {
                std::cerr << "moppe: GPU pass timestamps unavailable: "
                          << error.localizedDescription.UTF8String << std::endl;
                break;
              }
            }
          } else {
            std::cerr << "moppe: per-pass GPU timestamps unsupported"
                      << " (timestamp set=" << (timestamps ? "yes" : "no")
                      << ", draw-boundary="
                      << ([m_device supportsCounterSampling:
                                      MTLCounterSamplingPointAtDrawBoundary]
                            ? "yes"
                            : "no")
                      << ", stage-boundary="
                      << ([m_device supportsCounterSampling:
                                      MTLCounterSamplingPointAtStageBoundary]
                            ? "yes"
                            : "no")
                      << ")" << std::endl;
          }
        }
      }

#if !TARGET_OS_IPHONE
      if (const char* requested = ::getenv ("MOPPE_METAL_CAPTURE")) {
        m_frame.capture_path = requested;
        if (const char* frames = ::getenv ("MOPPE_METAL_CAPTURE_FRAMES"))
          m_frame.capture_frame_limit = std::max (1, ::atoi (frames));
      }
#endif

      view.device = m_device;
#if TARGET_OS_IPHONE
      view.colorPixelFormat = MTLPixelFormatBGRA8Unorm;
#else
      view.colorPixelFormat = MTLPixelFormatRGBA16Float;
#endif
      view.depthStencilPixelFormat = MTLPixelFormatInvalid;
      view.sampleCount = 1;
#if !TARGET_OS_IPHONE
      {
        CGColorSpaceRef linear =
          CGColorSpaceCreateWithName (kCGColorSpaceExtendedLinearSRGB);
        view.colorspace = linear;
        CGColorSpaceRelease (linear);
        CAMetalLayer* layer = (CAMetalLayer*)view.layer;
        layer.wantsExtendedDynamicRangeContent = YES;
        layer.displaySyncEnabled = YES;
      }
      if (view.window)
        m_frame.scale = (float)view.window.backingScaleFactor;
#else
      m_frame.scale = (float)view.contentScaleFactor;
#endif

#if TARGET_OS_SIMULATOR
      m_memoryless_ok = false;
#else
      m_memoryless_ok = [m_device supportsFamily:MTLGPUFamilyApple2];
#endif
      if (@available (macOS 13.0, iOS 16.0, tvOS 16.0, *))
        m_pipelines.mesh_shaders_ok =
          [m_device supportsFamily:MTLGPUFamilyMetal3];

      std::cerr << "moppe: Metal: device=" << m_device.name.UTF8String
                << ", frames-in-flight=" << FRAMES_IN_FLIGHT
                << ", memoryless=" << (m_memoryless_ok ? "yes" : "no")
                << ", mesh-shaders="
                << (m_pipelines.mesh_shaders_ok ? "yes" : "no") << std::endl;

      NSError* error = nil;
      NSString* path = [NSString stringWithUTF8String:lib_path.c_str ()];
      NSURL* url = [NSURL fileURLWithPath:path];
      if ([path.pathExtension isEqualToString:@"metal"]) {
        NSString* source =
          [NSString stringWithContentsOfFile:path
                                    encoding:NSUTF8StringEncoding
                                       error:&error];
        if (source)
          m_library = [m_device newLibraryWithSource:source
                                             options:nil
                                               error:&error];
      } else {
        m_library = [m_device newLibraryWithURL:url error:&error];
      }
      if (!m_library) {
        std::cerr << "moppe: failed to load shader library at " << lib_path
                  << ": "
                  << (error ? error.localizedDescription.UTF8String : "?")
                  << std::endl;
        abort ();
      }

      build_pipelines ();

      m_frame.inflight = dispatch_semaphore_create (FRAMES_IN_FLIGHT);
      for (int i = 0; i < FRAMES_IN_FLIGHT; ++i)
        m_frame.stream[i] = nil;

      // 1x1 white fallback texture.
      TextureDesc wd;
      wd.width = wd.height = 1;
      wd.format = TextureFormat::RGBA8;
      wd.filter = TextureFilter::Nearest;
      const uint8_t white[4] = { 255, 255, 255, 255 };
      m_pipelines.white = create_texture (wd, white);
    }

    MetalRenderer::~MetalRenderer () {
#if defined(TRACY_ENABLE) && !TARGET_OS_IPHONE
      if (m_frame.tracy_metal) {
        id<MTLCommandBuffer> fence = [m_queue commandBuffer];
        [fence commit];
        [fence waitUntilCompleted];
        TracyMetalCollect (m_frame.tracy_metal);
        TracyMetalDestroy (m_frame.tracy_metal);
      }
#endif
    }

    id<MTLRenderPipelineState>
    MetalRenderer::make_pipeline (NSString* vs,
                                  NSString* fs,
                                  MTLPixelFormat color,
                                  MTLPixelFormat depth,
                                  int samples,
                                  bool blend,
                                  bool additive) {
      id<MTLFunction> vf = [m_library newFunctionWithName:vs];
      id<MTLFunction> ff = fs ? [m_library newFunctionWithName:fs] : nil;
      if (!vf || (fs && !ff)) {
        // Shader not present (yet) -- callers treat nil as "skip".
        return nil;
      }

      MTLRenderPipelineDescriptor* d =
        [[MTLRenderPipelineDescriptor alloc] init];
      d.vertexFunction = vf;
      d.fragmentFunction = ff;
      d.rasterSampleCount = samples;
      if (color != MTLPixelFormatInvalid) {
        d.colorAttachments[0].pixelFormat = color;
        if (blend) {
          // Additive keeps src-alpha as the throttle but sums into
          // the framebuffer: overlapping glow builds toward white.
          const MTLBlendFactor dst =
            additive ? MTLBlendFactorOne : MTLBlendFactorOneMinusSourceAlpha;
          d.colorAttachments[0].blendingEnabled = YES;
          d.colorAttachments[0].sourceRGBBlendFactor =
            MTLBlendFactorSourceAlpha;
          d.colorAttachments[0].destinationRGBBlendFactor = dst;
          d.colorAttachments[0].sourceAlphaBlendFactor =
            MTLBlendFactorSourceAlpha;
          d.colorAttachments[0].destinationAlphaBlendFactor = dst;
        }
      }
      d.depthAttachmentPixelFormat = depth;
      if (depth == MTLPixelFormatDepth32Float_Stencil8)
        d.stencilAttachmentPixelFormat = depth;

      NSError* error = nil;
      id<MTLRenderPipelineState> pso =
        [m_device newRenderPipelineStateWithDescriptor:d error:&error];
      if (!pso) {
        std::cerr << "moppe: pipeline " << vs.UTF8String << "/"
                  << (fs ? fs.UTF8String : "-") << " failed: "
                  << (error ? error.localizedDescription.UTF8String : "?")
                  << std::endl;
        abort ();
      }
      return pso;
    }

    void MetalRenderer::build_pipelines () {
      // The scene renders HDR (half-float, scene-referred with headroom
      // above 1.0). macOS keeps that headroom through an EDR drawable;
      // iOS uses the existing 8-bit SDR presentation path.
      const MTLPixelFormat scene = MTLPixelFormatRGBA16Float;
#if TARGET_OS_IPHONE
      const MTLPixelFormat drawable = MTLPixelFormatBGRA8Unorm;
#else
      const MTLPixelFormat drawable = MTLPixelFormatRGBA16Float;
#endif
      // The scene depth target carries a stencil plane so self-overlapping
      // translucent surfaces (river strips) can blend first-fragment-wins.
      const MTLPixelFormat depth = MTLPixelFormatDepth32Float_Stencil8;

      m_pipelines.uber_opaque = make_pipeline (
        @"uber_vertex", @"uber_fragment", scene, depth, MSAA_SAMPLES, false);
      m_pipelines.uber_blend = make_pipeline (
        @"uber_vertex", @"uber_fragment", scene, depth, MSAA_SAMPLES, true);
      m_pipelines.uber_add = make_pipeline (@"uber_vertex",
                                            @"uber_fragment",
                                            scene,
                                            depth,
                                            MSAA_SAMPLES,
                                            true,
                                            true);
      m_pipelines.hud = make_pipeline (@"hud_vertex",
                                       @"hud_fragment",
                                       drawable,
                                       MTLPixelFormatInvalid,
                                       1,
                                       true);
      m_pipelines.present = make_pipeline (@"quad_vertex",
                                           @"present_fragment",
                                           drawable,
                                           MTLPixelFormatInvalid,
                                           1,
                                           false);
      m_pipelines.ghost = make_pipeline (@"quad_vertex",
                                         @"quad_fragment",
                                         scene,
                                         MTLPixelFormatInvalid,
                                         1,
                                         true);
      m_pipelines.copy = make_pipeline (@"quad_vertex",
                                        @"quad_fragment",
                                        scene,
                                        MTLPixelFormatInvalid,
                                        1,
                                        false);
      m_pipelines.underwater = make_pipeline (@"quad_vertex",
                                              @"underwater_fragment",
                                              scene,
                                              MTLPixelFormatInvalid,
                                              1,
                                              false);
      m_pipelines.bloom_bright = make_pipeline (@"quad_vertex",
                                                @"bloom_bright_fragment",
                                                scene,
                                                MTLPixelFormatInvalid,
                                                1,
                                                false);
      m_pipelines.bloom_blur = make_pipeline (@"quad_vertex",
                                              @"bloom_blur_fragment",
                                              scene,
                                              MTLPixelFormatInvalid,
                                              1,
                                              false);
      m_pipelines.probe = make_pipeline (@"quad_vertex",
                                         @"probe_fragment",
                                         MTLPixelFormatRGBA32Float,
                                         MTLPixelFormatInvalid,
                                         1,
                                         false);
      m_pipelines.terrain = make_pipeline (@"terrain_vertex",
                                           @"terrain_fragment",
                                           scene,
                                           depth,
                                           MSAA_SAMPLES,
                                           false);
      m_pipelines.terrain_shadow = make_pipeline (@"terrain_shadow_vertex",
                                                  nil,
                                                  MTLPixelFormatInvalid,
                                                  MTLPixelFormatDepth16Unorm,
                                                  1,
                                                  false);
      m_pipelines.sky = make_pipeline (
        @"sky_vertex", @"sky_fragment", scene, depth, MSAA_SAMPLES, false);
      m_pipelines.ocean = make_pipeline (
        @"ocean_vertex", @"ocean_fragment", scene, depth, MSAA_SAMPLES, true);
      m_pipelines.dust_soft = make_pipeline (
        @"dust_vertex", @"dust_fragment", scene, depth, MSAA_SAMPLES, true);
      m_pipelines.dust_add = make_pipeline (@"dust_vertex",
                                            @"dust_fragment",
                                            scene,
                                            depth,
                                            MSAA_SAMPLES,
                                            true,
                                            true);
      if (m_pipelines.mesh_shaders_ok) {
        if (@available (macOS 13.0, iOS 16.0, tvOS 16.0, *)) {
          const auto make_dust_mesh = [&] (bool additive) {
            MTLMeshRenderPipelineDescriptor* p =
              [[MTLMeshRenderPipelineDescriptor alloc] init];
            p.meshFunction = [m_library newFunctionWithName:@"dust_mesh"];
            p.fragmentFunction =
              [m_library newFunctionWithName:@"dust_fragment"];
            p.rasterSampleCount = MSAA_SAMPLES;
            p.colorAttachments[0].pixelFormat = scene;
            p.colorAttachments[0].blendingEnabled = YES;
            p.colorAttachments[0].sourceRGBBlendFactor =
              MTLBlendFactorSourceAlpha;
            p.colorAttachments[0].destinationRGBBlendFactor =
              additive ? MTLBlendFactorOne : MTLBlendFactorOneMinusSourceAlpha;
            p.colorAttachments[0].sourceAlphaBlendFactor =
              MTLBlendFactorSourceAlpha;
            p.colorAttachments[0].destinationAlphaBlendFactor =
              MTLBlendFactorOneMinusSourceAlpha;
            p.depthAttachmentPixelFormat = depth;
            p.stencilAttachmentPixelFormat = depth;
            p.maxTotalThreadsPerMeshThreadgroup = 64;
            if (!p.meshFunction || !p.fragmentFunction)
              return (id<MTLRenderPipelineState>)nil;
            NSError* error = nil;
            id<MTLRenderPipelineState> result = [m_device
              newRenderPipelineStateWithMeshDescriptor:p
                                               options:MTLPipelineOptionNone
                                            reflection:nil
                                                 error:&error];
            if (!result)
              std::cerr << "moppe: dust mesh pipeline failed: "
                        << (error ? error.localizedDescription.UTF8String : "?")
                        << std::endl;
            return result;
          };
          m_pipelines.dust_mesh_soft = make_dust_mesh (false);
          m_pipelines.dust_mesh_add = make_dust_mesh (true);

          // Lattice water tiles: the near standing-water surface on the
          // terrain's own sample grid, sharing the ocean fragment shader.
          MTLMeshRenderPipelineDescriptor* w =
            [[MTLMeshRenderPipelineDescriptor alloc] init];
          w.objectFunction =
            [m_library newFunctionWithName:@"water_tile_object"];
          w.meshFunction = [m_library newFunctionWithName:@"water_tile_mesh"];
          w.fragmentFunction =
            [m_library newFunctionWithName:@"ocean_fragment"];
          w.rasterSampleCount = MSAA_SAMPLES;
          w.colorAttachments[0].pixelFormat = scene;
          w.colorAttachments[0].blendingEnabled = YES;
          w.colorAttachments[0].sourceRGBBlendFactor =
            MTLBlendFactorSourceAlpha;
          w.colorAttachments[0].destinationRGBBlendFactor =
            MTLBlendFactorOneMinusSourceAlpha;
          w.colorAttachments[0].sourceAlphaBlendFactor =
            MTLBlendFactorSourceAlpha;
          w.colorAttachments[0].destinationAlphaBlendFactor =
            MTLBlendFactorOneMinusSourceAlpha;
          w.depthAttachmentPixelFormat = depth;
          w.stencilAttachmentPixelFormat = depth;
          w.payloadMemoryLength = 1024;
          w.maxTotalThreadsPerObjectThreadgroup = 64;
          w.maxTotalThreadsPerMeshThreadgroup = 256;
          if (w.objectFunction && w.meshFunction && w.fragmentFunction) {
            NSError* error = nil;
            m_pipelines.water_tiles = [m_device
              newRenderPipelineStateWithMeshDescriptor:w
                                               options:MTLPipelineOptionNone
                                            reflection:nil
                                                 error:&error];
            if (!m_pipelines.water_tiles)
              std::cerr << "moppe: water tile pipeline failed: "
                        << (error ? error.localizedDescription.UTF8String : "?")
                        << std::endl;
          }
        }
      }
      m_pipelines.river = make_pipeline (
        @"river_vertex", @"river_fragment", scene, depth, MSAA_SAMPLES, true);

      // Depth-stencil states, reversed-Z.
      for (int test = 0; test < 2; ++test)
        for (int write = 0; write < 2; ++write) {
          MTLDepthStencilDescriptor* d =
            [[MTLDepthStencilDescriptor alloc] init];
          d.depthCompareFunction =
            test ? MTLCompareFunctionGreaterEqual : MTLCompareFunctionAlways;
          d.depthWriteEnabled = write ? YES : NO;
          m_pipelines.depth[test][write] =
            [m_device newDepthStencilStateWithDescriptor:d];
        }
      {
        MTLDepthStencilDescriptor* d = [[MTLDepthStencilDescriptor alloc] init];
        d.depthCompareFunction = MTLCompareFunctionLessEqual;
        d.depthWriteEnabled = YES;
        m_pipelines.shadow_depth =
          [m_device newDepthStencilStateWithDescriptor:d];
      }
      {
        // First-fragment-wins for the translucent river strips: the first
        // water fragment on a pixel stamps stencil 1, and every later
        // overlapping strip fragment fails the Equal-0 test, so bends and
        // confluences never double-blend into dark wedges.
        MTLDepthStencilDescriptor* d = [[MTLDepthStencilDescriptor alloc] init];
        d.depthCompareFunction = MTLCompareFunctionGreaterEqual;
        d.depthWriteEnabled = NO;
        // Drawn with reference value 1: pass while the pixel still holds
        // the cleared 0, then Replace stamps the reference in.
        MTLStencilDescriptor* s = [[MTLStencilDescriptor alloc] init];
        s.stencilCompareFunction = MTLCompareFunctionNotEqual;
        s.depthStencilPassOperation = MTLStencilOperationReplace;
        d.frontFaceStencil = s;
        d.backFaceStencil = s;
        m_pipelines.river_depth =
          [m_device newDepthStencilStateWithDescriptor:d];
      }

      // Samplers.
      {
        MTLSamplerDescriptor* d = [[MTLSamplerDescriptor alloc] init];
        d.minFilter = MTLSamplerMinMagFilterLinear;
        d.magFilter = MTLSamplerMinMagFilterLinear;
        d.mipFilter = MTLSamplerMipFilterLinear;
        d.sAddressMode = MTLSamplerAddressModeRepeat;
        d.tAddressMode = MTLSamplerAddressModeRepeat;
        d.maxAnisotropy = 8;
        m_pipelines.sampler_repeat = [m_device newSamplerStateWithDescriptor:d];
        d.sAddressMode = MTLSamplerAddressModeClampToEdge;
        d.tAddressMode = MTLSamplerAddressModeClampToEdge;
        d.maxAnisotropy = 1;
        m_pipelines.sampler_clamp = [m_device newSamplerStateWithDescriptor:d];
      }
    }

    // -- resources -----------------------------------------------------

    void MetalRenderer::upload_texture (id<MTLTexture> tex,
                                        const void* pixels,
                                        int w,
                                        int h,
                                        int bpp,
                                        bool gen_mips) {
      MOPPE_PROFILE_ZONE ("MetalRenderer::upload_texture");
      const size_t bytes = (size_t)w * h * bpp;
      id<MTLBuffer> staging =
        [m_device newBufferWithBytes:pixels
                              length:bytes
                             options:MTLResourceStorageModeShared];

      id<MTLCommandBuffer> cmd = [m_queue commandBuffer];
      id<MTLBlitCommandEncoder> blit = [cmd blitCommandEncoder];
      [blit copyFromBuffer:staging
               sourceOffset:0
          sourceBytesPerRow:(NSUInteger)w * bpp
        sourceBytesPerImage:bytes
                 sourceSize:MTLSizeMake (w, h, 1)
                  toTexture:tex
           destinationSlice:0
           destinationLevel:0
          destinationOrigin:MTLOriginMake (0, 0, 0)];
      if (gen_mips)
        [blit generateMipmapsForTexture:tex];
      [blit endEncoding];
      [cmd commit];
      [cmd waitUntilCompleted];
    }

    TexturePtr MetalRenderer::create_texture (const TextureDesc& desc,
                                              const void* pixels) {
      // RGB8 is expanded to RGBA8 on upload.
      std::vector<uint8_t> expanded;
      const void* data = pixels;
      if (desc.format == TextureFormat::RGB8) {
        const uint8_t* src = (const uint8_t*)pixels;
        expanded.resize ((size_t)desc.width * desc.height * 4);
        for (size_t i = 0; i < (size_t)desc.width * desc.height; ++i) {
          expanded[i * 4 + 0] = src[i * 3 + 0];
          expanded[i * 4 + 1] = src[i * 3 + 1];
          expanded[i * 4 + 2] = src[i * 3 + 2];
          expanded[i * 4 + 3] = 255;
        }
        data = expanded.data ();
      }

      const bool mips = desc.filter == TextureFilter::Mipmap;
      // sRGB view: the hardware decodes texels to linear at sample
      // time (and encodes on mip generation), so albedo textures light
      // correctly in the linear scene.  White + coverage textures
      // (glyphs, particle discs) are unaffected.
      MTLTextureDescriptor* td = [MTLTextureDescriptor
        texture2DDescriptorWithPixelFormat:MTLPixelFormatRGBA8Unorm_sRGB
                                     width:desc.width
                                    height:desc.height
                                 mipmapped:mips];
      td.storageMode = MTLStorageModePrivate;
      td.usage = MTLTextureUsageShaderRead;

      MetalTexture* t = new MetalTexture ();
      t->width = desc.width;
      t->height = desc.height;
      t->texture = [m_device newTextureWithDescriptor:td];

      MTLSamplerDescriptor* sd = [[MTLSamplerDescriptor alloc] init];
      sd.minFilter = desc.filter == TextureFilter::Nearest
                       ? MTLSamplerMinMagFilterNearest
                       : MTLSamplerMinMagFilterLinear;
      sd.magFilter = sd.minFilter;
      sd.mipFilter =
        mips ? MTLSamplerMipFilterLinear : MTLSamplerMipFilterNotMipmapped;
      sd.sAddressMode = desc.wrap == TextureWrap::Repeat
                          ? MTLSamplerAddressModeRepeat
                          : MTLSamplerAddressModeClampToEdge;
      sd.tAddressMode = sd.sAddressMode;
      sd.maxAnisotropy =
        (NSUInteger)(desc.max_anisotropy < 1 ? 1 : desc.max_anisotropy);
      t->sampler = [m_device newSamplerStateWithDescriptor:sd];

      upload_texture (t->texture, data, desc.width, desc.height, 4, mips);
      return TexturePtr (t);
    }

    MeshPtr MetalRenderer::create_mesh (const DrawList& recorded) {
      MOPPE_PROFILE_ZONE ("MetalRenderer::create_mesh");
      MetalMesh* m = new MetalMesh ();
      m->runs.assign (recorded.runs ().begin (), recorded.runs ().end ());
      if (!recorded.vertices ().empty ())
        m->vertices = [m_device
          newBufferWithBytes:recorded.vertices ().data ()
                      length:recorded.vertices ().size () * sizeof (Vertex)
                     options:MTLResourceStorageModeShared];
      return MeshPtr (m);
    }

    // -- world setup ---------------------------------------------------

    void MetalRenderer::set_terrain (const TerrainParams& params,
                                     const float* heights,
                                     const Vec3* normals) {
      MOPPE_PROFILE_ZONE ("MetalRenderer::set_terrain");
      const int w = params.width, h = params.height;
      const bool transition =
        params.derive_normals && m_terrain_resources.have_terrain &&
        m_terrain_resources.heights &&
        m_terrain_resources.heights.width == (NSUInteger)w &&
        m_terrain_resources.heights.height == (NSUInteger)h;
      if (m_terrain_resources.height_transition_active) {
        const float elapsed = std::max (
          0.0f,
          m_frame.params.time - m_terrain_resources.height_transition_start);
        const float duration = std::max (
          0.001f, m_terrain_resources.params.height_transition_duration);
        if (elapsed >= duration)
          m_terrain_resources.height_transition_active = false;
      }
      const bool continue_transition =
        transition && m_terrain_resources.height_transition_active &&
        m_terrain_resources.previous_heights;
      if (transition && !continue_transition)
        ++m_terrain_resources.height_transition_generation;

      if (transition && !continue_transition) {
        id<MTLTexture> old_heights = m_terrain_resources.heights;
        m_terrain_resources.heights = m_terrain_resources.previous_heights;
        m_terrain_resources.previous_heights = old_heights;
      } else if (!transition) {
        m_terrain_resources.previous_heights = nil;
      }
      m_terrain_resources.params = params;

      // Heights: R32Float, read() access only.
      if (!m_terrain_resources.heights ||
          m_terrain_resources.heights.width != (NSUInteger)w ||
          m_terrain_resources.heights.height != (NSUInteger)h) {
        MTLTextureDescriptor* td = [MTLTextureDescriptor
          texture2DDescriptorWithPixelFormat:MTLPixelFormatR32Float
                                       width:w
                                      height:h
                                   mipmapped:NO];
        td.storageMode = MTLStorageModePrivate;
        td.usage = MTLTextureUsageShaderRead;
        m_terrain_resources.heights = [m_device newTextureWithDescriptor:td];
      }
      upload_texture (m_terrain_resources.heights, heights, w, h, 4, false);
      if (transition && !continue_transition) {
        m_terrain_resources.height_transition_start = m_frame.params.time;
        m_terrain_resources.height_transition_active = true;
      } else if (!transition) {
        m_terrain_resources.height_transition_active = false;
      }

      // Normals: RG16Snorm xz, y reconstructed in the shader.
      if (!params.derive_normals) {
        std::vector<int16_t> packed ((size_t)w * h * 2);
        for (size_t i = 0; i < (size_t)w * h; ++i) {
          const Vec3& n = normals[i];
          float x = n[0], z = n[2];
          if (x < -1)
            x = -1;
          if (x > 1)
            x = 1;
          if (z < -1)
            z = -1;
          if (z > 1)
            z = 1;
          packed[i * 2 + 0] = (int16_t)(x * 32767.0f);
          packed[i * 2 + 1] = (int16_t)(z * 32767.0f);
        }
        if (!m_terrain_resources.normals ||
            m_terrain_resources.normals.width != (NSUInteger)w ||
            m_terrain_resources.normals.height != (NSUInteger)h) {
          MTLTextureDescriptor* td = [MTLTextureDescriptor
            texture2DDescriptorWithPixelFormat:MTLPixelFormatRG16Snorm
                                         width:w
                                        height:h
                                     mipmapped:NO];
          td.storageMode = MTLStorageModePrivate;
          td.usage = MTLTextureUsageShaderRead;
          m_terrain_resources.normals = [m_device newTextureWithDescriptor:td];
        }
        upload_texture (
          m_terrain_resources.normals, packed.data (), w, h, 4, false);
      } else if (!m_terrain_resources.normals) {
        const int16_t up[2] = { 0, 0 };
        MTLTextureDescriptor* td = [MTLTextureDescriptor
          texture2DDescriptorWithPixelFormat:MTLPixelFormatRG16Snorm
                                       width:1
                                      height:1
                                   mipmapped:NO];
        td.storageMode = MTLStorageModePrivate;
        td.usage = MTLTextureUsageShaderRead;
        m_terrain_resources.normals = [m_device newTextureWithDescriptor:td];
        upload_texture (m_terrain_resources.normals, up, 1, 1, 4, false);
      }

      // Shared chunk-local index templates.  The finest inserts one
      // virtual vertex between source samples; progressively coarser
      // levels use source strides 1, 2, 4, and 8.
      struct Gen {
        static id<MTLBuffer>
        make (id<MTLDevice> dev, int vpr, uint32_t& count_out) {
          std::vector<uint32_t> idx;
          const int rows = vpr - 1;
          idx.reserve ((size_t)rows * (vpr * 2 + 1));
          for (int r = 0; r < rows; ++r) {
            for (int x = 0; x < vpr; ++x) {
              idx.push_back ((uint32_t)(r * vpr + x));
              idx.push_back ((uint32_t)((r + 1) * vpr + x));
            }
            idx.push_back (0xFFFFFFFFu); // strip restart
          }
          count_out = (uint32_t)idx.size ();
          return [dev newBufferWithBytes:idx.data ()
                                  length:idx.size () * 4
                                 options:MTLResourceStorageModeShared];
        }
      };
      for (int lod = 0; lod < TERRAIN_LOD_COUNT; ++lod)
        if (!m_terrain_resources.indices[lod])
          m_terrain_resources.indices[lod] =
            Gen::make (m_device,
                       TERRAIN_LOD_VERTS[lod],
                       m_terrain_resources.index_count[lod]);

      m_terrain_resources.have_terrain = true;
    }

    void MetalRenderer::set_terrain_topology_overlay (bool enabled) {
      m_terrain_resources.params.topology_overlay = enabled;
    }

    void MetalRenderer::set_terrain_textures (TexturePtr grass,
                                              TexturePtr dirt,
                                              TexturePtr rock,
                                              TexturePtr snow) {
      m_terrain_resources.grass = grass;
      m_terrain_resources.dirt = dirt;
      m_terrain_resources.rock = rock;
      m_terrain_resources.snow = snow;
    }

    void MetalRenderer::set_terrain_overlay (const TerrainOverlayParams& params,
                                             std::span<const float> values) {
      if (params.width < 1 || params.height < 1 ||
          values.size () !=
            static_cast<std::size_t> (params.width) * params.height)
        throw std::invalid_argument ("invalid terrain overlay raster");
      if (!m_terrain_resources.overlay ||
          m_terrain_resources.overlay.width != (NSUInteger)params.width ||
          m_terrain_resources.overlay.height != (NSUInteger)params.height) {
        MTLTextureDescriptor* td = [MTLTextureDescriptor
          texture2DDescriptorWithPixelFormat:MTLPixelFormatR32Float
                                       width:params.width
                                      height:params.height
                                   mipmapped:NO];
        td.storageMode = MTLStorageModePrivate;
        td.usage = MTLTextureUsageShaderRead;
        m_terrain_resources.overlay = [m_device newTextureWithDescriptor:td];
      }
      upload_texture (m_terrain_resources.overlay,
                      values.data (),
                      params.width,
                      params.height,
                      4,
                      false);
      m_terrain_resources.overlay_params = params;
      m_terrain_resources.have_overlay = true;
    }

    void MetalRenderer::clear_terrain_overlay () {
      m_terrain_resources.have_overlay = false;
    }

    void MetalRenderer::render_terrain_shadow (const Mat4& light_view_proj) {
      MOPPE_PROFILE_ZONE ("MetalRenderer::render_terrain_shadow");
      if (!m_pipelines.terrain_shadow || !m_terrain_resources.have_terrain)
        return;

      const int shadow_size =
        std::max (256, m_terrain_resources.params.shadow_resolution);
      const bool new_shadow_transition =
        m_terrain_resources.params.derive_normals &&
        m_terrain_resources.shadow_transition_generation !=
          m_terrain_resources.height_transition_generation;
      if (new_shadow_transition && m_terrain_resources.shadow_map) {
        id<MTLTexture> old_shadow = m_terrain_resources.shadow_map;
        m_terrain_resources.shadow_map =
          m_terrain_resources.previous_shadow_map;
        m_terrain_resources.previous_shadow_map = old_shadow;
      } else if (!m_terrain_resources.params.derive_normals) {
        m_terrain_resources.previous_shadow_map = nil;
      }

      if (!m_terrain_resources.shadow_map ||
          m_terrain_resources.shadow_map.width != (NSUInteger)shadow_size ||
          m_terrain_resources.shadow_map.height != (NSUInteger)shadow_size) {
        MTLTextureDescriptor* td = [MTLTextureDescriptor
          texture2DDescriptorWithPixelFormat:MTLPixelFormatDepth16Unorm
                                       width:shadow_size
                                      height:shadow_size
                                   mipmapped:NO];
        td.storageMode = MTLStorageModePrivate;
        td.usage = MTLTextureUsageRenderTarget | MTLTextureUsageShaderRead;
        m_terrain_resources.shadow_map = [m_device newTextureWithDescriptor:td];
      }

      // Bias: xy from NDC [-1,1] to uv [0,1], with the Metal
      // texture-space Y flip; z is already [0,1].
      Mat4 bias;
      bias.m[0] = 0.5f;
      bias.m[5] = -0.5f;
      bias.m[12] = 0.5f;
      bias.m[13] = 0.5f;
      m_terrain_resources.light_biased = bias * light_view_proj;

      MTLRenderPassDescriptor* rp =
        [MTLRenderPassDescriptor renderPassDescriptor];
      rp.depthAttachment.texture = m_terrain_resources.shadow_map;
      rp.depthAttachment.loadAction = MTLLoadActionClear;
      rp.depthAttachment.storeAction = MTLStoreActionStore;
      rp.depthAttachment.clearDepth = 1.0;

      id<MTLCommandBuffer> cmd = [m_queue commandBuffer];
      // This setup pass runs on the world-generation thread. Tracy 0.13.1's
      // Metal server crashes when a context first receives a GPU zone from
      // that worker and later receives frame zones from the render thread.
      // Keep its existing command-buffer timing until upstream supports this
      // cross-thread pattern reliably.
      MOPPE_METAL_ZONE_DISABLED (m_frame.tracy_metal, rp, "Terrain shadow");
      id<MTLRenderCommandEncoder> enc =
        [cmd renderCommandEncoderWithDescriptor:rp];
      [enc setRenderPipelineState:m_pipelines.terrain_shadow];
      [enc setDepthStencilState:m_pipelines.shadow_depth];
      [enc setCullMode:MTLCullModeNone];
      [enc setFrontFacingWinding:MTLWindingCounterClockwise];
      // Port of glPolygonOffset(2,2) in the GL shadow pass.
      [enc setDepthBias:2.0f slopeScale:2.0f clamp:0.0f];

      MoppeTerrainUniforms u;
      std::memset (&u, 0, sizeof (u));
      u.view_proj = m4 (light_view_proj);
      u.params0.x = m_terrain_resources.params.scale[0];
      u.params0.y = m_terrain_resources.params.scale[1];
      u.params0.z = m_terrain_resources.params.scale[2];
      [enc setVertexBytes:&u length:sizeof (u) atIndex:MOPPE_BUF_FRAME];
      [enc setVertexTexture:m_terrain_resources.heights
                    atIndex:MOPPE_TEX_HEIGHTS];

      const int requested_step =
        std::max (1, m_terrain_resources.params.shadow_sample_step);
      int shadow_lod = TERRAIN_NATIVE_LOD;
      while (shadow_lod + 1 < TERRAIN_LOD_COUNT &&
             TERRAIN_LOD_STEP[shadow_lod] < requested_step)
        ++shadow_lod;
      const float shadow_step = TERRAIN_LOD_STEP[shadow_lod];
      const int chunks = (m_terrain_resources.params.width - 1) / CHUNK_CELLS;
      for (int cz = 0; cz < chunks; ++cz)
        for (int cx = 0; cx < chunks; ++cx) {
          MoppeChunkUniforms c;
          std::memset (&c, 0, sizeof (c));
          c.origin_x = cx * CHUNK_CELLS;
          c.origin_z = cz * CHUNK_CELLS;
          c.step = shadow_step;
          c.verts_per_row = TERRAIN_LOD_VERTS[shadow_lod];
          c.parent_step = shadow_step;
          [enc setVertexBytes:&c length:sizeof (c) atIndex:MOPPE_BUF_CHUNK];
          [enc drawIndexedPrimitives:MTLPrimitiveTypeTriangleStrip
                          indexCount:m_terrain_resources.index_count[shadow_lod]
                           indexType:MTLIndexTypeUInt32
                         indexBuffer:m_terrain_resources.indices[shadow_lod]
                   indexBufferOffset:0];
        }
      [enc endEncoding];
      [cmd commit];
      [cmd waitUntilCompleted];
#if defined(TRACY_ENABLE) && !TARGET_OS_IPHONE
      TracyMetalCollect (m_frame.tracy_metal);
#endif
      if (::getenv ("MOPPE_PROFILE_SHADOW")) {
        const double gpu_ms = 1000.0 * (cmd.GPUEndTime - cmd.GPUStartTime);
        std::cerr << "terrain shadow: " << shadow_size << "px, step "
                  << shadow_step << ", " << gpu_ms << " ms GPU\n";
      }
      m_terrain_resources.have_shadow = true;
      m_terrain_resources.shadow_transition_generation =
        m_terrain_resources.height_transition_generation;
    }

    void MetalRenderer::set_ocean (const OceanSetup& setup,
                                   std::span<const float> water_levels) {
      MOPPE_PROFILE_ZONE ("MetalRenderer::set_ocean");
      // Regular triangle water grid. The vertex shader samples the
      // optional standing-water surface and does the waving. Built as plain
      // triangles to keep sea and lakes in one draw.
      const int cells = setup.cells;
      const float step = 2 * setup.half_extent / cells;
      const float x0 = setup.center[0] - setup.half_extent;
      const float z0 = setup.center[2] - setup.half_extent;

      std::vector<float> verts;
      verts.reserve ((size_t)cells * cells * 6 * 3);
      for (int j = 0; j < cells; ++j)
        for (int i = 0; i < cells; ++i) {
          const float xa = x0 + i * step, xb = xa + step;
          const float za = z0 + j * step, zb = za + step;
          const float y = setup.level;
          const float quad[6][3] = {
            { xa, y, za }, { xa, y, zb }, { xb, y, za },
            { xb, y, za }, { xa, y, zb }, { xb, y, zb },
          };
          for (int k = 0; k < 6; ++k) {
            verts.push_back (quad[k][0]);
            verts.push_back (quad[k][1]);
            verts.push_back (quad[k][2]);
          }
        }

      m_water_resources.ocean_level = setup.level;
      m_water_resources.ocean_vcount = (uint32_t)(verts.size () / 3);
      m_water_resources.ocean_verts =
        [m_device newBufferWithBytes:verts.data ()
                              length:verts.size () * sizeof (float)
                             options:MTLResourceStorageModeShared];

      // Interleaved (level, wave amplitude) pairs per terrain sample.
      const std::size_t expected =
        2 * static_cast<std::size_t> (m_terrain_resources.params.width) *
        m_terrain_resources.params.height;
      m_water_resources.have_water_levels =
        expected != 0 && water_levels.size () == expected;
      if (m_water_resources.have_water_levels) {
        const int width = m_terrain_resources.params.width;
        const int height = m_terrain_resources.params.height;
        if (!m_water_resources.water_levels ||
            m_water_resources.water_levels.width !=
              static_cast<NSUInteger> (width) ||
            m_water_resources.water_levels.height !=
              static_cast<NSUInteger> (height)) {
          MTLTextureDescriptor* td = [MTLTextureDescriptor
            texture2DDescriptorWithPixelFormat:MTLPixelFormatRG32Float
                                         width:width
                                        height:height
                                     mipmapped:NO];
          td.storageMode = MTLStorageModePrivate;
          td.usage = MTLTextureUsageShaderRead;
          m_water_resources.water_levels =
            [m_device newTextureWithDescriptor:td];
        }
        upload_texture (m_water_resources.water_levels,
                        water_levels.data (),
                        width,
                        height,
                        8,
                        false);
      }
    }

    void MetalRenderer::set_water_flow (std::span<const float> flow) {
      MOPPE_PROFILE_ZONE ("MetalRenderer::set_water_flow");
      // Interleaved (x, z) meters per second per terrain sample; half
      // precision carries river currents with headroom to spare.
      const std::size_t expected =
        2 * static_cast<std::size_t> (m_terrain_resources.params.width) *
        m_terrain_resources.params.height;
      m_water_resources.have_water_flow =
        expected != 0 && flow.size () == expected;
      if (!m_water_resources.have_water_flow)
        return;
      const int width = m_terrain_resources.params.width;
      const int height = m_terrain_resources.params.height;
      if (!m_water_resources.water_flow ||
          m_water_resources.water_flow.width !=
            static_cast<NSUInteger> (width) ||
          m_water_resources.water_flow.height !=
            static_cast<NSUInteger> (height)) {
        MTLTextureDescriptor* td = [MTLTextureDescriptor
          texture2DDescriptorWithPixelFormat:MTLPixelFormatRG16Float
                                       width:width
                                      height:height
                                   mipmapped:NO];
        td.storageMode = MTLStorageModePrivate;
        td.usage = MTLTextureUsageShaderRead;
        m_water_resources.water_flow = [m_device newTextureWithDescriptor:td];
      }
      std::vector<__fp16> halves (flow.size ());
      for (std::size_t i = 0; i < flow.size (); ++i)
        halves[i] = static_cast<__fp16> (flow[i]);
      upload_texture (
        m_water_resources.water_flow, halves.data (), width, height, 4, false);
    }

    void MetalRenderer::set_terrain_geology (std::span<const float> geology) {
      MOPPE_PROFILE_ZONE ("MetalRenderer::set_terrain_geology");
      const std::size_t expected =
        2 * static_cast<std::size_t> (m_terrain_resources.params.width) *
        m_terrain_resources.params.height;
      m_terrain_resources.have_geology =
        expected != 0 && geology.size () == expected;
      if (!m_terrain_resources.have_geology)
        return;
      const int width = m_terrain_resources.params.width;
      const int height = m_terrain_resources.params.height;
      if (!m_terrain_resources.geology ||
          m_terrain_resources.geology.width !=
            static_cast<NSUInteger> (width) ||
          m_terrain_resources.geology.height !=
            static_cast<NSUInteger> (height)) {
        MTLTextureDescriptor* td = [MTLTextureDescriptor
          texture2DDescriptorWithPixelFormat:MTLPixelFormatRG16Float
                                       width:width
                                      height:height
                                   mipmapped:NO];
        td.storageMode = MTLStorageModePrivate;
        td.usage = MTLTextureUsageShaderRead;
        m_terrain_resources.geology = [m_device newTextureWithDescriptor:td];
      }
      std::vector<__fp16> halves (geology.size ());
      for (std::size_t i = 0; i < geology.size (); ++i)
        halves[i] = static_cast<__fp16> (geology[i]);
      upload_texture (
        m_terrain_resources.geology, halves.data (), width, height, 4, false);
    }

    void MetalRenderer::set_terrain_shore (std::span<const float> distance) {
      const std::size_t expected =
        static_cast<std::size_t> (m_terrain_resources.params.width) *
        m_terrain_resources.params.height;
      m_terrain_resources.have_shore = distance.size () == expected;
      if (!m_terrain_resources.have_shore)
        return;
      const int width = m_terrain_resources.params.width;
      const int height = m_terrain_resources.params.height;
      if (!m_terrain_resources.shore ||
          m_terrain_resources.shore.width != static_cast<NSUInteger> (width) ||
          m_terrain_resources.shore.height !=
            static_cast<NSUInteger> (height)) {
        MTLTextureDescriptor* td = [MTLTextureDescriptor
          texture2DDescriptorWithPixelFormat:MTLPixelFormatR16Float
                                       width:width
                                      height:height
                                   mipmapped:NO];
        td.storageMode = MTLStorageModePrivate;
        td.usage = MTLTextureUsageShaderRead;
        m_terrain_resources.shore = [m_device newTextureWithDescriptor:td];
      }
      std::vector<__fp16> halves (distance.size ());
      for (std::size_t i = 0; i < distance.size (); ++i)
        halves[i] = static_cast<__fp16> (distance[i]);
      upload_texture (
        m_terrain_resources.shore, halves.data (), width, height, 2, false);
    }

    void MetalRenderer::set_terrain_paths (
      std::span<const float> influence,
      std::span<const float> home_base_influence) {
      const std::size_t expected =
        static_cast<std::size_t> (m_terrain_resources.params.width) *
        m_terrain_resources.params.height;
      m_terrain_resources.have_paths =
        expected != 0 && influence.size () == expected;
      if (!m_terrain_resources.have_paths)
        return;
      const int width = m_terrain_resources.params.width;
      const int height = m_terrain_resources.params.height;
      if (!home_base_influence.empty () &&
          home_base_influence.size () != expected)
        throw std::invalid_argument (
          "home base influence does not match terrain");
      if (!m_terrain_resources.paths ||
          m_terrain_resources.paths.width != static_cast<NSUInteger> (width) ||
          m_terrain_resources.paths.height !=
            static_cast<NSUInteger> (height)) {
        MTLTextureDescriptor* td = [MTLTextureDescriptor
          texture2DDescriptorWithPixelFormat:MTLPixelFormatRG16Float
                                       width:width
                                      height:height
                                   mipmapped:NO];
        td.storageMode = MTLStorageModePrivate;
        td.usage = MTLTextureUsageShaderRead;
        m_terrain_resources.paths = [m_device newTextureWithDescriptor:td];
      }
      std::vector<__fp16> halves (2 * influence.size ());
      for (std::size_t i = 0; i < influence.size (); ++i) {
        halves[2 * i] = static_cast<__fp16> (influence[i]);
        halves[2 * i + 1] = static_cast<__fp16> (
          home_base_influence.empty () ? 0.0f : home_base_influence[i]);
      }
      upload_texture (
        m_terrain_resources.paths, halves.data (), width, height, 4, false);
    }

    void MetalRenderer::set_terrain_moisture (std::span<const float> moisture) {
      MOPPE_PROFILE_ZONE ("MetalRenderer::set_terrain_moisture");
      const std::size_t expected =
        static_cast<std::size_t> (m_terrain_resources.params.width) *
        m_terrain_resources.params.height;
      m_terrain_resources.have_moisture =
        expected != 0 && moisture.size () == expected;
      if (!m_terrain_resources.have_moisture)
        return;
      const int width = m_terrain_resources.params.width;
      const int height = m_terrain_resources.params.height;
      if (!m_terrain_resources.moisture ||
          m_terrain_resources.moisture.width !=
            static_cast<NSUInteger> (width) ||
          m_terrain_resources.moisture.height !=
            static_cast<NSUInteger> (height)) {
        MTLTextureDescriptor* td = [MTLTextureDescriptor
          texture2DDescriptorWithPixelFormat:MTLPixelFormatR32Float
                                       width:width
                                      height:height
                                   mipmapped:NO];
        td.storageMode = MTLStorageModePrivate;
        td.usage = MTLTextureUsageShaderRead;
        m_terrain_resources.moisture = [m_device newTextureWithDescriptor:td];
      }
      upload_texture (m_terrain_resources.moisture,
                      moisture.data (),
                      width,
                      height,
                      4,
                      false);
    }

    void MetalRenderer::set_terrain_forest (std::span<const float> cover) {
      MOPPE_PROFILE_ZONE ("MetalRenderer::set_terrain_forest");
      const std::size_t expected =
        static_cast<std::size_t> (m_terrain_resources.params.width) *
        m_terrain_resources.params.height;
      m_terrain_resources.have_forest =
        expected != 0 && cover.size () == expected;
      if (!m_terrain_resources.have_forest)
        return;
      const int width = m_terrain_resources.params.width;
      const int height = m_terrain_resources.params.height;
      if (!m_terrain_resources.forest ||
          m_terrain_resources.forest.width != static_cast<NSUInteger> (width) ||
          m_terrain_resources.forest.height !=
            static_cast<NSUInteger> (height)) {
        MTLTextureDescriptor* td = [MTLTextureDescriptor
          texture2DDescriptorWithPixelFormat:MTLPixelFormatR32Float
                                       width:width
                                      height:height
                                   mipmapped:NO];
        td.storageMode = MTLStorageModePrivate;
        td.usage = MTLTextureUsageShaderRead;
        m_terrain_resources.forest = [m_device newTextureWithDescriptor:td];
      }
      upload_texture (
        m_terrain_resources.forest, cover.data (), width, height, 4, false);
    }

    void
    MetalRenderer::set_terrain_snow_support (std::span<const float> support) {
      MOPPE_PROFILE_ZONE ("MetalRenderer::set_terrain_snow_support");
      const std::size_t expected =
        static_cast<std::size_t> (m_terrain_resources.params.width) *
        m_terrain_resources.params.height;
      m_terrain_resources.have_snow_support =
        expected != 0 && support.size () == expected;
      if (!m_terrain_resources.have_snow_support)
        return;
      const int width = m_terrain_resources.params.width;
      const int height = m_terrain_resources.params.height;
      if (!m_terrain_resources.snow_support ||
          m_terrain_resources.snow_support.width !=
            static_cast<NSUInteger> (width) ||
          m_terrain_resources.snow_support.height !=
            static_cast<NSUInteger> (height)) {
        MTLTextureDescriptor* td = [MTLTextureDescriptor
          texture2DDescriptorWithPixelFormat:MTLPixelFormatR16Float
                                       width:width
                                      height:height
                                   mipmapped:NO];
        td.storageMode = MTLStorageModePrivate;
        td.usage = MTLTextureUsageShaderRead;
        m_terrain_resources.snow_support =
          [m_device newTextureWithDescriptor:td];
      }
      std::vector<__fp16> halves (support.size ());
      for (std::size_t i = 0; i < support.size (); ++i)
        halves[i] = static_cast<__fp16> (support[i]);
      upload_texture (m_terrain_resources.snow_support,
                      halves.data (),
                      width,
                      height,
                      sizeof (__fp16),
                      false);
    }

    void MetalRenderer::set_terrain_channel_flux (std::span<const float> flux) {
      MOPPE_PROFILE_ZONE ("MetalRenderer::set_terrain_channel_flux");
      // Interleaved (x, z) drainage flux per terrain sample; direction times
      // a [0,1] activity fits comfortably in half precision.
      const std::size_t expected =
        2 * static_cast<std::size_t> (m_terrain_resources.params.width) *
        m_terrain_resources.params.height;
      m_terrain_resources.have_channel_flux =
        expected != 0 && flux.size () == expected;
      if (!m_terrain_resources.have_channel_flux)
        return;
      const int width = m_terrain_resources.params.width;
      const int height = m_terrain_resources.params.height;
      if (!m_terrain_resources.channel_flux ||
          m_terrain_resources.channel_flux.width !=
            static_cast<NSUInteger> (width) ||
          m_terrain_resources.channel_flux.height !=
            static_cast<NSUInteger> (height)) {
        MTLTextureDescriptor* td = [MTLTextureDescriptor
          texture2DDescriptorWithPixelFormat:MTLPixelFormatRG16Float
                                       width:width
                                      height:height
                                   mipmapped:NO];
        td.storageMode = MTLStorageModePrivate;
        td.usage = MTLTextureUsageShaderRead;
        m_terrain_resources.channel_flux =
          [m_device newTextureWithDescriptor:td];
      }
      std::vector<__fp16> halves (flux.size ());
      for (std::size_t i = 0; i < flux.size (); ++i)
        halves[i] = static_cast<__fp16> (flux[i]);
      upload_texture (m_terrain_resources.channel_flux,
                      halves.data (),
                      width,
                      height,
                      4,
                      false);
    }

    // -- targets -------------------------------------------------------

    id<MTLTexture> MetalRenderer::make_target (
      MTLPixelFormat fmt, int w, int h, int samples, bool memoryless) {
      MTLTextureDescriptor* td =
        [MTLTextureDescriptor texture2DDescriptorWithPixelFormat:fmt
                                                           width:w
                                                          height:h
                                                       mipmapped:NO];
      td.textureType =
        samples > 1 ? MTLTextureType2DMultisample : MTLTextureType2D;
      td.sampleCount = samples;
      td.usage = MTLTextureUsageRenderTarget |
                 (samples > 1 ? 0 : MTLTextureUsageShaderRead);
      td.storageMode = (memoryless && m_memoryless_ok)
                         ? MTLStorageModeMemoryless
                         : MTLStorageModePrivate;
      return [m_device newTextureWithDescriptor:td];
    }

    void MetalRenderer::ensure_targets (float requested_scale,
                                        float scale_override) {
      const int drawable_w = (int)m_view.drawableSize.width;
      const int drawable_h = (int)m_view.drawableSize.height;
      if (drawable_w == 0 || drawable_h == 0)
        return;
      const CGSize points = m_view.bounds.size;
      const float backing_scale =
        points.width > 0 ? drawable_w / (float)points.width : 1.0f;
      const float scale =
        scene_render_scale (backing_scale, requested_scale, scale_override);
      const int w = std::max (1, (int)std::round (drawable_w * scale));
      const int h = std::max (1, (int)std::round (drawable_h * scale));
      if (w == m_targets.width && h == m_targets.height && m_targets.msaa_color)
        return;

      m_targets.width = w;
      m_targets.height = h;
      m_targets.msaa_color =
        make_target (MTLPixelFormatRGBA16Float, w, h, MSAA_SAMPLES, true);
      m_targets.msaa_depth = make_target (
        MTLPixelFormatDepth32Float_Stencil8, w, h, MSAA_SAMPLES, true);
      m_targets.scene_a =
        make_target (MTLPixelFormatRGBA16Float, w, h, 1, false);
      m_targets.scene_b =
        make_target (MTLPixelFormatRGBA16Float, w, h, 1, false);
      m_targets.prev_frame =
        make_target (MTLPixelFormatRGBA16Float, w, h, 1, false);
      const int bw = w / 4 > 0 ? w / 4 : 1;
      const int bh = h / 4 > 0 ? h / 4 : 1;
      m_targets.bloom_a =
        make_target (MTLPixelFormatRGBA16Float, bw, bh, 1, false);
      m_targets.bloom_b =
        make_target (MTLPixelFormatRGBA16Float, bw, bh, 1, false);
      std::cerr << "moppe: render targets: drawable=" << drawable_w << 'x'
                << drawable_h << ", scene=" << w << 'x' << h
                << ", render-scale=" << scale << ", msaa=" << MSAA_SAMPLES
                << 'x' << std::endl;
      if (!m_targets.probe_tex) {
        m_targets.probe_tex =
          make_target (MTLPixelFormatRGBA32Float, PROBE_W, PROBE_H, 1, false);
        for (int i = 0; i < FRAMES_IN_FLIGHT; ++i)
          m_targets.probe_buf[i] =
            [m_device newBufferWithLength:PROBE_W * PROBE_H * 16
                                  options:MTLResourceStorageModeShared];
      }
      // Freshly created: undefined contents until the first blur blit.
      m_targets.prev_valid = false;
    }

    // Log-average the last completed probe and ease the exposure
    // toward mid-gray; clamped to about a stop either way so night
    // stays night.  Adapting down (a blinding scene) is faster than
    // adapting up, like eyes.
    void MetalRenderer::update_exposure () {
      id<MTLBuffer> buf = m_targets.probe_buf[m_frame.slot];
      if (!buf)
        return;
      const float* px = (const float*)buf.contents;
      double sum = 0;
      for (int i = 0; i < PROBE_W * PROBE_H; ++i) {
        const float l = 0.2126f * px[i * 4] + 0.7152f * px[i * 4 + 1] +
                        0.0722f * px[i * 4 + 2];
        sum += std::log2 (std::max (l, 1e-4f));
      }
      const float avg = (float)std::exp2 (sum / (PROBE_W * PROBE_H));
      if (avg <= 1.5e-4f)
        return; // probe not written yet (startup frames)

      float target = 0.16f / avg;
      target = std::min (1.9f, std::max (0.55f, target));
      const float rate = target < m_targets.exposure ? 0.10f : 0.04f;
      m_targets.exposure += (target - m_targets.exposure) * rate;
    }

    // -- frame ---------------------------------------------------------

    bool MetalRenderer::begin_frame (const FrameParams& params) {
      MOPPE_PROFILE_ZONE ("MetalRenderer::begin_frame");
      const double frame_start = cpu_time ();
      {
        MOPPE_PROFILE_ZONE ("MetalRenderer::ensure_targets");
        ensure_targets (params.scene_scale, params.render_scale_override);
      }
      const double targets_done = cpu_time ();
      if (!m_targets.msaa_color)
        return false;

      {
        MOPPE_PROFILE_ZONE ("MetalRenderer::wait_for_inflight_frame");
        dispatch_semaphore_wait (m_frame.inflight, DISPATCH_TIME_FOREVER);
      }
#if defined(TRACY_ENABLE) && !TARGET_OS_IPHONE
      TracyMetalCollect (m_frame.tracy_metal);
#endif
      const double inflight_done = cpu_time ();

      {
        MOPPE_PROFILE_ZONE ("MetalRenderer::acquire_drawable");
        if (m_frame.pending_drawable) {
          m_frame.drawable = m_frame.pending_drawable;
          m_frame.pending_drawable = nil;
        } else {
          m_frame.drawable = m_view.currentDrawable;
        }
      }
      const double drawable_done = cpu_time ();
      if (!m_frame.drawable) {
        dispatch_semaphore_signal (m_frame.inflight);
        return false;
      }
      if (m_profile_cpu) {
        m_cpu_frame_start = frame_start;
        m_cpu_encode_start = drawable_done;
        m_cpu_targets_total += targets_done - frame_start;
        m_cpu_inflight_total += inflight_done - targets_done;
        m_cpu_drawable_total += drawable_done - inflight_done;
      }

      m_frame.slot = (m_frame.slot + 1) % FRAMES_IN_FLIGHT;
      m_frame.stream_used = 0;
      m_frame.profile_this_frame = params.profile;
      m_frame.timestamp_count = 0;
      m_frame.sample_passes.clear ();
      m_frame.current_gpu_pass = GpuPass::Count;

#if !TARGET_OS_IPHONE
      if (params.profile && !m_frame.capture_path.empty () &&
          !m_frame.capture_active && m_frame.capture_frames == 0) {
        NSString* path =
          [NSString stringWithUTF8String:m_frame.capture_path.c_str ()];
        MTLCaptureDescriptor* descriptor = [[MTLCaptureDescriptor alloc] init];
        descriptor.captureObject = m_queue;
        descriptor.destination = MTLCaptureDestinationGPUTraceDocument;
        descriptor.outputURL = [NSURL fileURLWithPath:path];
        NSError* error = nil;
        m_frame.capture_active = [[MTLCaptureManager sharedCaptureManager]
          startCaptureWithDescriptor:descriptor
                               error:&error];
        if (m_frame.capture_active)
          std::cerr << "moppe: capturing " << m_frame.capture_frame_limit
                    << " gameplay frames to " << m_frame.capture_path
                    << std::endl;
        else {
          ++m_frame.capture_frames; // Do not retry and spam every frame.
          std::cerr << "moppe: failed to start Metal capture: "
                    << error.localizedDescription.UTF8String << std::endl;
        }
      }
#endif

      const CGSize points = m_view.bounds.size;
      m_frame.scale = points.width > 0
                        ? (float)m_view.drawableSize.width / (float)points.width
                        : 1.0f;
#if !TARGET_OS_IPHONE
      if (m_view.window) {
        NSScreen* screen = m_view.window.screen;
        if (screen)
          m_frame.edr_headroom = std::max (
            1.0f, (float)screen.maximumExtendedDynamicRangeColorComponentValue);
      }
#endif
      m_frame.width_pts = (int)points.width;
      m_frame.height_pts = (int)points.height;

      m_frame.params = params;
      std::memset (&m_frame.uniforms, 0, sizeof (m_frame.uniforms));
      m_frame.uniforms.view_proj = m4 (params.proj * params.view);
      m_frame.uniforms.light_matrix = m4 (m_terrain_resources.light_biased);
      m_frame.uniforms.camera_pos = f4 (params.camera_pos);
      m_frame.uniforms.sun_dir = f4 (params.sun_dir);
      m_frame.uniforms.sun_diffuse = f4lin (params.sun_diffuse);
      m_frame.uniforms.sun_specular = f4lin (params.sun_specular);
      m_frame.uniforms.ambient = f4lin (params.ambient);
      m_frame.uniforms.fog_color = f4lin (params.clear_color, params.fog_scale);
      m_frame.uniforms.misc.x = params.time;
      m_frame.uniforms.shadow.x = m_terrain_resources.have_shadow
                                    ? m_terrain_resources.params.shadow_strength
                                    : 0.0f;
      m_frame.uniforms.shadow.y =
        m_terrain_resources.shadow_map
          ? 1.0f / (float)m_terrain_resources.shadow_map.width
          : 1.0f / 4096.0f;

      update_exposure ();

      m_frame.command_buffer = [m_queue commandBuffer];
      m_frame.current_scene = m_targets.scene_a;
      m_frame.scene_encoder = nil;
      m_frame.scene_pass_done = false;
      return true;
    }

    id<MTLRenderCommandEncoder> MetalRenderer::scene_encoder () {
      if (m_frame.scene_encoder)
        return m_frame.scene_encoder;

      MTLRenderPassDescriptor* rp =
        [MTLRenderPassDescriptor renderPassDescriptor];
      rp.colorAttachments[0].texture = m_targets.msaa_color;
      rp.colorAttachments[0].resolveTexture = m_targets.scene_a;
      rp.colorAttachments[0].loadAction = MTLLoadActionClear;
      rp.colorAttachments[0].storeAction = MTLStoreActionMultisampleResolve;
      rp.colorAttachments[0].clearColor =
        MTLClearColorMake (std::pow (m_frame.params.clear_color.red, 2.2f),
                           std::pow (m_frame.params.clear_color.green, 2.2f),
                           std::pow (m_frame.params.clear_color.blue, 2.2f),
                           1.0);
      rp.depthAttachment.texture = m_targets.msaa_depth;
      rp.depthAttachment.loadAction = MTLLoadActionClear;
      rp.depthAttachment.storeAction = MTLStoreActionDontCare;
      rp.depthAttachment.clearDepth = 0.0; // reversed-Z far
      rp.stencilAttachment.texture = m_targets.msaa_depth;
      rp.stencilAttachment.loadAction = MTLLoadActionClear;
      rp.stencilAttachment.storeAction = MTLStoreActionDontCare;
      rp.stencilAttachment.clearStencil = 0;
      attach_gpu_pass (rp, GpuPass::Scene);

      MOPPE_METAL_ZONE (m_frame.tracy_metal, rp, "Scene");
      m_frame.scene_encoder =
        [m_frame.command_buffer renderCommandEncoderWithDescriptor:rp];
      m_frame.scene_encoder.label = @"World scene";
      [m_frame.scene_encoder setFrontFacingWinding:MTLWindingCounterClockwise];
      return m_frame.scene_encoder;
    }

    void MetalRenderer::begin_gpu_pass (id<MTLRenderCommandEncoder> enc,
                                        GpuPass pass) {
      record_gpu_pass_start (m_frame, enc, pass);
    }

    void MetalRenderer::finish_gpu_pass (id<MTLRenderCommandEncoder> enc) {
      record_gpu_pass_end (m_frame, enc);
    }

    void MetalRenderer::attach_gpu_pass (MTLRenderPassDescriptor* desc,
                                         GpuPass pass) {
      attach_gpu_pass_timing (m_frame, desc, pass);
    }

    void MetalRenderer::end_scene_encoder () {
      if (m_frame.scene_encoder) {
        [m_frame.scene_encoder endEncoding];
        m_frame.scene_encoder = nil;
        m_frame.scene_pass_done = true;
      } else if (!m_frame.scene_pass_done) {
        // Nothing was drawn: run an empty pass so sceneA is cleared.
        scene_encoder ();
        [m_frame.scene_encoder endEncoding];
        m_frame.scene_encoder = nil;
        m_frame.scene_pass_done = true;
      }
    }

    void MetalTerrainPass::draw (const MetalTerrainPassInputs& inputs,
                                 const ChunkDraw* chunks,
                                 int count) {
      const MetalPipelines& pipelines = inputs.pipelines;
      MetalTerrainResources& terrain = inputs.terrain;
      const MetalWaterResources& water = inputs.water;
      const MetalFrameEncoding& frame = inputs.frame;
      id<MTLRenderCommandEncoder> enc = inputs.encoder;

      [enc setRenderPipelineState:pipelines.terrain];
      [enc setDepthStencilState:pipelines.depth[1][1]];
      [enc setCullMode:MTLCullModeBack];

      MoppeTerrainUniforms u;
      std::memset (&u, 0, sizeof (u));
      u.view_proj = frame.uniforms.view_proj;
      u.light_matrix = m4 (terrain.light_biased);
      u.camera_pos = frame.uniforms.camera_pos;
      u.sun_dir = frame.uniforms.sun_dir;
      u.sun_diffuse = frame.uniforms.sun_diffuse;
      u.sun_specular = frame.uniforms.sun_specular;
      u.ambient = frame.uniforms.ambient;
      u.fog_color = frame.uniforms.fog_color;
      u.fog_color.w = terrain.params.fog_scale;
      u.params0.x = terrain.params.scale[0];
      u.params0.y = terrain.params.scale[1];
      u.params0.z = terrain.params.scale[2];
      u.params0.w = terrain.params.tex_scale;
      u.params1.x = terrain.params.height_scale;
      u.params1.y = terrain.params.sea_level_norm;
      u.params1.z = terrain.have_shadow ? terrain.params.shadow_strength : 0;
      u.params1.w = terrain.shadow_map ? 1.0f / (float)terrain.shadow_map.width
                                       : 1.0f / 4096.0f;
      u.params2.x = static_cast<float> (terrain.params.projection);
      u.params2.y = terrain.params.torus_major_radius;
      u.params2.z = terrain.params.torus_minor_radius;
      u.params2.w = terrain.params.torus_height_scale;
      u.params3.x = terrain.params.derive_normals ? 1.0f : 0.0f;
      u.params3.y = terrain.params.periodic ? 1.0f : 0.0f;
      float height_blend = 1.0f;
      if (terrain.height_transition_active && terrain.previous_heights) {
        const float elapsed =
          std::max (0.0f, frame.params.time - terrain.height_transition_start);
        const float duration =
          std::max (0.001f, terrain.params.height_transition_duration);
        const float t = std::min (1.0f, elapsed / duration);
        height_blend = t * t * (3.0f - 2.0f * t);
        if (t >= 1.0f)
          terrain.height_transition_active = false;
      }
      u.params3.z = height_blend;
      u.params3.w = terrain.previous_shadow_map
                      ? 1.0f / (float)terrain.previous_shadow_map.width
                      : u.params1.w;
      if (terrain.have_overlay) {
        u.params4.x = 1.0f + static_cast<float> (terrain.overlay_params.ramp);
        u.params4.y = terrain.overlay_params.minimum;
        u.params4.z = terrain.overlay_params.maximum;
        u.params4.w = terrain.overlay_params.opacity;
      }
      u.params5.x = terrain.params.topology_overlay ? 1.0f : 0.0f;
      u.params5.y = water.have_water_levels ? 1.0f : 0.0f;
      u.params5.z = terrain.have_moisture ? 1.0f : 0.0f;
      u.params5.w = terrain.have_geology ? 1.0f : 0.0f;
      // Fragment-rate normals need the gameplay normal texture to be
      // authoritative: the Lab preview derives normals from heights, and
      // Cover View rotates normals into the torus frame per vertex.
      u.params6.x =
        (terrain.params.fragment_normals && !terrain.params.derive_normals &&
         terrain.params.projection == TerrainProjection::Plane)
          ? 1.0f
          : 0.0f;
      u.params6.y = terrain.have_shore ? 1.0f : 0.0f;
      u.params6.z = terrain.have_paths ? 1.0f : 0.0f;
      u.params6.w = terrain.have_forest ? 1.0f : 0.0f;
      u.params7.x =
        (terrain.params.snow_support_filter && terrain.have_snow_support)
          ? 1.0f
          : 0.0f;
      u.params7.y =
        (terrain.params.channel_flux_detail && terrain.have_channel_flux)
          ? 1.0f
          : 0.0f;

      [enc setVertexBytes:&u length:sizeof (u) atIndex:MOPPE_BUF_FRAME];
      [enc setFragmentBytes:&u length:sizeof (u) atIndex:MOPPE_BUF_FRAME];
      [enc setVertexTexture:terrain.heights atIndex:MOPPE_TEX_HEIGHTS];
      [enc setVertexTexture:terrain.normals atIndex:MOPPE_TEX_NORMALS];
      [enc setVertexTexture:(terrain.previous_heights ? terrain.previous_heights
                                                      : terrain.heights)
                    atIndex:MOPPE_TEX_PREVIOUS_HEIGHTS];

      MetalTexture* grass = (MetalTexture*)terrain.grass.get ();
      MetalTexture* dirt = (MetalTexture*)terrain.dirt.get ();
      MetalTexture* rock = (MetalTexture*)terrain.rock.get ();
      MetalTexture* snow = (MetalTexture*)terrain.snow.get ();
      if (grass) {
        [enc setFragmentTexture:grass->texture atIndex:MOPPE_TEX_GRASS];
        [enc setFragmentSamplerState:grass->sampler atIndex:0];
      }
      if (dirt)
        [enc setFragmentTexture:dirt->texture atIndex:MOPPE_TEX_DIRT];
      if (rock)
        [enc setFragmentTexture:rock->texture atIndex:MOPPE_TEX_ROCK];
      if (snow)
        [enc setFragmentTexture:snow->texture atIndex:MOPPE_TEX_SNOW];
      if (terrain.shadow_map)
        [enc setFragmentTexture:terrain.shadow_map atIndex:MOPPE_TEX_SHADOW];
      if (terrain.shadow_map)
        [enc setFragmentTexture:(terrain.previous_shadow_map
                                   ? terrain.previous_shadow_map
                                   : terrain.shadow_map)
                        atIndex:MOPPE_TEX_PREVIOUS_SHADOW];
      if (terrain.overlay)
        [enc setFragmentTexture:terrain.overlay
                        atIndex:MOPPE_TEX_TERRAIN_OVERLAY];
      [enc setFragmentTexture:(terrain.have_moisture ? terrain.moisture
                                                     : terrain.heights)
                      atIndex:MOPPE_TEX_TERRAIN_MOISTURE];
      [enc setFragmentTexture:(water.have_water_levels ? water.water_levels
                                                       : terrain.heights)
                      atIndex:MOPPE_TEX_TERRAIN_WATER];
      [enc setFragmentTexture:(terrain.have_geology ? terrain.geology
                                                    : terrain.heights)
                      atIndex:MOPPE_TEX_TERRAIN_GEOLOGY];
      [enc setFragmentTexture:terrain.normals
                      atIndex:MOPPE_TEX_TERRAIN_NORMALS];
      [enc setFragmentTexture:(terrain.have_shore ? terrain.shore
                                                  : terrain.heights)
                      atIndex:MOPPE_TEX_TERRAIN_SHORE];
      [enc setFragmentTexture:(terrain.have_paths ? terrain.paths
                                                  : terrain.heights)
                      atIndex:MOPPE_TEX_TERRAIN_PATHS];
      [enc setFragmentTexture:(terrain.have_forest ? terrain.forest
                                                   : terrain.heights)
                      atIndex:MOPPE_TEX_TERRAIN_FOREST];
      [enc setFragmentTexture:(terrain.have_snow_support ? terrain.snow_support
                                                         : terrain.heights)
                      atIndex:MOPPE_TEX_TERRAIN_SNOW_SUPPORT];
      [enc setFragmentTexture:(terrain.have_channel_flux ? terrain.channel_flux
                                                         : terrain.heights)
                      atIndex:MOPPE_TEX_TERRAIN_CHANNEL_FLUX];

      for (int i = 0; i < count; ++i) {
        const int lod =
          std::max (0, std::min (TERRAIN_LOD_COUNT - 1, (int)chunks[i].lod));
        MoppeChunkUniforms c;
        std::memset (&c, 0, sizeof (c));
        c.origin_x = chunks[i].x0;
        c.origin_z = chunks[i].z0;
        c.step = TERRAIN_LOD_STEP[lod];
        c.verts_per_row = TERRAIN_LOD_VERTS[lod];
        c.morph_start = chunks[i].morph_start;
        c.morph_end = chunks[i].morph_end;
        c.parent_step = lod + 1 < TERRAIN_LOD_COUNT ? TERRAIN_LOD_STEP[lod + 1]
                                                    : TERRAIN_LOD_STEP[lod];
        c.world_offset.x = chunks[i].offset_x;
        c.world_offset.z = chunks[i].offset_z;
        [enc setVertexBytes:&c length:sizeof (c) atIndex:MOPPE_BUF_CHUNK];
        [enc drawIndexedPrimitives:MTLPrimitiveTypeTriangleStrip
                        indexCount:terrain.index_count[lod]
                         indexType:MTLIndexTypeUInt32
                       indexBuffer:terrain.indices[lod]
                 indexBufferOffset:0];
      }
    }

    void MetalRenderer::draw_terrain (const ChunkDraw* chunks, int count) {
      if (!m_pipelines.terrain || !m_terrain_resources.have_terrain ||
          count == 0)
        return;
      id<MTLRenderCommandEncoder> encoder = scene_encoder ();
      begin_gpu_pass (encoder, GpuPass::Terrain);
      MetalTerrainPass::draw ({ encoder,
                                m_pipelines,
                                m_terrain_resources,
                                m_water_resources,
                                m_frame },
                              chunks,
                              count);
    }

    void MetalScenePass::draw_sky (const MetalScenePassInputs& inputs,
                                   const SkyParams& params) {
      id<MTLDevice> device = inputs.device;
      id<MTLRenderCommandEncoder> enc = inputs.encoder;
      const MetalPipelines& pipelines = inputs.pipelines;
      MetalSceneResources& scene = inputs.scene;
      MetalFrameEncoding& frame = inputs.frame;

      // Build the dome lazily: 24x24 sphere, radius 4000, positions
      // only (matches the old display list).
      if (!scene.sky_verts) {
        const float radius = 4000.0f;
        const int slices = 24, stacks = 24;
        std::vector<float> v;
        for (int i = 0; i < stacks; ++i) {
          const float a0 = PI * (float)i / stacks - PI / 2;
          const float a1 = PI * (float)(i + 1) / stacks - PI / 2;
          const float y0 = radius * std::sin (a0);
          const float r0 = radius * std::cos (a0);
          const float y1 = radius * std::sin (a1);
          const float r1 = radius * std::cos (a1);
          for (int j = 0; j < slices; ++j) {
            const float b0 = PI2 * (float)j / slices;
            const float b1 = PI2 * (float)(j + 1) / slices;
            const float p[4][3] = {
              { r0 * std::cos (b0), y0, r0 * std::sin (b0) },
              { r0 * std::cos (b1), y0, r0 * std::sin (b1) },
              { r1 * std::cos (b0), y1, r1 * std::sin (b0) },
              { r1 * std::cos (b1), y1, r1 * std::sin (b1) },
            };
            const int tris[6] = { 0, 2, 1, 1, 2, 3 };
            for (int k = 0; k < 6; ++k) {
              v.push_back (p[tris[k]][0]);
              v.push_back (p[tris[k]][1]);
              v.push_back (p[tris[k]][2]);
            }
          }
        }
        scene.sky_vcount = (uint32_t)(v.size () / 3);
        scene.sky_verts =
          [device newBufferWithBytes:v.data ()
                              length:v.size () * sizeof (float)
                             options:MTLResourceStorageModeShared];
      }

      [enc setRenderPipelineState:pipelines.sky];
      // Depth test on, write off: terrain occludes the cloud shader.
      [enc setDepthStencilState:pipelines.depth[1][0]];
      [enc setCullMode:MTLCullModeNone];

      // Rotation-only view: the dome follows the camera.
      Mat4 view_rot = frame.params.view;
      view_rot.m[12] = view_rot.m[13] = view_rot.m[14] = 0;

      MoppeSkyUniforms u;
      std::memset (&u, 0, sizeof (u));
      u.view_proj = m4 (frame.params.proj * view_rot);
      u.sun_dir = f4 (params.sun_dir);
      u.fog_color = f4lin (params.fog_color);
      u.params.x = params.time;
      u.params.y = params.sun_height;
      u.params.z = params.cloudiness;

      [enc setVertexBuffer:scene.sky_verts offset:0 atIndex:MOPPE_BUF_VERTICES];
      [enc setVertexBytes:&u length:sizeof (u) atIndex:MOPPE_BUF_FRAME];
      [enc setFragmentBytes:&u length:sizeof (u) atIndex:MOPPE_BUF_FRAME];
      [enc drawPrimitives:MTLPrimitiveTypeTriangle
              vertexStart:0
              vertexCount:scene.sky_vcount];
    }

    void MetalRenderer::draw_sky (const SkyParams& params) {
      if (!m_pipelines.sky)
        return;
      id<MTLRenderCommandEncoder> encoder = scene_encoder ();
      begin_gpu_pass (encoder, GpuPass::Sky);
      MetalScenePass::draw_sky ({ m_device,
                                  encoder,
                                  m_pipelines,
                                  m_terrain_resources,
                                  m_scene_resources,
                                  m_frame },
                                params);
    }

    void MetalWaterPass::draw_ocean (const MetalWaterPassInputs& inputs,
                                     const OceanParams& params) {
      id<MTLRenderCommandEncoder> enc = inputs.encoder;
      const MetalPipelines& pipelines = inputs.pipelines;
      const MetalTerrainResources& terrain = inputs.terrain;
      const MetalWaterResources& water_resources = inputs.water;
      const MetalFrameEncoding& frame = inputs.frame;

      [enc setRenderPipelineState:pipelines.ocean];
      [enc setDepthStencilState:pipelines.depth[1][1]];
      [enc setCullMode:MTLCullModeNone]; // visible from below too

      MoppeOceanUniforms u;
      std::memset (&u, 0, sizeof (u));
      u.view_proj = frame.uniforms.view_proj;
      u.light_matrix = m4 (terrain.light_biased);
      u.camera_pos = frame.uniforms.camera_pos;
      u.sun_dir = frame.uniforms.sun_dir;
      u.sun_diffuse = frame.uniforms.sun_diffuse;
      u.sun_specular = frame.uniforms.sun_specular;
      u.ambient = frame.uniforms.ambient;
      u.fog_color = f4lin (params.fog_color, params.fog_scale);
      u.params.x = params.time;
      u.params.y = water_resources.ocean_level;
      u.params.z = terrain.params.periodic ? 1.0f : 0.0f;
      u.params.w = water_resources.have_water_levels ? 1.0f : 0.0f;
      u.world_offset.x = params.world_offset[0];
      u.world_offset.z = params.world_offset[2];
      u.shadow.x = terrain.have_shadow ? terrain.params.shadow_strength : 0.0f;
      u.shadow.y = terrain.shadow_map ? 1.0f / (float)terrain.shadow_map.width
                                      : 1.0f / 4096.0f;

      // Shore data: the fragment shader reads the height texture to
      // find the seabed for foam and shallows.
      if (terrain.have_terrain && terrain.heights) {
        u.shore.x = 1.0f / terrain.params.scale[0];
        u.shore.y = 1.0f / terrain.params.scale[2];
        u.shore.z = terrain.params.scale[1];
        u.shore.w = (float)terrain.params.width;
        [enc setFragmentTexture:terrain.heights atIndex:MOPPE_TEX_HEIGHTS];
        [enc setVertexTexture:terrain.heights atIndex:MOPPE_TEX_HEIGHTS];
      }
      id<MTLTexture> water = water_resources.have_water_levels
                               ? water_resources.water_levels
                               : terrain.heights;
      [enc setVertexTexture:water atIndex:MOPPE_TEX_WATER_LEVELS];
      [enc setFragmentTexture:water atIndex:MOPPE_TEX_WATER_LEVELS_FRAGMENT];
      u.current.x = water_resources.have_water_flow ? 1.0f : 0.0f;
      [enc setFragmentTexture:water_resources.have_water_flow
                                ? water_resources.water_flow
                                : water
                      atIndex:MOPPE_TEX_WATER_FLOW_FRAGMENT];
      u.current.y = terrain.have_geology ? 1.0f : 0.0f;
      [enc setFragmentTexture:terrain.have_geology ? terrain.geology : water
                      atIndex:MOPPE_TEX_WATER_GEOLOGY_FRAGMENT];
      if (terrain.shadow_map)
        [enc setFragmentTexture:terrain.shadow_map atIndex:MOPPE_TEX_SHADOW];

      // Near standing water renders on the terrain lattice through the
      // mesh pipeline; the coarse grid keeps the horizon. Both passes
      // discard on the same radius so they partition exactly.
      const bool lattice = pipelines.water_tiles &&
                           water_resources.have_water_levels &&
                           terrain.have_terrain && terrain.heights;
      const float fine_radius = 700.0f;
      const float tile_world = 15.0f * terrain.params.scale[0];
      const int tiles_side = (int)std::ceil ((2.0f * fine_radius) / tile_world);
      if (lattice) {
        u.tiles.x =
          std::floor ((frame.params.camera_pos[0] - fine_radius) / tile_world);
        u.tiles.y =
          std::floor ((frame.params.camera_pos[2] - fine_radius) / tile_world);
        u.tiles.z = (float)tiles_side;
        u.tiles.w = fine_radius;
      }

      [enc setVertexBuffer:water_resources.ocean_verts
                    offset:0
                   atIndex:MOPPE_BUF_VERTICES];
      [enc setVertexBytes:&u length:sizeof (u) atIndex:MOPPE_BUF_FRAME];
      [enc setFragmentBytes:&u length:sizeof (u) atIndex:MOPPE_BUF_FRAME];
      [enc drawPrimitives:MTLPrimitiveTypeTriangle
              vertexStart:0
              vertexCount:water_resources.ocean_vcount];

      if (lattice) {
        if (@available (macOS 13.0, iOS 16.0, tvOS 16.0, *)) {
          MoppeOceanUniforms t = u;
          t.tiles.w = -fine_radius;
          [enc setRenderPipelineState:pipelines.water_tiles];
          [enc setObjectBytes:&t length:sizeof (t) atIndex:MOPPE_BUF_FRAME];
          [enc setObjectTexture:terrain.heights atIndex:MOPPE_TEX_HEIGHTS];
          [enc setObjectTexture:water_resources.water_levels
                        atIndex:MOPPE_TEX_WATER_LEVELS];
          [enc setMeshBytes:&t length:sizeof (t) atIndex:MOPPE_BUF_FRAME];
          [enc setMeshTexture:terrain.heights atIndex:MOPPE_TEX_HEIGHTS];
          [enc setMeshTexture:water_resources.water_levels
                      atIndex:MOPPE_TEX_WATER_LEVELS];
          [enc setFragmentBytes:&t length:sizeof (t) atIndex:MOPPE_BUF_FRAME];
          const NSUInteger total =
            (NSUInteger)tiles_side * (NSUInteger)tiles_side;
          [enc drawMeshThreadgroups:MTLSizeMake ((total + 63) / 64, 1, 1)
            threadsPerObjectThreadgroup:MTLSizeMake (64, 1, 1)
              threadsPerMeshThreadgroup:MTLSizeMake (256, 1, 1)];
        }
      }
    }

    void MetalRenderer::draw_ocean (const OceanParams& params) {
      if (!m_pipelines.ocean || !m_water_resources.ocean_verts)
        return;
      id<MTLRenderCommandEncoder> encoder = scene_encoder ();
      begin_gpu_pass (encoder, GpuPass::Water);
      MetalWaterPass::draw_ocean ({ encoder,
                                    m_pipelines,
                                    m_terrain_resources,
                                    m_water_resources,
                                    m_frame },
                                  params);
    }

    void MetalScenePass::draw_dust (const MetalScenePassInputs& inputs,
                                    std::span<const DustEmission> emissions,
                                    float logical_time) {
      id<MTLDevice> device = inputs.device;
      id<MTLRenderCommandEncoder> enc = inputs.encoder;
      const MetalPipelines& pipelines = inputs.pipelines;
      const MetalFrameEncoding& frame = inputs.frame;

      MoppeDustUniforms uniforms {};
      uniforms.camera_right = f4 (frame.params.cam_right);
      uniforms.camera_up = f4 (frame.params.cam_up);
      uniforms.params.x = logical_time;

      for (int pass = 0; pass < 2; ++pass) {
        const bool additive = pass == 1;
        std::vector<MoppeDustEmission> packed;
        packed.reserve (emissions.size ());
        for (const DustEmission& emission : emissions) {
          if (emission.additive != additive || emission.particle_count == 0)
            continue;
          MoppeDustEmission p {};
          p.position_birth = f4 (emission.position, emission.birth_time);
          p.velocity_count = f4 (emission.velocity,
                                 static_cast<float> (emission.particle_count));
          p.color_id =
            f4 (emission.color, static_cast<float> (emission.id & 0x00ffffffu));
          p.style.x = emission.size;
          p.style.y = emission.life;
          p.style.z = emission.gravity;
          p.style.w = emission.spread;
          packed.push_back (p);
        }
        if (packed.empty ())
          continue;

        [enc setDepthStencilState:pipelines.depth[1][0]];
        [enc setCullMode:MTLCullModeNone];
        id<MTLRenderPipelineState> mesh_pipeline =
          additive ? pipelines.dust_mesh_add : pipelines.dust_mesh_soft;
        if (mesh_pipeline) {
          if (@available (macOS 13.0, iOS 16.0, tvOS 16.0, *)) {
            id<MTLBuffer> buffer = [device
              newBufferWithBytes:packed.data ()
                          length:packed.size () * sizeof (MoppeDustEmission)
                         options:MTLResourceStorageModeShared];
            [enc setRenderPipelineState:mesh_pipeline];
            [enc setMeshBuffer:buffer offset:0 atIndex:MOPPE_BUF_VERTICES];
            [enc setMeshBytes:&frame.uniforms
                       length:sizeof (frame.uniforms)
                      atIndex:MOPPE_BUF_FRAME];
            [enc setMeshBytes:&uniforms
                       length:sizeof (uniforms)
                      atIndex:MOPPE_BUF_DRAW];
            [enc drawMeshThreadgroups:MTLSizeMake (packed.size (), 1, 1)
              threadsPerObjectThreadgroup:MTLSizeMake (1, 1, 1)
                threadsPerMeshThreadgroup:MTLSizeMake (64, 1, 1)];
            continue;
          }
        }

        [enc setRenderPipelineState:additive ? pipelines.dust_add
                                             : pipelines.dust_soft];
        [enc setVertexBytes:&frame.uniforms
                     length:sizeof (frame.uniforms)
                    atIndex:MOPPE_BUF_FRAME];
        [enc setVertexBytes:&uniforms
                     length:sizeof (uniforms)
                    atIndex:MOPPE_BUF_DRAW];
        for (const MoppeDustEmission& emission : packed) {
          [enc setVertexBytes:&emission
                       length:sizeof (emission)
                      atIndex:MOPPE_BUF_VERTICES];
          [enc
            drawPrimitives:MTLPrimitiveTypeTriangle
               vertexStart:0
               vertexCount:6
             instanceCount:static_cast<NSUInteger> (emission.velocity_count.w)];
        }
      }
    }

    void MetalRenderer::draw_dust (std::span<const DustEmission> emissions,
                                   float logical_time) {
      if (emissions.empty () || !m_pipelines.dust_soft || !m_pipelines.dust_add)
        return;
      id<MTLRenderCommandEncoder> encoder = scene_encoder ();
      MetalScenePass::draw_dust ({ m_device,
                                   encoder,
                                   m_pipelines,
                                   m_terrain_resources,
                                   m_scene_resources,
                                   m_frame },
                                 emissions,
                                 logical_time);
    }

    // -- draw lists ----------------------------------------------------

    size_t
    MetalDrawListEncoder::stream_vertices (id<MTLDevice> device,
                                           MetalFrameEncoding& frame,
                                           const std::vector<Vertex>& verts) {
      const size_t bytes = verts.size () * sizeof (Vertex);
      const size_t offset = frame.stream_used;
      const size_t needed = offset + bytes;

      id<MTLBuffer> buf = frame.stream[frame.slot];
      if (!buf || buf.length < needed) {
        // Native-resolution HUD text plus transient world-space effects can
        // legitimately exceed 1 MiB in Terrain Lab. Avoid replacing the shared
        // per-frame buffer between the scene and HUD encoders in the common
        // case; growth remains available for genuinely larger frames.
        size_t cap = buf ? buf.length : (4 << 20);
        while (cap < needed)
          cap *= 2;
        // Old buffer stays retained by prior encoder commands.
        buf = [device newBufferWithLength:cap
                                  options:MTLResourceStorageModeShared];
        if (offset > 0 && frame.stream[frame.slot])
          std::memcpy (buf.contents, frame.stream[frame.slot].contents, offset);
        frame.stream[frame.slot] = buf;
      }

      std::memcpy ((uint8_t*)buf.contents + offset, verts.data (), bytes);
      frame.stream_used = needed;
      return offset;
    }

    void
    MetalDrawListEncoder::set_run_state (id<MTLRenderCommandEncoder> enc,
                                         const MetalPipelines& pipelines,
                                         const MetalTerrainResources& terrain,
                                         const DrawState& s,
                                         const Texture* tex,
                                         bool hud) {
      if (hud) {
        [enc setRenderPipelineState:pipelines.hud];
        [enc setCullMode:MTLCullModeNone];
      } else {
        [enc setRenderPipelineState:s.additive ? pipelines.uber_add
                                    : s.blend  ? pipelines.uber_blend
                                               : pipelines.uber_opaque];
        [enc setDepthStencilState:pipelines.depth[s.depth_test ? 1 : 0]
                                                 [s.depth_write ? 1 : 0]];
        [enc setCullMode:s.cull ? MTLCullModeBack : MTLCullModeNone];
      }

      const MetalTexture* mt = (const MetalTexture*)tex;
      if (!mt)
        mt = (const MetalTexture*)pipelines.white.get ();
      [enc setFragmentTexture:mt->texture atIndex:MOPPE_TEX_COLOR];
      if (!hud && terrain.shadow_map)
        [enc setFragmentTexture:terrain.shadow_map atIndex:MOPPE_TEX_SHADOW];
      [enc setFragmentSamplerState:mt->sampler atIndex:0];
    }

    void MetalDrawListEncoder::play (const MetalDrawListInputs& inputs,
                                     const std::vector<Vertex>& verts,
                                     const std::vector<DrawList::Run>& runs,
                                     bool hud) {
      id<MTLRenderCommandEncoder> enc = inputs.encoder;
      MetalFrameEncoding& frame = inputs.frame;

      if (verts.empty ())
        return;
      const size_t base = stream_vertices (inputs.device, frame, verts);
      [enc setVertexBuffer:frame.stream[frame.slot]
                    offset:base
                   atIndex:MOPPE_BUF_VERTICES];

      for (size_t i = 0; i < runs.size (); ++i) {
        const DrawList::Run& r = runs[i];
        if (r.count == 0)
          continue;
        set_run_state (
          enc, inputs.pipelines, inputs.terrain, r.state, r.texture, hud);
        [enc drawPrimitives:MTLPrimitiveTypeTriangle
                vertexStart:r.first
                vertexCount:r.count];
      }
    }

    void MetalScenePass::draw_list (const MetalScenePassInputs& inputs,
                                    const DrawList& list) {
      id<MTLDevice> device = inputs.device;
      id<MTLRenderCommandEncoder> enc = inputs.encoder;
      const MetalPipelines& pipelines = inputs.pipelines;
      const MetalTerrainResources& terrain = inputs.terrain;
      MetalFrameEncoding& frame = inputs.frame;

      MoppeDrawUniforms du;
      std::memset (&du, 0, sizeof (du));
      du.model = m4 (Mat4 ());
      du.nrm0.x = 1;
      du.nrm1.y = 1;
      du.nrm2.z = 1;
      [enc setVertexBytes:&du length:sizeof (du) atIndex:MOPPE_BUF_DRAW];
      [enc setVertexBytes:&frame.uniforms
                   length:sizeof (frame.uniforms)
                  atIndex:MOPPE_BUF_FRAME];
      [enc setFragmentBytes:&frame.uniforms
                     length:sizeof (frame.uniforms)
                    atIndex:MOPPE_BUF_FRAME];

      MetalDrawListEncoder::play ({ device, enc, pipelines, terrain, frame },
                                  list.vertices (),
                                  list.runs (),
                                  false);
    }

    void MetalRenderer::draw_list (const DrawList& list) {
      if (list.empty ())
        return;
      id<MTLRenderCommandEncoder> encoder = scene_encoder ();
      begin_gpu_pass (encoder, GpuPass::Scene);
      MetalScenePass::draw_list ({ m_device,
                                   encoder,
                                   m_pipelines,
                                   m_terrain_resources,
                                   m_scene_resources,
                                   m_frame },
                                 list);
    }

    void MetalScenePass::draw_mesh (const MetalScenePassInputs& inputs,
                                    const Mesh& mesh,
                                    const Mat4& model) {
      id<MTLRenderCommandEncoder> enc = inputs.encoder;
      const MetalPipelines& pipelines = inputs.pipelines;
      const MetalTerrainResources& terrain = inputs.terrain;
      const MetalFrameEncoding& frame = inputs.frame;
      const MetalMesh& m = (const MetalMesh&)mesh;

      MoppeDrawUniforms du;
      std::memset (&du, 0, sizeof (du));
      du.model = m4 (model);
      const NormalMat nm = NormalMat::from (model);
      du.nrm0 = f4 (nm.c0);
      du.nrm1 = f4 (nm.c1);
      du.nrm2 = f4 (nm.c2);
      [enc setVertexBytes:&du length:sizeof (du) atIndex:MOPPE_BUF_DRAW];
      [enc setVertexBytes:&frame.uniforms
                   length:sizeof (frame.uniforms)
                  atIndex:MOPPE_BUF_FRAME];
      [enc setFragmentBytes:&frame.uniforms
                     length:sizeof (frame.uniforms)
                    atIndex:MOPPE_BUF_FRAME];

      [enc setVertexBuffer:m.vertices offset:0 atIndex:MOPPE_BUF_VERTICES];
      for (size_t i = 0; i < m.runs.size (); ++i) {
        const DrawList::Run& r = m.runs[i];
        if (r.count == 0)
          continue;
        MetalDrawListEncoder::set_run_state (
          enc, pipelines, terrain, r.state, r.texture, false);
        [enc drawPrimitives:MTLPrimitiveTypeTriangle
                vertexStart:r.first
                vertexCount:r.count];
      }
    }

    void MetalRenderer::draw_mesh (const Mesh& mesh, const Mat4& model) {
      const MetalMesh& metal_mesh = (const MetalMesh&)mesh;
      if (!metal_mesh.vertices)
        return;
      id<MTLRenderCommandEncoder> encoder = scene_encoder ();
      begin_gpu_pass (encoder, GpuPass::Scene);
      MetalScenePass::draw_mesh ({ m_device,
                                   encoder,
                                   m_pipelines,
                                   m_terrain_resources,
                                   m_scene_resources,
                                   m_frame },
                                 mesh,
                                 model);
    }

    void MetalWaterPass::draw_rivers (const MetalWaterPassInputs& inputs,
                                      const Mesh& mesh,
                                      const Mat4& model) {
      id<MTLRenderCommandEncoder> enc = inputs.encoder;
      const MetalPipelines& pipelines = inputs.pipelines;
      const MetalTerrainResources& terrain = inputs.terrain;
      const MetalFrameEncoding& frame = inputs.frame;
      const MetalMesh& m = (const MetalMesh&)mesh;

      [enc setRenderPipelineState:pipelines.river];
      [enc setDepthStencilState:pipelines.river_depth];
      [enc setStencilReferenceValue:1];
      [enc setCullMode:MTLCullModeNone];

      MoppeDrawUniforms du;
      std::memset (&du, 0, sizeof (du));
      du.model = m4 (model);
      const NormalMat nm = NormalMat::from (model);
      du.nrm0 = f4 (nm.c0);
      du.nrm1 = f4 (nm.c1);
      du.nrm2 = f4 (nm.c2);
      [enc setVertexBytes:&du length:sizeof (du) atIndex:MOPPE_BUF_DRAW];
      [enc setVertexBytes:&frame.uniforms
                   length:sizeof (frame.uniforms)
                  atIndex:MOPPE_BUF_FRAME];
      [enc setFragmentBytes:&frame.uniforms
                     length:sizeof (frame.uniforms)
                    atIndex:MOPPE_BUF_FRAME];
      if (terrain.shadow_map)
        [enc setFragmentTexture:terrain.shadow_map atIndex:MOPPE_TEX_SHADOW];
      [enc setVertexBuffer:m.vertices offset:0 atIndex:MOPPE_BUF_VERTICES];
      for (const DrawList::Run& run : m.runs)
        if (run.count > 0)
          [enc drawPrimitives:MTLPrimitiveTypeTriangle
                  vertexStart:run.first
                  vertexCount:run.count];
    }

    void MetalRenderer::draw_rivers (const Mesh& mesh, const Mat4& model) {
      const MetalMesh& metal_mesh = (const MetalMesh&)mesh;
      if (!m_pipelines.river || !metal_mesh.vertices)
        return;
      id<MTLRenderCommandEncoder> encoder = scene_encoder ();
      begin_gpu_pass (encoder, GpuPass::Water);
      MetalWaterPass::draw_rivers ({ encoder,
                                     m_pipelines,
                                     m_terrain_resources,
                                     m_water_resources,
                                     m_frame },
                                   mesh,
                                   model);
    }

    // -- post ----------------------------------------------------------

    void MetalPostPass::apply_underwater (const MetalPostPassInputs& inputs,
                                          float time) {
      const MetalPipelines& pipelines = inputs.pipelines;
      MetalFrameTargets& targets = inputs.targets;
      MetalFrameEncoding& frame = inputs.frame;

      id<MTLTexture> src = frame.current_scene;
      id<MTLTexture> dst =
        (src == targets.scene_a) ? targets.scene_b : targets.scene_a;

      MTLRenderPassDescriptor* rp =
        [MTLRenderPassDescriptor renderPassDescriptor];
      rp.colorAttachments[0].texture = dst;
      rp.colorAttachments[0].loadAction = MTLLoadActionDontCare;
      rp.colorAttachments[0].storeAction = MTLStoreActionStore;
      attach_gpu_pass_timing (frame, rp, GpuPass::Post);

      MOPPE_METAL_ZONE (metal_tracy (frame), rp, "Underwater");
      id<MTLRenderCommandEncoder> enc =
        [frame.command_buffer renderCommandEncoderWithDescriptor:rp];
      enc.label = @"Underwater post-process";
      record_gpu_pass_start (frame, enc, GpuPass::Post);
      MoppeQuadUniforms q;
      std::memset (&q, 0, sizeof (q));
      q.tint.x = q.tint.y = q.tint.z = q.tint.w = 1;
      q.params.x = 1; // no zoom
      q.params.y = time;
      [enc setRenderPipelineState:pipelines.underwater];
      [enc setVertexBytes:&q length:sizeof (q) atIndex:MOPPE_BUF_FRAME];
      [enc setFragmentBytes:&q length:sizeof (q) atIndex:MOPPE_BUF_FRAME];
      [enc setFragmentTexture:src atIndex:MOPPE_TEX_SCENE];
      [enc drawPrimitives:MTLPrimitiveTypeTriangle vertexStart:0 vertexCount:3];
      [enc endEncoding];

      frame.current_scene = dst;
    }

    void MetalRenderer::apply_underwater (float time) {
      if (!m_pipelines.underwater)
        return;
      end_scene_encoder ();
      MetalPostPass::apply_underwater ({ m_pipelines, m_targets, m_frame },
                                       time);
    }

    void MetalPostPass::apply_motion_blur (const MetalPostPassInputs& inputs,
                                           float strength) {
      const MetalPipelines& pipelines = inputs.pipelines;
      MetalFrameTargets& targets = inputs.targets;
      MetalFrameEncoding& frame = inputs.frame;

      // Ghost quads: previous frame drawn back 3x, zoomed, faded --
      // then the composite becomes the next "previous frame", which
      // is what makes the radial streaks build up.  A freshly
      // (re)created prev texture holds undefined memory: skip the
      // ghosts and just prime it (the old build's m_blur_valid).
      if (!targets.prev_valid) {
        id<MTLBlitCommandEncoder> prime =
          [frame.command_buffer blitCommandEncoder];
        [prime copyFromTexture:frame.current_scene
                     toTexture:targets.prev_frame];
        [prime endEncoding];
        targets.prev_valid = true;
        return;
      }

      MTLRenderPassDescriptor* rp =
        [MTLRenderPassDescriptor renderPassDescriptor];
      rp.colorAttachments[0].texture = frame.current_scene;
      rp.colorAttachments[0].loadAction = MTLLoadActionLoad;
      rp.colorAttachments[0].storeAction = MTLStoreActionStore;
      attach_gpu_pass_timing (frame, rp, GpuPass::Post);

      MOPPE_METAL_ZONE (metal_tracy (frame), rp, "Motion blur");
      id<MTLRenderCommandEncoder> enc =
        [frame.command_buffer renderCommandEncoderWithDescriptor:rp];
      enc.label = @"Motion blur";
      record_gpu_pass_start (frame, enc, GpuPass::Post);
      [enc setRenderPipelineState:pipelines.ghost];
      for (int i = 1; i <= 3; ++i) {
        MoppeQuadUniforms q;
        std::memset (&q, 0, sizeof (q));
        const float alpha = 0.5f * strength / i;
        q.tint.x = q.tint.y = q.tint.z = 1;
        q.tint.w = alpha;
        q.params.x = 1.0f + 0.012f * i * strength;
        [enc setVertexBytes:&q length:sizeof (q) atIndex:MOPPE_BUF_FRAME];
        [enc setFragmentBytes:&q length:sizeof (q) atIndex:MOPPE_BUF_FRAME];
        [enc setFragmentTexture:targets.prev_frame atIndex:MOPPE_TEX_SCENE];
        [enc drawPrimitives:MTLPrimitiveTypeTriangle
                vertexStart:0
                vertexCount:3];
      }
      [enc endEncoding];

      id<MTLBlitCommandEncoder> blit =
        [frame.command_buffer blitCommandEncoder];
      [blit copyFromTexture:frame.current_scene toTexture:targets.prev_frame];
      [blit endEncoding];
    }

    void MetalRenderer::apply_motion_blur (float strength) {
      if (!m_pipelines.ghost || strength <= 0.01f)
        return;
      end_scene_encoder ();
      MetalPostPass::apply_motion_blur ({ m_pipelines, m_targets, m_frame },
                                        strength);
    }

    void MetalPostPass::apply_scene_blur (const MetalPostPassInputs& inputs) {
      const MetalPipelines& pipelines = inputs.pipelines;
      MetalFrameTargets& targets = inputs.targets;
      MetalFrameEncoding& frame = inputs.frame;

      const auto pass = [&frame] (NSString* label,
                                  id<MTLRenderPipelineState> pipeline,
                                  id<MTLTexture> src,
                                  id<MTLTexture> dst,
                                  const MoppeQuadUniforms& q) {
        MTLRenderPassDescriptor* rp =
          [MTLRenderPassDescriptor renderPassDescriptor];
        rp.colorAttachments[0].texture = dst;
        rp.colorAttachments[0].loadAction = MTLLoadActionDontCare;
        rp.colorAttachments[0].storeAction = MTLStoreActionStore;
        attach_gpu_pass_timing (frame, rp, GpuPass::Post);
        MOPPE_METAL_ZONE (metal_tracy (frame), rp, "Loading blur");
        id<MTLRenderCommandEncoder> enc =
          [frame.command_buffer renderCommandEncoderWithDescriptor:rp];
        enc.label = label;
        record_gpu_pass_start (frame, enc, GpuPass::Post);
        [enc setRenderPipelineState:pipeline];
        [enc setVertexBytes:&q length:sizeof (q) atIndex:MOPPE_BUF_FRAME];
        [enc setFragmentBytes:&q length:sizeof (q) atIndex:MOPPE_BUF_FRAME];
        [enc setFragmentTexture:src atIndex:MOPPE_TEX_SCENE];
        [enc drawPrimitives:MTLPrimitiveTypeTriangle
                vertexStart:0
                vertexCount:3];
        [enc endEncoding];
      };

      MoppeQuadUniforms q;
      std::memset (&q, 0, sizeof (q));
      q.tint.x = q.tint.y = q.tint.z = q.tint.w = 1;
      q.params.x = 1;
      pass (@"Loading background downsample",
            pipelines.copy,
            frame.current_scene,
            targets.bloom_a,
            q);
      q.params.z = 2.0f / (float)targets.bloom_a.width;
      pass (@"Loading background horizontal blur",
            pipelines.bloom_blur,
            targets.bloom_a,
            targets.bloom_b,
            q);
      q.params.z = 0;
      q.params.w = 2.0f / (float)targets.bloom_a.height;
      pass (@"Loading background vertical blur",
            pipelines.bloom_blur,
            targets.bloom_b,
            targets.bloom_a,
            q);
      q.params.w = 0;
      id<MTLTexture> dst = frame.current_scene == targets.scene_a
                             ? targets.scene_b
                             : targets.scene_a;
      pass (@"Loading background upsample",
            pipelines.copy,
            targets.bloom_a,
            dst,
            q);
      frame.current_scene = dst;
    }

    void MetalRenderer::apply_scene_blur () {
      if (!m_pipelines.copy || !m_pipelines.bloom_blur || !m_targets.bloom_a ||
          !m_targets.bloom_b)
        return;
      end_scene_encoder ();
      MetalPostPass::apply_scene_blur ({ m_pipelines, m_targets, m_frame });
    }

    // -- hud + present ---------------------------------------------------

    void MetalHudPass::draw (const MetalHudPassInputs& inputs,
                             const DrawList& list) {
      id<MTLDevice> device = inputs.device;
      const MetalPipelines& pipelines = inputs.pipelines;
      const MetalTerrainResources& terrain = inputs.terrain;
      MetalFrameTargets& targets = inputs.targets;
      MetalFrameEncoding& frame = inputs.frame;

      enum class QuadZone {
        BloomBright,
        BloomHorizontal,
        BloomVertical,
        Exposure
      };
      // One fullscreen pass into `dst` reading `src`.
      auto quad_pass = [&] (QuadZone zone,
                            GpuPass pass,
                            NSString* label,
                            id<MTLRenderPipelineState> pso,
                            id<MTLTexture> src,
                            id<MTLTexture> dst,
                            const MoppeQuadUniforms& q) {
        MTLRenderPassDescriptor* p =
          [MTLRenderPassDescriptor renderPassDescriptor];
        p.colorAttachments[0].texture = dst;
        p.colorAttachments[0].loadAction = MTLLoadActionDontCare;
        p.colorAttachments[0].storeAction = MTLStoreActionStore;
        attach_gpu_pass_timing (frame, p, pass);
        const auto encode = [&] {
          id<MTLRenderCommandEncoder> e =
            [frame.command_buffer renderCommandEncoderWithDescriptor:p];
          e.label = label;
          record_gpu_pass_start (frame, e, pass);
          [e setRenderPipelineState:pso];
          [e setVertexBytes:&q length:sizeof (q) atIndex:MOPPE_BUF_FRAME];
          [e setFragmentBytes:&q length:sizeof (q) atIndex:MOPPE_BUF_FRAME];
          [e setFragmentTexture:src atIndex:MOPPE_TEX_SCENE];
          [e drawPrimitives:MTLPrimitiveTypeTriangle
                vertexStart:0
                vertexCount:3];
          [e endEncoding];
        };
        switch (zone) {
        case QuadZone::BloomBright: {
          MOPPE_METAL_ZONE (metal_tracy (frame), p, "Bloom bright pass");
          encode ();
          break;
        }
        case QuadZone::BloomHorizontal: {
          MOPPE_METAL_ZONE (metal_tracy (frame), p, "Bloom horizontal blur");
          encode ();
          break;
        }
        case QuadZone::BloomVertical: {
          MOPPE_METAL_ZONE (metal_tracy (frame), p, "Bloom vertical blur");
          encode ();
          break;
        }
        case QuadZone::Exposure: {
          MOPPE_METAL_ZONE (metal_tracy (frame), p, "Exposure probe");
          encode ();
          break;
        }
        }
      };

      // Bloom: bright-pass into quarter res, then a separable blur
      // (a -> b horizontal, b -> a vertical).  The bright pass sees
      // the exposed scene so the glow tracks the eye's adaptation.
      const bool bloom_ok = frame.params.bloom && pipelines.bloom_bright &&
                            pipelines.bloom_blur && targets.bloom_a;
      if (bloom_ok) {
        MoppeQuadUniforms q;
        std::memset (&q, 0, sizeof (q));
        q.tint.x = q.tint.y = q.tint.z = 1;
        q.tint.w = targets.exposure * frame.params.exposure_bias;
        q.params.x = 1;
        quad_pass (QuadZone::BloomBright,
                   GpuPass::Bloom,
                   @"Bloom bright pass",
                   pipelines.bloom_bright,
                   frame.current_scene,
                   targets.bloom_a,
                   q);

        q.params.z = 1.0f / (float)targets.bloom_a.width;
        q.params.w = 0;
        quad_pass (QuadZone::BloomHorizontal,
                   GpuPass::Bloom,
                   @"Bloom horizontal blur",
                   pipelines.bloom_blur,
                   targets.bloom_a,
                   targets.bloom_b,
                   q);
        q.params.z = 0;
        q.params.w = 1.0f / (float)targets.bloom_a.height;
        quad_pass (QuadZone::BloomVertical,
                   GpuPass::Bloom,
                   @"Bloom vertical blur",
                   pipelines.bloom_blur,
                   targets.bloom_b,
                   targets.bloom_a,
                   q);
      }

      // Auto-exposure probe: a 32x16 average of this frame, blitted
      // to a CPU-visible buffer and read FRAMES_IN_FLIGHT frames
      // later in update_exposure().
      if (frame.params.auto_exposure && pipelines.probe && targets.probe_tex &&
          targets.probe_buf[frame.slot]) {
        MoppeQuadUniforms q;
        std::memset (&q, 0, sizeof (q));
        q.tint.x = q.tint.y = q.tint.z = q.tint.w = 1;
        q.params.x = 1;
        quad_pass (QuadZone::Exposure,
                   GpuPass::Exposure,
                   @"Exposure probe",
                   pipelines.probe,
                   frame.current_scene,
                   targets.probe_tex,
                   q);

        id<MTLBlitCommandEncoder> blit =
          [frame.command_buffer blitCommandEncoder];
        [blit copyFromTexture:targets.probe_tex
                       sourceSlice:0
                       sourceLevel:0
                      sourceOrigin:MTLOriginMake (0, 0, 0)
                        sourceSize:MTLSizeMake (PROBE_W, PROBE_H, 1)
                          toBuffer:targets.probe_buf[frame.slot]
                 destinationOffset:0
            destinationBytesPerRow:PROBE_W * 16
          destinationBytesPerImage:PROBE_W * PROBE_H * 16];
        [blit endEncoding];
      }

      MTLRenderPassDescriptor* rp =
        [MTLRenderPassDescriptor renderPassDescriptor];
      rp.colorAttachments[0].texture = frame.drawable.texture;
      rp.colorAttachments[0].loadAction = MTLLoadActionDontCare;
      rp.colorAttachments[0].storeAction = MTLStoreActionStore;
      attach_gpu_pass_timing (frame, rp, GpuPass::Present);

      MOPPE_METAL_ZONE (metal_tracy (frame), rp, "Present and HUD");
      id<MTLRenderCommandEncoder> enc =
        [frame.command_buffer renderCommandEncoderWithDescriptor:rp];
      enc.label = @"Present and HUD";
      record_gpu_pass_start (frame, enc, GpuPass::Present);

      // Scene quad with the tonemap, grade, bloom, and lens flare.
      if (pipelines.present) {
        MoppeQuadUniforms q;
        std::memset (&q, 0, sizeof (q));
        q.tint.x = q.tint.y = q.tint.z = 1;
        q.tint.w = targets.exposure * frame.params.exposure_bias;
        q.params.x = 1;
        q.params.y = frame.params.time;
#if !TARGET_OS_IPHONE
        q.params.z = frame.edr_headroom;
        q.params.w = 1;
#endif

        // Project the sun onto the screen for the flare; the game
        // supplies the occlusion term, we add the edge fade.
        if (frame.params.lens_flare && frame.params.sun_visibility > 0.001f) {
          const Mat4 vp = frame.params.proj * frame.params.view;
          const Vec3 sp =
            frame.params.camera_pos + frame.params.sun_dir * 4000.0f;
          const float* m = vp.m;
          const float cx = m[0] * sp[0] + m[4] * sp[1] + m[8] * sp[2] + m[12];
          const float cy = m[1] * sp[0] + m[5] * sp[1] + m[9] * sp[2] + m[13];
          const float cw = m[3] * sp[0] + m[7] * sp[1] + m[11] * sp[2] + m[15];
          if (cw > 0.01f) {
            const float nx = cx / cw, ny = cy / cw;
            const float edge =
              1.0f -
              std::max (0.0f,
                        (std::max (std::fabs (nx), std::fabs (ny)) - 0.85f) /
                          0.45f);
            if (edge > 0.0f) {
              q.sun.x = nx * 0.5f + 0.5f;
              q.sun.y = 1.0f - (ny * 0.5f + 0.5f);
              // Exposure folds in so the flare adapts with the eye.
              q.sun.z = frame.params.sun_visibility *
                        (edge > 1.0f ? 1.0f : edge) * targets.exposure *
                        frame.params.exposure_bias;
              q.sun.w = (float)targets.width / (float)targets.height;
            }
          }
        }

        [enc setRenderPipelineState:pipelines.present];
        [enc setVertexBytes:&q length:sizeof (q) atIndex:MOPPE_BUF_FRAME];
        [enc setFragmentBytes:&q length:sizeof (q) atIndex:MOPPE_BUF_FRAME];
        [enc setFragmentTexture:frame.current_scene atIndex:MOPPE_TEX_SCENE];
        if (bloom_ok)
          [enc setFragmentTexture:targets.bloom_a atIndex:MOPPE_TEX_BLOOM];
        [enc drawPrimitives:MTLPrimitiveTypeTriangle
                vertexStart:0
                vertexCount:3];
      }

      // HUD overlay in point coordinates.
      if (!list.empty () && pipelines.hud) {
        MoppeHudUniforms hu;
        std::memset (&hu, 0, sizeof (hu));
        hu.proj = m4 (
          Mat4::hud_ortho ((float)frame.width_pts, (float)frame.height_pts));
#if !TARGET_OS_IPHONE
        hu.params.x = 1;
#endif
        [enc setVertexBytes:&hu length:sizeof (hu) atIndex:MOPPE_BUF_FRAME];
        [enc setFragmentBytes:&hu length:sizeof (hu) atIndex:MOPPE_BUF_FRAME];
        MetalDrawListEncoder::play ({ device, enc, pipelines, terrain, frame },
                                    list.vertices (),
                                    list.runs (),
                                    true);
      }

      record_gpu_pass_end (frame, enc);
      [enc endEncoding];
    }

    void MetalRenderer::draw_hud (const DrawList& list) {
      end_scene_encoder ();
      MetalHudPass::draw (
        { m_device, m_pipelines, m_terrain_resources, m_targets, m_frame },
        list);
    }

    void MetalRenderer::request_screenshot (const std::string& path) {
      m_frame.screenshot_path = path;
    }

    void MetalRenderer::end_frame () {
      MOPPE_PROFILE_ZONE ("MetalRenderer::end_frame");
      {
        MOPPE_PROFILE_ZONE ("MetalRenderer::finish_scene_encoding");
        end_scene_encoder (); // in case nothing was drawn
      }

#if !TARGET_OS_IPHONE
      id<MTLBuffer> capture = nil;
      std::size_t capture_row_bytes = 0;
      int capture_width = 0;
      int capture_height = 0;
      const std::string capture_path = m_frame.screenshot_path;
      if (!capture_path.empty () && m_frame.drawable) {
        capture_width = static_cast<int> (m_frame.drawable.texture.width);
        capture_height = static_cast<int> (m_frame.drawable.texture.height);
        capture_row_bytes =
          (static_cast<std::size_t> (capture_width) * 8 + 255) &
          ~static_cast<std::size_t> (255);
        capture =
          [m_device newBufferWithLength:capture_row_bytes * capture_height
                                options:MTLResourceStorageModeShared];
        id<MTLBlitCommandEncoder> blit =
          [m_frame.command_buffer blitCommandEncoder];
        [blit copyFromTexture:m_frame.drawable.texture
                       sourceSlice:0
                       sourceLevel:0
                      sourceOrigin:MTLOriginMake (0, 0, 0)
                        sourceSize:MTLSizeMake (
                                     capture_width, capture_height, 1)
                          toBuffer:capture
                 destinationOffset:0
            destinationBytesPerRow:capture_row_bytes
          destinationBytesPerImage:capture_row_bytes * capture_height];
        [blit endEncoding];
      }
#endif

      if (m_frame.drawable
#if !TARGET_OS_IPHONE
          && capture_path.empty ()
#endif
      )
        [m_frame.command_buffer presentDrawable:m_frame.drawable];

      id<MTLBuffer> timestamp_results = nil;
      const int timestamp_count = m_frame.timestamp_count;
      const std::vector<GpuPass> sample_passes = m_frame.sample_passes;
      if (timestamp_count > 1 && m_frame.timestamp_samples[m_frame.slot]) {
        timestamp_results = m_frame.timestamp_results[m_frame.slot];
        id<MTLBlitCommandEncoder> resolve =
          [m_frame.command_buffer blitCommandEncoder];
        resolve.label = @"Resolve frame timestamps";
        [resolve resolveCounters:m_frame.timestamp_samples[m_frame.slot]
                         inRange:NSMakeRange (0, timestamp_count)
               destinationBuffer:timestamp_results
               destinationOffset:0];
        [resolve endEncoding];
      }

      dispatch_semaphore_t sem = m_frame.inflight;
      std::shared_ptr<FrameTiming> timing =
        m_frame.profile_this_frame ? m_frame_timing : nullptr;
      std::shared_ptr<BenchmarkOutput> benchmark =
        m_frame.params.benchmark_measured ? m_benchmark : nullptr;
      const uint32_t benchmark_mask = m_frame.params.benchmark_mask;
      const uint32_t benchmark_partition_mask =
        m_frame.params.benchmark_partition_mask;
      const uint32_t benchmark_epoch = m_frame.params.benchmark_epoch;
      const uint32_t benchmark_frame = m_frame.params.benchmark_frame;
      [m_frame.command_buffer addCompletedHandler:^(
                                id<MTLCommandBuffer> command) {
        if (benchmark && command.GPUEndTime >= command.GPUStartTime) {
          const BenchmarkSample sample { benchmark_mask,
                                         benchmark_partition_mask,
                                         benchmark_epoch,
                                         benchmark_frame,
                                         1000.0 * (command.GPUEndTime -
                                                   command.GPUStartTime) };
          {
            std::lock_guard<std::mutex> lock (benchmark->mutex);
            benchmark->samples.push_back (sample);
          }
          ++benchmark->completed;
        }
        if (timing && command.GPUEndTime >= command.GPUStartTime) {
          const double gpu_ms =
            1000.0 * (command.GPUEndTime - command.GPUStartTime);
          std::array<double, GPU_PASS_COUNT> pass_ms {};
          if (timestamp_results && timestamp_count > 1) {
            const auto* samples =
              static_cast<const MTLCounterResultTimestamp*> (
                timestamp_results.contents);
            if (sample_passes.size () + 1 == (size_t)timestamp_count) {
              for (int i = 0; i + 1 < timestamp_count; ++i) {
                const uint64_t a = samples[i].timestamp;
                const uint64_t b = samples[i + 1].timestamp;
                if (a != MTLCounterErrorValue && b != MTLCounterErrorValue &&
                    b >= a)
                  pass_ms[static_cast<int> (sample_passes[i])] +=
                    (b - a) / 1000000.0;
              }
            } else if (sample_passes.size () * 2 == (size_t)timestamp_count) {
              for (size_t i = 0; i < sample_passes.size (); ++i) {
                const uint64_t a = samples[i * 2].timestamp;
                const uint64_t b = samples[i * 2 + 1].timestamp;
                if (a != MTLCounterErrorValue && b != MTLCounterErrorValue &&
                    b >= a)
                  pass_ms[static_cast<int> (sample_passes[i])] +=
                    (b - a) / 1000000.0;
              }
            }
          }
          std::lock_guard<std::mutex> lock (timing->mutex);
          if (timing->interval_start == 0)
            timing->interval_start = command.GPUEndTime;
          timing->gpu_total_ms += gpu_ms;
          timing->gpu_min_ms = std::min (timing->gpu_min_ms, gpu_ms);
          timing->gpu_max_ms = std::max (timing->gpu_max_ms, gpu_ms);
          for (int i = 0; i < GPU_PASS_COUNT; ++i)
            timing->pass_total_ms[i] += pass_ms[i];
          ++timing->frames;
          const double elapsed = command.GPUEndTime - timing->interval_start;
          if (elapsed >= 1.0) {
            std::cerr << "frame GPU: " << timing->gpu_total_ms / timing->frames
                      << " ms avg, " << timing->gpu_min_ms << " ms min, "
                      << timing->gpu_max_ms << " ms max (" << timing->frames
                      << " frames)" << std::endl;
            if (timestamp_results) {
              std::cerr << "  encoder spans (may overlap):";
              for (int i = 0; i < GPU_PASS_COUNT; ++i)
                if (timing->pass_total_ms[i] > 0)
                  std::cerr << " " << GPU_PASS_NAMES[i] << "="
                            << timing->pass_total_ms[i] / timing->frames
                            << " ms";
              std::cerr << std::endl;
            }
            timing->interval_start = command.GPUEndTime;
            timing->gpu_total_ms = 0;
            timing->gpu_min_ms = std::numeric_limits<double>::max ();
            timing->gpu_max_ms = 0;
            timing->pass_total_ms.fill (0);
            timing->frames = 0;
          }
        }
        dispatch_semaphore_signal (sem);
      }];
      {
        MOPPE_PROFILE_ZONE ("MetalRenderer::commit_command_buffer");
        [m_frame.command_buffer commit];
      }
      if (m_profile_cpu && m_cpu_frame_start > 0) {
        const double now = cpu_time ();
        if (m_cpu_interval_start == 0)
          m_cpu_interval_start = m_cpu_frame_start;
        m_cpu_encode_total += now - m_cpu_encode_start;
        ++m_cpu_frames;
        const double elapsed = now - m_cpu_interval_start;
        if (elapsed >= 1.0) {
          const double scale = 1000.0 / m_cpu_frames;
          std::cerr << "  renderer CPU: targets=" << m_cpu_targets_total * scale
                    << " ms, inflight-wait=" << m_cpu_inflight_total * scale
                    << " ms, drawable-wait=" << m_cpu_drawable_total * scale
                    << " ms, encode+submit=" << m_cpu_encode_total * scale
                    << " ms" << std::endl;
          m_cpu_interval_start = now;
          m_cpu_targets_total = 0;
          m_cpu_inflight_total = 0;
          m_cpu_drawable_total = 0;
          m_cpu_encode_total = 0;
          m_cpu_frames = 0;
        }
      }

#if !TARGET_OS_IPHONE
      if (m_frame.capture_active && m_frame.profile_this_frame &&
          ++m_frame.capture_frames >= m_frame.capture_frame_limit) {
        [m_frame.command_buffer waitUntilCompleted];
        [[MTLCaptureManager sharedCaptureManager] stopCapture];
        m_frame.capture_active = false;
        std::cerr << "moppe: wrote Metal capture " << m_frame.capture_path
                  << std::endl;
      }
#endif

#if !TARGET_OS_IPHONE
      if (capture) {
        [m_frame.command_buffer waitUntilCompleted];
        if (!write_capture_png (capture_path,
                                capture_width,
                                capture_height,
                                capture_row_bytes,
                                capture.contents))
          std::cerr << "moppe: failed to write screenshot " << capture_path
                    << std::endl;
        else
          std::cout << "moppe: wrote screenshot " << capture_path << std::endl;
      }
      m_frame.screenshot_path.clear ();
#endif

      m_frame.command_buffer = nil;
      m_frame.drawable = nil;
      m_frame.current_scene = nil;
    }

    bool MetalRenderer::benchmark_complete () const {
      return m_benchmark && m_benchmark->expected > 0 &&
             m_benchmark->completed.load () >= m_benchmark->expected;
    }

    void MetalRenderer::reset_temporal_state () {
      // Epoch changes restore CPU state while previous frames may still be in
      // flight. A queue fence keeps their exposure blits from racing this
      // reset; transition time is outside the measured frame block.
      id<MTLCommandBuffer> fence = [m_queue commandBuffer];
      [fence commit];
      [fence waitUntilCompleted];
      m_targets.prev_valid = false;
      m_targets.exposure = 1.0f;
      for (id<MTLBuffer> buffer : m_targets.probe_buf)
        if (buffer)
          std::memset (buffer.contents, 0, buffer.length);
    }

    void MetalRenderer::write_benchmark_results () {
      if (!m_benchmark || m_benchmark->path.empty ())
        return;
      std::vector<BenchmarkSample> samples;
      {
        std::lock_guard<std::mutex> lock (m_benchmark->mutex);
        samples = m_benchmark->samples;
      }
      std::sort (
        samples.begin (), samples.end (), [] (const auto& a, const auto& b) {
          return a.epoch == b.epoch ? a.frame < b.frame : a.epoch < b.epoch;
        });
      std::ofstream output (m_benchmark->path);
      output << "epoch,mask,partition_mask,logical_frame,gpu_ms";
      for (const std::string& name : m_benchmark->feature_names)
        output << ',' << name;
      output << '\n';
      for (const BenchmarkSample& sample : samples) {
        output << sample.epoch << ',' << sample.mask << ','
               << sample.partition_mask << ',' << sample.frame << ','
               << sample.gpu_ms;
        for (std::size_t bit = 0; bit < m_benchmark->feature_names.size ();
             ++bit)
          output << ',' << ((sample.mask & (1u << bit)) ? 1 : 0);
        output << '\n';
      }
      std::cerr << "moppe: wrote " << samples.size ()
                << " graphics benchmark samples to " << m_benchmark->path
                << std::endl;
    }

    // ------------------------------------------------------------------

    Renderer* create_metal_renderer (void* mtk_view,
                                     const std::string& lib_path) {
      return new MetalRenderer ((__bridge MTKView*)mtk_view, lib_path);
    }

    void set_metal_drawable (Renderer& renderer, void* drawable) {
      MetalRenderer& metal = static_cast<MetalRenderer&> (renderer);
      metal.set_next_drawable ((__bridge id<CAMetalDrawable>)drawable);
    }
  }
}
