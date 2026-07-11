// Metal backend for moppe/render.  One command buffer per frame:
//
//   scene pass   MSAA 4x -> resolve into sceneA (reversed-Z depth)
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

#include <moppe/render/metal/metal_renderer.hh>
#include <moppe/render/metal/shader_types.h>

#include <algorithm>
#include <array>
#include <bit>
#include <chrono>
#include <cmath>
#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <iostream>
#include <limits>
#include <memory>
#include <mutex>
#include <vector>

namespace moppe {
  namespace render {
    namespace {
      const int FRAMES_IN_FLIGHT = 3;
      const int MSAA_SAMPLES = 4;
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

      float scene_render_scale (float backing_scale) {
#if TARGET_OS_IPHONE
        (void)backing_scale;
        return 1.0f;
#else
        float scale = 1.0f / std::max (1.0f, backing_scale);
        if (const char* requested = std::getenv ("MOPPE_RENDERSCALE"))
          scale = static_cast<float> (std::atof (requested));
        return std::clamp (scale, 0.25f, 1.0f);
#endif
      }

      enum class GpuPass {
        Terrain,
        Sky,
        Water,
        Grass,
        Scene,
        Post,
        Bloom,
        Exposure,
        Present,
        Count
      };
      constexpr int GPU_PASS_COUNT = static_cast<int> (GpuPass::Count);
      const char* GPU_PASS_NAMES[GPU_PASS_COUNT] = {
        "terrain", "sky",   "water",    "grass",  "scene",
        "post",    "bloom", "exposure", "present"
      };

      struct FrameTiming {
        std::mutex mutex;
        double interval_start = 0;
        double gpu_total_ms = 0;
        double gpu_min_ms = std::numeric_limits<double>::max ();
        double gpu_max_ms = 0;
        std::array<double, GPU_PASS_COUNT> pass_total_ms {};
        int frames = 0;
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

      MoppeFloat4 f4 (const Vector3D& v, float w = 0.0f) {
        MoppeFloat4 r;
        r.x = v.x;
        r.y = v.y;
        r.z = v.z;
        r.w = w;
        return r;
      }

      // Game-side colors are authored in display space; the scene
      // lights in linear.  Decode at the uniform boundary.
      MoppeFloat4 f4lin (const Vector3D& v, float w = 0.0f) {
        MoppeFloat4 r;
        r.x = std::pow (std::max (v.x, 0.0f), 2.2f);
        r.y = std::pow (std::max (v.y, 0.0f), 2.2f);
        r.z = std::pow (std::max (v.z, 0.0f), 2.2f);
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
    }

    class MetalRenderer : public Renderer {
    public:
      MetalRenderer (MTKView* view, const std::string& lib_path);

      // resources
      TexturePtr create_texture (const TextureDesc& desc,
                                 const void* pixels) override;
      MeshPtr create_mesh (const DrawList& recorded) override;

      // world setup
      void set_terrain (const TerrainParams& params,
                        const float* heights,
                        const Vector3D* normals) override;
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

      // frame
      bool begin_frame (const FrameParams& params) override;
      void draw_terrain (const ChunkDraw* chunks, int count) override;
      void draw_sky (const SkyParams& params) override;
      void draw_ocean (const OceanParams& params) override;
      void draw_grass (const GrassParams& params) override;
      void draw_rivers (const Mesh& mesh, const Mat4& model) override;
      void draw_mesh (const Mesh& mesh, const Mat4& model) override;
      void draw_list (const DrawList& list) override;
      void apply_underwater (float time) override;
      void apply_motion_blur (float strength) override;
      void apply_scene_blur () override;
      void draw_hud (const DrawList& list) override;
      void request_screenshot (const std::string& path) override;
      void end_frame () override;

      int width_pts () const override {
        return m_width_pts;
      }
      int height_pts () const override {
        return m_height_pts;
      }
      float scale_factor () const override {
        return m_scale;
      }

    private:
      void build_pipelines ();
      void ensure_targets ();
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
      void play_list (id<MTLRenderCommandEncoder> enc,
                      const std::vector<Vertex>& verts,
                      const std::vector<DrawList::Run>& runs,
                      bool hud);
      size_t stream_vertices (const std::vector<Vertex>& verts);
      void set_run_state (id<MTLRenderCommandEncoder> enc,
                          const DrawState& s,
                          const Texture* tex,
                          bool hud);
      void begin_gpu_pass (id<MTLRenderCommandEncoder> enc, GpuPass pass);
      void finish_gpu_pass (id<MTLRenderCommandEncoder> enc);
      void attach_gpu_pass (MTLRenderPassDescriptor* desc, GpuPass pass);

      MTKView* m_view;
      id<MTLDevice> m_device;
      id<MTLCommandQueue> m_queue;
      id<MTLLibrary> m_library;

      // pipelines
      id<MTLRenderPipelineState> m_uber_opaque = nil, m_uber_blend = nil;
      id<MTLRenderPipelineState> m_uber_add = nil;
      id<MTLRenderPipelineState> m_hud = nil;
      id<MTLRenderPipelineState> m_present = nil, m_ghost = nil;
      id<MTLRenderPipelineState> m_copy = nil;
      id<MTLRenderPipelineState> m_underwater = nil;
      id<MTLRenderPipelineState> m_bloom_bright = nil;
      id<MTLRenderPipelineState> m_bloom_blur = nil;
      id<MTLRenderPipelineState> m_probe = nil;
      id<MTLRenderPipelineState> m_terrain = nil, m_terrain_shadow = nil;
      id<MTLRenderPipelineState> m_sky = nil, m_ocean = nil;
      id<MTLRenderPipelineState> m_grass_pipeline = nil;
      id<MTLRenderPipelineState> m_grass_mesh_pipeline = nil;
      id<MTLRenderPipelineState> m_water_tiles_pipeline = nil;
      bool m_mesh_shaders_ok = false;
      id<MTLRenderPipelineState> m_river = nil;

      // depth-stencil: index [test][write], reversed-Z (>=)
      id<MTLDepthStencilState> m_ds[2][2];
      id<MTLDepthStencilState> m_ds_shadow = nil;
      id<MTLDepthStencilState> m_ds_river = nil;

      id<MTLSamplerState> m_sampler_repeat = nil; // mip, aniso
      id<MTLSamplerState> m_sampler_clamp = nil;
      TexturePtr m_white;

      // render targets (scene-scaled; bloom at quarter scene resolution)
      id<MTLTexture> m_msaa_color = nil, m_msaa_depth = nil;
      id<MTLTexture> m_scene_a = nil, m_scene_b = nil;
      id<MTLTexture> m_prev_frame = nil;
      id<MTLTexture> m_bloom_a = nil, m_bloom_b = nil;
      bool m_prev_valid = false;

      // Auto exposure: a tiny luminance probe of each frame is read
      // back FRAMES_IN_FLIGHT frames later (safe by the semaphore)
      // and drives a smoothed exposure factor.
      id<MTLTexture> m_probe_tex = nil;
      id<MTLBuffer> m_probe_buf[FRAMES_IN_FLIGHT];
      float m_exposure = 1.0f;
      float m_edr_headroom = 1.0f;
      int m_target_w = 0, m_target_h = 0;
      bool m_memoryless_ok = false;

      // shadow map
      id<MTLTexture> m_shadow_map = nil;
      id<MTLTexture> m_previous_shadow_map = nil;
      Mat4 m_light_biased;
      bool m_have_shadow = false;

      // terrain
      id<MTLTexture> m_heights = nil, m_previous_heights = nil;
      id<MTLTexture> m_normals = nil;
      float m_height_transition_start = 0.0f;
      bool m_height_transition_active = false;
      uint64_t m_height_transition_generation = 0;
      uint64_t m_shadow_transition_generation = 0;
      id<MTLBuffer> m_terrain_indices[TERRAIN_LOD_COUNT];
      uint32_t m_terrain_index_count[TERRAIN_LOD_COUNT];
      TerrainParams m_terrain_params;
      bool m_have_terrain = false;
      TexturePtr m_grass, m_dirt, m_rock, m_snow;
      id<MTLTexture> m_terrain_overlay = nil;
      TerrainOverlayParams m_terrain_overlay_params {};
      bool m_have_terrain_overlay = false;

      // sky dome / ocean grid
      id<MTLBuffer> m_sky_verts = nil;
      uint32_t m_sky_vcount = 0;
      id<MTLBuffer> m_ocean_verts = nil;
      uint32_t m_ocean_vcount = 0;
      float m_ocean_level = 0;
      id<MTLTexture> m_water_levels = nil;
      bool m_have_water_levels = false;
      id<MTLTexture> m_water_flow = nil;
      bool m_have_water_flow = false;
      id<MTLTexture> m_moisture = nil;
      bool m_have_moisture = false;

      // streaming
      dispatch_semaphore_t m_inflight;
      int m_slot = 0;
      id<MTLBuffer> m_stream[FRAMES_IN_FLIGHT];
      size_t m_stream_used = 0;

      // per-frame state
      id<MTLCommandBuffer> m_cmd = nil;
      id<MTLRenderCommandEncoder> m_enc = nil;
      id<CAMetalDrawable> m_drawable = nil;
      id<MTLTexture> m_current = nil; // scene_a or scene_b
      bool m_scene_pass_done = false;
      FrameParams m_fp;
      MoppeFrameUniforms m_fu;
      bool m_profile_this_frame = false;
      id<MTLCounterSampleBuffer> m_timestamp_samples[FRAMES_IN_FLIGHT] {};
      id<MTLBuffer> m_timestamp_results[FRAMES_IN_FLIGHT] {};
      std::vector<GpuPass> m_sample_passes;
      int m_timestamp_count = 0;
      GpuPass m_current_gpu_pass = GpuPass::Count;
      bool m_draw_boundary_timestamps = false;
      bool m_stage_boundary_timestamps = false;
      bool m_logged_initial_targets = false;

      int m_width_pts = 0, m_height_pts = 0;
      float m_scale = 1.0f;
      std::string m_screenshot_path;
      bool m_profile_gpu = false;
      std::shared_ptr<FrameTiming> m_frame_timing;
      bool m_profile_cpu = false;
      double m_cpu_frame_start = 0;
      double m_cpu_encode_start = 0;
      double m_cpu_interval_start = 0;
      double m_cpu_targets_total = 0;
      double m_cpu_inflight_total = 0;
      double m_cpu_drawable_total = 0;
      double m_cpu_encode_total = 0;
      int m_cpu_frames = 0;
#if !TARGET_OS_IPHONE
      bool m_capture_active = false;
      int m_capture_frames = 0;
      int m_capture_frame_limit = 120;
      std::string m_capture_path;
#endif
    };

    // ------------------------------------------------------------------

    MetalRenderer::MetalRenderer (MTKView* view, const std::string& lib_path) {
      m_view = view;
      m_device = view.device ? view.device : MTLCreateSystemDefaultDevice ();
      m_queue = [m_device newCommandQueue];

      m_profile_gpu = ::getenv ("MOPPE_PROFILE_GPU") != nullptr;
      m_profile_cpu = ::getenv ("MOPPE_PROFILE_CPU") != nullptr;
      if (m_profile_gpu)
        m_frame_timing = std::make_shared<FrameTiming> ();

      if (m_profile_gpu) {
        if (@available (macOS 11.0, iOS 14.0, *)) {
          id<MTLCounterSet> timestamps = nil;
          for (id<MTLCounterSet> set in m_device.counterSets)
            if ([set.name isEqualToString:MTLCommonCounterSetTimestamp]) {
              timestamps = set;
              break;
            }
          m_draw_boundary_timestamps =
            timestamps &&
            [m_device
              supportsCounterSampling:MTLCounterSamplingPointAtDrawBoundary];
          m_stage_boundary_timestamps =
            timestamps &&
            [m_device
              supportsCounterSampling:MTLCounterSamplingPointAtStageBoundary];
          if (timestamps &&
              (m_draw_boundary_timestamps || m_stage_boundary_timestamps)) {
            if (!m_draw_boundary_timestamps && m_stage_boundary_timestamps)
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
              m_timestamp_samples[i] =
                [m_device newCounterSampleBufferWithDescriptor:desc
                                                         error:&error];
              m_timestamp_results[i] =
                [m_device newBufferWithLength:MAX_TIMESTAMP_SAMPLES *
                                              sizeof (MTLCounterResultTimestamp)
                                      options:MTLResourceStorageModeShared];
              if (!m_timestamp_samples[i]) {
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
        m_capture_path = requested;
        if (const char* frames = ::getenv ("MOPPE_METAL_CAPTURE_FRAMES"))
          m_capture_frame_limit = std::max (1, ::atoi (frames));
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
        m_scale = (float)view.window.backingScaleFactor;
#else
      m_scale = (float)view.contentScaleFactor;
#endif

#if TARGET_OS_SIMULATOR
      m_memoryless_ok = false;
#else
      m_memoryless_ok = [m_device supportsFamily:MTLGPUFamilyApple2];
#endif
      if (@available (macOS 13.0, iOS 16.0, *))
        m_mesh_shaders_ok = [m_device supportsFamily:MTLGPUFamilyMetal3];

      std::cerr << "moppe: Metal: device=" << m_device.name.UTF8String
                << ", frames-in-flight=" << FRAMES_IN_FLIGHT
                << ", memoryless=" << (m_memoryless_ok ? "yes" : "no")
                << ", mesh-shaders=" << (m_mesh_shaders_ok ? "yes" : "no")
                << std::endl;

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

      m_inflight = dispatch_semaphore_create (FRAMES_IN_FLIGHT);
      for (int i = 0; i < FRAMES_IN_FLIGHT; ++i)
        m_stream[i] = nil;

      // 1x1 white fallback texture.
      TextureDesc wd;
      wd.width = wd.height = 1;
      wd.format = TextureFormat::RGBA8;
      wd.filter = TextureFilter::Nearest;
      const uint8_t white[4] = { 255, 255, 255, 255 };
      m_white = create_texture (wd, white);
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

      m_uber_opaque = make_pipeline (
        @"uber_vertex", @"uber_fragment", scene, depth, MSAA_SAMPLES, false);
      m_uber_blend = make_pipeline (
        @"uber_vertex", @"uber_fragment", scene, depth, MSAA_SAMPLES, true);
      m_uber_add = make_pipeline (@"uber_vertex",
                                  @"uber_fragment",
                                  scene,
                                  depth,
                                  MSAA_SAMPLES,
                                  true,
                                  true);
      m_hud = make_pipeline (@"hud_vertex",
                             @"hud_fragment",
                             drawable,
                             MTLPixelFormatInvalid,
                             1,
                             true);
      m_present = make_pipeline (@"quad_vertex",
                                 @"present_fragment",
                                 drawable,
                                 MTLPixelFormatInvalid,
                                 1,
                                 false);
      m_ghost = make_pipeline (@"quad_vertex",
                               @"quad_fragment",
                               scene,
                               MTLPixelFormatInvalid,
                               1,
                               true);
      m_copy = make_pipeline (@"quad_vertex",
                              @"quad_fragment",
                              scene,
                              MTLPixelFormatInvalid,
                              1,
                              false);
      m_underwater = make_pipeline (@"quad_vertex",
                                    @"underwater_fragment",
                                    scene,
                                    MTLPixelFormatInvalid,
                                    1,
                                    false);
      m_bloom_bright = make_pipeline (@"quad_vertex",
                                      @"bloom_bright_fragment",
                                      scene,
                                      MTLPixelFormatInvalid,
                                      1,
                                      false);
      m_bloom_blur = make_pipeline (@"quad_vertex",
                                    @"bloom_blur_fragment",
                                    scene,
                                    MTLPixelFormatInvalid,
                                    1,
                                    false);
      m_probe = make_pipeline (@"quad_vertex",
                               @"probe_fragment",
                               MTLPixelFormatRGBA32Float,
                               MTLPixelFormatInvalid,
                               1,
                               false);
      m_terrain = make_pipeline (@"terrain_vertex",
                                 @"terrain_fragment",
                                 scene,
                                 depth,
                                 MSAA_SAMPLES,
                                 false);
      m_terrain_shadow = make_pipeline (@"terrain_shadow_vertex",
                                        nil,
                                        MTLPixelFormatInvalid,
                                        MTLPixelFormatDepth16Unorm,
                                        1,
                                        false);
      m_sky = make_pipeline (
        @"sky_vertex", @"sky_fragment", scene, depth, MSAA_SAMPLES, false);
      m_ocean = make_pipeline (
        @"ocean_vertex", @"ocean_fragment", scene, depth, MSAA_SAMPLES, true);
      if (m_mesh_shaders_ok) {
        if (@available (macOS 13.0, iOS 16.0, *)) {
          // The mesh grass path: an object stage culls patches and a mesh
          // stage emits the surviving blades. Falls back to the instanced
          // vertex pipeline when compilation or hardware support fails.
          MTLMeshRenderPipelineDescriptor* d =
            [[MTLMeshRenderPipelineDescriptor alloc] init];
          d.objectFunction = [m_library newFunctionWithName:@"grass_object"];
          d.meshFunction = [m_library newFunctionWithName:@"grass_mesh"];
          d.fragmentFunction =
            [m_library newFunctionWithName:@"grass_fragment"];
          d.rasterSampleCount = MSAA_SAMPLES;
          d.colorAttachments[0].pixelFormat = scene;
          d.depthAttachmentPixelFormat = depth;
          d.stencilAttachmentPixelFormat = depth;
          d.payloadMemoryLength = 1024;
          d.maxTotalThreadsPerObjectThreadgroup = 64;
          d.maxTotalThreadsPerMeshThreadgroup = 32;
          if (d.objectFunction && d.meshFunction && d.fragmentFunction) {
            NSError* error = nil;
            m_grass_mesh_pipeline = [m_device
              newRenderPipelineStateWithMeshDescriptor:d
                                               options:MTLPipelineOptionNone
                                            reflection:nil
                                                 error:&error];
            if (!m_grass_mesh_pipeline)
              std::cerr << "moppe: grass mesh pipeline failed: "
                        << (error ? error.localizedDescription.UTF8String : "?")
                        << std::endl;
          }

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
            m_water_tiles_pipeline = [m_device
              newRenderPipelineStateWithMeshDescriptor:w
                                               options:MTLPipelineOptionNone
                                            reflection:nil
                                                 error:&error];
            if (!m_water_tiles_pipeline)
              std::cerr << "moppe: water tile pipeline failed: "
                        << (error ? error.localizedDescription.UTF8String : "?")
                        << std::endl;
          }
        }
      }
      m_grass_pipeline = make_pipeline (
        @"grass_vertex", @"grass_fragment", scene, depth, MSAA_SAMPLES, false);
      m_river = make_pipeline (
        @"river_vertex", @"river_fragment", scene, depth, MSAA_SAMPLES, true);

      // Depth-stencil states, reversed-Z.
      for (int test = 0; test < 2; ++test)
        for (int write = 0; write < 2; ++write) {
          MTLDepthStencilDescriptor* d =
            [[MTLDepthStencilDescriptor alloc] init];
          d.depthCompareFunction =
            test ? MTLCompareFunctionGreaterEqual : MTLCompareFunctionAlways;
          d.depthWriteEnabled = write ? YES : NO;
          m_ds[test][write] = [m_device newDepthStencilStateWithDescriptor:d];
        }
      {
        MTLDepthStencilDescriptor* d = [[MTLDepthStencilDescriptor alloc] init];
        d.depthCompareFunction = MTLCompareFunctionLessEqual;
        d.depthWriteEnabled = YES;
        m_ds_shadow = [m_device newDepthStencilStateWithDescriptor:d];
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
        m_ds_river = [m_device newDepthStencilStateWithDescriptor:d];
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
        m_sampler_repeat = [m_device newSamplerStateWithDescriptor:d];
        d.sAddressMode = MTLSamplerAddressModeClampToEdge;
        d.tAddressMode = MTLSamplerAddressModeClampToEdge;
        d.maxAnisotropy = 1;
        m_sampler_clamp = [m_device newSamplerStateWithDescriptor:d];
      }
    }

    // -- resources -----------------------------------------------------

    void MetalRenderer::upload_texture (id<MTLTexture> tex,
                                        const void* pixels,
                                        int w,
                                        int h,
                                        int bpp,
                                        bool gen_mips) {
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
                                     const Vector3D* normals) {
      const int w = params.width, h = params.height;
      const bool transition = params.derive_normals && m_have_terrain &&
                              m_heights && m_heights.width == (NSUInteger)w &&
                              m_heights.height == (NSUInteger)h;
      const bool continue_transition =
        transition && m_height_transition_active && m_previous_heights;
      if (transition && !continue_transition)
        ++m_height_transition_generation;

      if (transition && !continue_transition) {
        id<MTLTexture> old_heights = m_heights;
        m_heights = m_previous_heights;
        m_previous_heights = old_heights;
      } else if (!transition) {
        m_previous_heights = nil;
      }
      m_terrain_params = params;

      // Heights: R32Float, read() access only.
      if (!m_heights || m_heights.width != (NSUInteger)w ||
          m_heights.height != (NSUInteger)h) {
        MTLTextureDescriptor* td = [MTLTextureDescriptor
          texture2DDescriptorWithPixelFormat:MTLPixelFormatR32Float
                                       width:w
                                      height:h
                                   mipmapped:NO];
        td.storageMode = MTLStorageModePrivate;
        td.usage = MTLTextureUsageShaderRead;
        m_heights = [m_device newTextureWithDescriptor:td];
      }
      upload_texture (m_heights, heights, w, h, 4, false);
      if (transition && !continue_transition) {
        m_height_transition_start = m_fp.time;
        m_height_transition_active = true;
      } else if (!transition) {
        m_height_transition_active = false;
      }

      // Normals: RG16Snorm xz, y reconstructed in the shader.
      if (!params.derive_normals) {
        std::vector<int16_t> packed ((size_t)w * h * 2);
        for (size_t i = 0; i < (size_t)w * h; ++i) {
          const Vector3D& n = normals[i];
          float x = n.x, z = n.z;
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
        if (!m_normals || m_normals.width != (NSUInteger)w ||
            m_normals.height != (NSUInteger)h) {
          MTLTextureDescriptor* td = [MTLTextureDescriptor
            texture2DDescriptorWithPixelFormat:MTLPixelFormatRG16Snorm
                                         width:w
                                        height:h
                                     mipmapped:NO];
          td.storageMode = MTLStorageModePrivate;
          td.usage = MTLTextureUsageShaderRead;
          m_normals = [m_device newTextureWithDescriptor:td];
        }
        upload_texture (m_normals, packed.data (), w, h, 4, false);
      } else if (!m_normals) {
        const int16_t up[2] = { 0, 0 };
        MTLTextureDescriptor* td = [MTLTextureDescriptor
          texture2DDescriptorWithPixelFormat:MTLPixelFormatRG16Snorm
                                       width:1
                                      height:1
                                   mipmapped:NO];
        td.storageMode = MTLStorageModePrivate;
        td.usage = MTLTextureUsageShaderRead;
        m_normals = [m_device newTextureWithDescriptor:td];
        upload_texture (m_normals, up, 1, 1, 4, false);
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
        if (!m_terrain_indices[lod])
          m_terrain_indices[lod] = Gen::make (
            m_device, TERRAIN_LOD_VERTS[lod], m_terrain_index_count[lod]);

      m_have_terrain = true;
    }

    void MetalRenderer::set_terrain_textures (TexturePtr grass,
                                              TexturePtr dirt,
                                              TexturePtr rock,
                                              TexturePtr snow) {
      m_grass = grass;
      m_dirt = dirt;
      m_rock = rock;
      m_snow = snow;
    }

    void MetalRenderer::set_terrain_overlay (const TerrainOverlayParams& params,
                                             std::span<const float> values) {
      if (params.width < 1 || params.height < 1 ||
          values.size () !=
            static_cast<std::size_t> (params.width) * params.height)
        throw std::invalid_argument ("invalid terrain overlay raster");
      if (!m_terrain_overlay ||
          m_terrain_overlay.width != (NSUInteger)params.width ||
          m_terrain_overlay.height != (NSUInteger)params.height) {
        MTLTextureDescriptor* td = [MTLTextureDescriptor
          texture2DDescriptorWithPixelFormat:MTLPixelFormatR32Float
                                       width:params.width
                                      height:params.height
                                   mipmapped:NO];
        td.storageMode = MTLStorageModePrivate;
        td.usage = MTLTextureUsageShaderRead;
        m_terrain_overlay = [m_device newTextureWithDescriptor:td];
      }
      upload_texture (m_terrain_overlay,
                      values.data (),
                      params.width,
                      params.height,
                      4,
                      false);
      m_terrain_overlay_params = params;
      m_have_terrain_overlay = true;
    }

    void MetalRenderer::clear_terrain_overlay () {
      m_have_terrain_overlay = false;
    }

    void MetalRenderer::render_terrain_shadow (const Mat4& light_view_proj) {
      if (!m_terrain_shadow || !m_have_terrain)
        return;

      const int shadow_size =
        std::max (256, m_terrain_params.shadow_resolution);
      const bool new_shadow_transition =
        m_terrain_params.derive_normals &&
        m_shadow_transition_generation != m_height_transition_generation;
      if (new_shadow_transition && m_shadow_map) {
        id<MTLTexture> old_shadow = m_shadow_map;
        m_shadow_map = m_previous_shadow_map;
        m_previous_shadow_map = old_shadow;
      } else if (!m_terrain_params.derive_normals) {
        m_previous_shadow_map = nil;
      }

      if (!m_shadow_map || m_shadow_map.width != (NSUInteger)shadow_size ||
          m_shadow_map.height != (NSUInteger)shadow_size) {
        MTLTextureDescriptor* td = [MTLTextureDescriptor
          texture2DDescriptorWithPixelFormat:MTLPixelFormatDepth16Unorm
                                       width:shadow_size
                                      height:shadow_size
                                   mipmapped:NO];
        td.storageMode = MTLStorageModePrivate;
        td.usage = MTLTextureUsageRenderTarget | MTLTextureUsageShaderRead;
        m_shadow_map = [m_device newTextureWithDescriptor:td];
      }

      // Bias: xy from NDC [-1,1] to uv [0,1], with the Metal
      // texture-space Y flip; z is already [0,1].
      Mat4 bias;
      bias.m[0] = 0.5f;
      bias.m[5] = -0.5f;
      bias.m[12] = 0.5f;
      bias.m[13] = 0.5f;
      m_light_biased = bias * light_view_proj;

      MTLRenderPassDescriptor* rp =
        [MTLRenderPassDescriptor renderPassDescriptor];
      rp.depthAttachment.texture = m_shadow_map;
      rp.depthAttachment.loadAction = MTLLoadActionClear;
      rp.depthAttachment.storeAction = MTLStoreActionStore;
      rp.depthAttachment.clearDepth = 1.0;

      id<MTLCommandBuffer> cmd = [m_queue commandBuffer];
      id<MTLRenderCommandEncoder> enc =
        [cmd renderCommandEncoderWithDescriptor:rp];
      [enc setRenderPipelineState:m_terrain_shadow];
      [enc setDepthStencilState:m_ds_shadow];
      [enc setCullMode:MTLCullModeNone];
      [enc setFrontFacingWinding:MTLWindingCounterClockwise];
      // Port of glPolygonOffset(2,2) in the GL shadow pass.
      [enc setDepthBias:2.0f slopeScale:2.0f clamp:0.0f];

      MoppeTerrainUniforms u;
      std::memset (&u, 0, sizeof (u));
      u.view_proj = m4 (light_view_proj);
      u.params0.x = m_terrain_params.scale.x;
      u.params0.y = m_terrain_params.scale.y;
      u.params0.z = m_terrain_params.scale.z;
      [enc setVertexBytes:&u length:sizeof (u) atIndex:MOPPE_BUF_FRAME];
      [enc setVertexTexture:m_heights atIndex:MOPPE_TEX_HEIGHTS];

      const int requested_step =
        std::max (1, m_terrain_params.shadow_sample_step);
      int shadow_lod = TERRAIN_NATIVE_LOD;
      while (shadow_lod + 1 < TERRAIN_LOD_COUNT &&
             TERRAIN_LOD_STEP[shadow_lod] < requested_step)
        ++shadow_lod;
      const float shadow_step = TERRAIN_LOD_STEP[shadow_lod];
      const int chunks = (m_terrain_params.width - 1) / CHUNK_CELLS;
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
                          indexCount:m_terrain_index_count[shadow_lod]
                           indexType:MTLIndexTypeUInt32
                         indexBuffer:m_terrain_indices[shadow_lod]
                   indexBufferOffset:0];
        }
      [enc endEncoding];
      [cmd commit];
      [cmd waitUntilCompleted];
      if (::getenv ("MOPPE_PROFILE_SHADOW")) {
        const double gpu_ms = 1000.0 * (cmd.GPUEndTime - cmd.GPUStartTime);
        std::cerr << "terrain shadow: " << shadow_size << "px, step "
                  << shadow_step << ", " << gpu_ms << " ms GPU\n";
      }
      m_have_shadow = true;
      m_shadow_transition_generation = m_height_transition_generation;
    }

    void MetalRenderer::set_ocean (const OceanSetup& setup,
                                   std::span<const float> water_levels) {
      // Regular triangle water grid. The vertex shader samples the
      // optional standing-water surface and does the waving. Built as plain
      // triangles to keep sea and lakes in one draw.
      const int cells = setup.cells;
      const float step = 2 * setup.half_extent / cells;
      const float x0 = setup.center.x - setup.half_extent;
      const float z0 = setup.center.z - setup.half_extent;

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

      m_ocean_level = setup.level;
      m_ocean_vcount = (uint32_t)(verts.size () / 3);
      m_ocean_verts =
        [m_device newBufferWithBytes:verts.data ()
                              length:verts.size () * sizeof (float)
                             options:MTLResourceStorageModeShared];

      // Interleaved (level, wave amplitude) pairs per terrain sample.
      const std::size_t expected =
        2 * static_cast<std::size_t> (m_terrain_params.width) *
        m_terrain_params.height;
      m_have_water_levels = water_levels.size () == expected;
      if (m_have_water_levels) {
        const int width = m_terrain_params.width;
        const int height = m_terrain_params.height;
        if (!m_water_levels ||
            m_water_levels.width != static_cast<NSUInteger> (width) ||
            m_water_levels.height != static_cast<NSUInteger> (height)) {
          MTLTextureDescriptor* td = [MTLTextureDescriptor
            texture2DDescriptorWithPixelFormat:MTLPixelFormatRG32Float
                                         width:width
                                        height:height
                                     mipmapped:NO];
          td.storageMode = MTLStorageModePrivate;
          td.usage = MTLTextureUsageShaderRead;
          m_water_levels = [m_device newTextureWithDescriptor:td];
        }
        upload_texture (
          m_water_levels, water_levels.data (), width, height, 8, false);
      }
    }

    void MetalRenderer::set_water_flow (std::span<const float> flow) {
      // Interleaved (x, z) meters per second per terrain sample; half
      // precision carries river currents with headroom to spare.
      const std::size_t expected =
        2 * static_cast<std::size_t> (m_terrain_params.width) *
        m_terrain_params.height;
      m_have_water_flow = flow.size () == expected;
      if (!m_have_water_flow)
        return;
      const int width = m_terrain_params.width;
      const int height = m_terrain_params.height;
      if (!m_water_flow ||
          m_water_flow.width != static_cast<NSUInteger> (width) ||
          m_water_flow.height != static_cast<NSUInteger> (height)) {
        MTLTextureDescriptor* td = [MTLTextureDescriptor
          texture2DDescriptorWithPixelFormat:MTLPixelFormatRG16Float
                                       width:width
                                      height:height
                                   mipmapped:NO];
        td.storageMode = MTLStorageModePrivate;
        td.usage = MTLTextureUsageShaderRead;
        m_water_flow = [m_device newTextureWithDescriptor:td];
      }
      std::vector<__fp16> halves (flow.size ());
      for (std::size_t i = 0; i < flow.size (); ++i)
        halves[i] = static_cast<__fp16> (flow[i]);
      upload_texture (m_water_flow, halves.data (), width, height, 4, false);
    }

    void MetalRenderer::set_terrain_moisture (std::span<const float> moisture) {
      const std::size_t expected =
        static_cast<std::size_t> (m_terrain_params.width) *
        m_terrain_params.height;
      m_have_moisture = moisture.size () == expected;
      if (!m_have_moisture)
        return;
      const int width = m_terrain_params.width;
      const int height = m_terrain_params.height;
      if (!m_moisture || m_moisture.width != static_cast<NSUInteger> (width) ||
          m_moisture.height != static_cast<NSUInteger> (height)) {
        MTLTextureDescriptor* td = [MTLTextureDescriptor
          texture2DDescriptorWithPixelFormat:MTLPixelFormatR32Float
                                       width:width
                                      height:height
                                   mipmapped:NO];
        td.storageMode = MTLStorageModePrivate;
        td.usage = MTLTextureUsageShaderRead;
        m_moisture = [m_device newTextureWithDescriptor:td];
      }
      upload_texture (m_moisture, moisture.data (), width, height, 4, false);
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

    void MetalRenderer::ensure_targets () {
      const int drawable_w = (int)m_view.drawableSize.width;
      const int drawable_h = (int)m_view.drawableSize.height;
      if (drawable_w == 0 || drawable_h == 0)
        return;
      const CGSize points = m_view.bounds.size;
      const float backing_scale =
        points.width > 0 ? drawable_w / (float)points.width : 1.0f;
      const float scale = scene_render_scale (backing_scale);
      const int w = std::max (1, (int)std::round (drawable_w * scale));
      const int h = std::max (1, (int)std::round (drawable_h * scale));
      if (w == m_target_w && h == m_target_h && m_msaa_color)
        return;

      m_target_w = w;
      m_target_h = h;
      m_msaa_color =
        make_target (MTLPixelFormatRGBA16Float, w, h, MSAA_SAMPLES, true);
      m_msaa_depth = make_target (
        MTLPixelFormatDepth32Float_Stencil8, w, h, MSAA_SAMPLES, true);
      m_scene_a = make_target (MTLPixelFormatRGBA16Float, w, h, 1, false);
      m_scene_b = make_target (MTLPixelFormatRGBA16Float, w, h, 1, false);
      m_prev_frame = make_target (MTLPixelFormatRGBA16Float, w, h, 1, false);
      const int bw = w / 4 > 0 ? w / 4 : 1;
      const int bh = h / 4 > 0 ? h / 4 : 1;
      m_bloom_a = make_target (MTLPixelFormatRGBA16Float, bw, bh, 1, false);
      m_bloom_b = make_target (MTLPixelFormatRGBA16Float, bw, bh, 1, false);
      if (!m_logged_initial_targets) {
        std::cerr << "moppe: render targets: drawable=" << drawable_w << 'x'
                  << drawable_h << ", scene=" << w << 'x' << h
                  << ", render-scale=" << scale << ", msaa=" << MSAA_SAMPLES
                  << 'x' << std::endl;
        m_logged_initial_targets = true;
      }
      if (!m_probe_tex) {
        m_probe_tex =
          make_target (MTLPixelFormatRGBA32Float, PROBE_W, PROBE_H, 1, false);
        for (int i = 0; i < FRAMES_IN_FLIGHT; ++i)
          m_probe_buf[i] =
            [m_device newBufferWithLength:PROBE_W * PROBE_H * 16
                                  options:MTLResourceStorageModeShared];
      }
      // Freshly created: undefined contents until the first blur blit.
      m_prev_valid = false;
    }

    // Log-average the last completed probe and ease the exposure
    // toward mid-gray; clamped to about a stop either way so night
    // stays night.  Adapting down (a blinding scene) is faster than
    // adapting up, like eyes.
    void MetalRenderer::update_exposure () {
      id<MTLBuffer> buf = m_probe_buf[m_slot];
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
      const float rate = target < m_exposure ? 0.10f : 0.04f;
      m_exposure += (target - m_exposure) * rate;
    }

    // -- frame ---------------------------------------------------------

    bool MetalRenderer::begin_frame (const FrameParams& params) {
      const double frame_start = cpu_time ();
      ensure_targets ();
      const double targets_done = cpu_time ();
      if (!m_msaa_color)
        return false;

      dispatch_semaphore_wait (m_inflight, DISPATCH_TIME_FOREVER);
      const double inflight_done = cpu_time ();

      m_drawable = m_view.currentDrawable;
      const double drawable_done = cpu_time ();
      if (!m_drawable) {
        dispatch_semaphore_signal (m_inflight);
        return false;
      }
      if (m_profile_cpu) {
        m_cpu_frame_start = frame_start;
        m_cpu_encode_start = drawable_done;
        m_cpu_targets_total += targets_done - frame_start;
        m_cpu_inflight_total += inflight_done - targets_done;
        m_cpu_drawable_total += drawable_done - inflight_done;
      }

      m_slot = (m_slot + 1) % FRAMES_IN_FLIGHT;
      m_stream_used = 0;
      m_profile_this_frame = params.profile;
      m_timestamp_count = 0;
      m_sample_passes.clear ();
      m_current_gpu_pass = GpuPass::Count;

#if !TARGET_OS_IPHONE
      if (params.profile && !m_capture_path.empty () && !m_capture_active &&
          m_capture_frames == 0) {
        NSString* path =
          [NSString stringWithUTF8String:m_capture_path.c_str ()];
        MTLCaptureDescriptor* descriptor = [[MTLCaptureDescriptor alloc] init];
        descriptor.captureObject = m_queue;
        descriptor.destination = MTLCaptureDestinationGPUTraceDocument;
        descriptor.outputURL = [NSURL fileURLWithPath:path];
        NSError* error = nil;
        m_capture_active = [[MTLCaptureManager sharedCaptureManager]
          startCaptureWithDescriptor:descriptor
                               error:&error];
        if (m_capture_active)
          std::cerr << "moppe: capturing " << m_capture_frame_limit
                    << " gameplay frames to " << m_capture_path << std::endl;
        else {
          ++m_capture_frames; // Do not retry and spam every frame.
          std::cerr << "moppe: failed to start Metal capture: "
                    << error.localizedDescription.UTF8String << std::endl;
        }
      }
#endif

      const CGSize points = m_view.bounds.size;
      m_scale = points.width > 0
                  ? (float)m_view.drawableSize.width / (float)points.width
                  : 1.0f;
#if !TARGET_OS_IPHONE
      if (m_view.window) {
        NSScreen* screen = m_view.window.screen;
        if (screen)
          m_edr_headroom = std::max (
            1.0f, (float)screen.maximumExtendedDynamicRangeColorComponentValue);
      }
#endif
      m_width_pts = (int)points.width;
      m_height_pts = (int)points.height;

      m_fp = params;
      std::memset (&m_fu, 0, sizeof (m_fu));
      m_fu.view_proj = m4 (params.proj * params.view);
      m_fu.light_matrix = m4 (m_light_biased);
      m_fu.camera_pos = f4 (params.camera_pos);
      m_fu.sun_dir = f4 (params.sun_dir);
      m_fu.sun_diffuse = f4lin (params.sun_diffuse);
      m_fu.sun_specular = f4lin (params.sun_specular);
      m_fu.ambient = f4lin (params.ambient);
      m_fu.fog_color = f4lin (params.clear_color, params.fog_scale);
      m_fu.misc.x = params.time;
      m_fu.shadow.x = m_have_shadow ? m_terrain_params.shadow_strength : 0.0f;
      m_fu.shadow.y =
        m_shadow_map ? 1.0f / (float)m_shadow_map.width : 1.0f / 4096.0f;

      update_exposure ();

      m_cmd = [m_queue commandBuffer];
      m_current = m_scene_a;
      m_enc = nil;
      m_scene_pass_done = false;
      return true;
    }

    id<MTLRenderCommandEncoder> MetalRenderer::scene_encoder () {
      if (m_enc)
        return m_enc;

      MTLRenderPassDescriptor* rp =
        [MTLRenderPassDescriptor renderPassDescriptor];
      rp.colorAttachments[0].texture = m_msaa_color;
      rp.colorAttachments[0].resolveTexture = m_scene_a;
      rp.colorAttachments[0].loadAction = MTLLoadActionClear;
      rp.colorAttachments[0].storeAction = MTLStoreActionMultisampleResolve;
      rp.colorAttachments[0].clearColor =
        MTLClearColorMake (std::pow (m_fp.clear_color.x, 2.2f),
                           std::pow (m_fp.clear_color.y, 2.2f),
                           std::pow (m_fp.clear_color.z, 2.2f),
                           1.0);
      rp.depthAttachment.texture = m_msaa_depth;
      rp.depthAttachment.loadAction = MTLLoadActionClear;
      rp.depthAttachment.storeAction = MTLStoreActionDontCare;
      rp.depthAttachment.clearDepth = 0.0; // reversed-Z far
      rp.stencilAttachment.texture = m_msaa_depth;
      rp.stencilAttachment.loadAction = MTLLoadActionClear;
      rp.stencilAttachment.storeAction = MTLStoreActionDontCare;
      rp.stencilAttachment.clearStencil = 0;
      attach_gpu_pass (rp, GpuPass::Scene);

      m_enc = [m_cmd renderCommandEncoderWithDescriptor:rp];
      m_enc.label = @"World scene";
      [m_enc setFrontFacingWinding:MTLWindingCounterClockwise];
      return m_enc;
    }

    void MetalRenderer::begin_gpu_pass (id<MTLRenderCommandEncoder> enc,
                                        GpuPass pass) {
      if (!m_draw_boundary_timestamps || !m_profile_this_frame ||
          !m_timestamp_samples[m_slot] || pass == m_current_gpu_pass ||
          m_timestamp_count >= MAX_TIMESTAMP_SAMPLES - 1)
        return;
      [enc sampleCountersInBuffer:m_timestamp_samples[m_slot]
                    atSampleIndex:m_timestamp_count
                      withBarrier:YES];
      ++m_timestamp_count;
      m_sample_passes.push_back (pass);
      m_current_gpu_pass = pass;
    }

    void MetalRenderer::finish_gpu_pass (id<MTLRenderCommandEncoder> enc) {
      if (!m_draw_boundary_timestamps || !m_profile_this_frame ||
          !m_timestamp_samples[m_slot] || m_timestamp_count == 0 ||
          m_timestamp_count >= MAX_TIMESTAMP_SAMPLES)
        return;
      [enc sampleCountersInBuffer:m_timestamp_samples[m_slot]
                    atSampleIndex:m_timestamp_count
                      withBarrier:YES];
      ++m_timestamp_count;
      m_current_gpu_pass = GpuPass::Count;
    }

    void MetalRenderer::attach_gpu_pass (MTLRenderPassDescriptor* desc,
                                         GpuPass pass) {
      if (!m_stage_boundary_timestamps || !m_profile_this_frame ||
          !m_timestamp_samples[m_slot] ||
          m_timestamp_count + 2 > MAX_TIMESTAMP_SAMPLES)
        return;
      MTLRenderPassSampleBufferAttachmentDescriptor* attachment =
        desc.sampleBufferAttachments[0];
      attachment.sampleBuffer = m_timestamp_samples[m_slot];
      attachment.startOfVertexSampleIndex = m_timestamp_count++;
      attachment.endOfFragmentSampleIndex = m_timestamp_count++;
      m_sample_passes.push_back (pass);
    }

    void MetalRenderer::end_scene_encoder () {
      if (m_enc) {
        [m_enc endEncoding];
        m_enc = nil;
        m_scene_pass_done = true;
      } else if (!m_scene_pass_done) {
        // Nothing was drawn: run an empty pass so sceneA is cleared.
        scene_encoder ();
        [m_enc endEncoding];
        m_enc = nil;
        m_scene_pass_done = true;
      }
    }

    void MetalRenderer::draw_terrain (const ChunkDraw* chunks, int count) {
      if (!m_terrain || !m_have_terrain || count == 0)
        return;
      id<MTLRenderCommandEncoder> enc = scene_encoder ();
      begin_gpu_pass (enc, GpuPass::Terrain);

      [enc setRenderPipelineState:m_terrain];
      [enc setDepthStencilState:m_ds[1][1]];
      [enc setCullMode:MTLCullModeBack];

      MoppeTerrainUniforms u;
      std::memset (&u, 0, sizeof (u));
      u.view_proj = m_fu.view_proj;
      u.light_matrix = m4 (m_light_biased);
      u.camera_pos = m_fu.camera_pos;
      u.sun_dir = m_fu.sun_dir;
      u.sun_diffuse = m_fu.sun_diffuse;
      u.sun_specular = m_fu.sun_specular;
      u.ambient = m_fu.ambient;
      u.fog_color = m_fu.fog_color;
      u.fog_color.w = m_terrain_params.fog_scale;
      u.params0.x = m_terrain_params.scale.x;
      u.params0.y = m_terrain_params.scale.y;
      u.params0.z = m_terrain_params.scale.z;
      u.params0.w = m_terrain_params.tex_scale;
      u.params1.x = m_terrain_params.height_scale;
      u.params1.y = m_terrain_params.sea_level_norm;
      u.params1.z = m_have_shadow ? m_terrain_params.shadow_strength : 0;
      u.params1.w =
        m_shadow_map ? 1.0f / (float)m_shadow_map.width : 1.0f / 4096.0f;
      u.params2.x = static_cast<float> (m_terrain_params.projection);
      u.params2.y = m_terrain_params.torus_major_radius;
      u.params2.z = m_terrain_params.torus_minor_radius;
      u.params2.w = m_terrain_params.torus_height_scale;
      u.params3.x = m_terrain_params.derive_normals ? 1.0f : 0.0f;
      u.params3.y = m_terrain_params.periodic ? 1.0f : 0.0f;
      float height_blend = 1.0f;
      if (m_height_transition_active && m_previous_heights) {
        const float elapsed =
          std::max (0.0f, m_fp.time - m_height_transition_start);
        const float t = std::min (1.0f, elapsed / 0.12f);
        height_blend = t * t * (3.0f - 2.0f * t);
        if (t >= 1.0f)
          m_height_transition_active = false;
      }
      u.params3.z = height_blend;
      u.params3.w = m_previous_shadow_map
                      ? 1.0f / (float)m_previous_shadow_map.width
                      : u.params1.w;
      if (m_have_terrain_overlay) {
        u.params4.x = 1.0f + static_cast<float> (m_terrain_overlay_params.ramp);
        u.params4.y = m_terrain_overlay_params.minimum;
        u.params4.z = m_terrain_overlay_params.maximum;
        u.params4.w = m_terrain_overlay_params.opacity;
      }
      u.params5.x = m_terrain_params.topology_overlay ? 0.24f : 0.0f;
      u.params5.y = m_have_water_levels ? 1.0f : 0.0f;
      u.params5.z = m_have_moisture ? 1.0f : 0.0f;

      [enc setVertexBytes:&u length:sizeof (u) atIndex:MOPPE_BUF_FRAME];
      [enc setFragmentBytes:&u length:sizeof (u) atIndex:MOPPE_BUF_FRAME];
      [enc setVertexTexture:m_heights atIndex:MOPPE_TEX_HEIGHTS];
      [enc setVertexTexture:m_normals atIndex:MOPPE_TEX_NORMALS];
      [enc
        setVertexTexture:(m_previous_heights ? m_previous_heights : m_heights)
                 atIndex:MOPPE_TEX_PREVIOUS_HEIGHTS];

      MetalTexture* grass = (MetalTexture*)m_grass.get ();
      MetalTexture* dirt = (MetalTexture*)m_dirt.get ();
      MetalTexture* rock = (MetalTexture*)m_rock.get ();
      MetalTexture* snow = (MetalTexture*)m_snow.get ();
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
      if (m_shadow_map)
        [enc setFragmentTexture:m_shadow_map atIndex:MOPPE_TEX_SHADOW];
      if (m_shadow_map)
        [enc setFragmentTexture:(m_previous_shadow_map ? m_previous_shadow_map
                                                       : m_shadow_map)
                        atIndex:MOPPE_TEX_PREVIOUS_SHADOW];
      if (m_terrain_overlay)
        [enc setFragmentTexture:m_terrain_overlay
                        atIndex:MOPPE_TEX_TERRAIN_OVERLAY];
      [enc setFragmentTexture:(m_have_moisture ? m_moisture : m_heights)
                      atIndex:MOPPE_TEX_TERRAIN_MOISTURE];
      [enc setFragmentTexture:(m_have_water_levels ? m_water_levels : m_heights)
                      atIndex:MOPPE_TEX_TERRAIN_WATER];

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
                        indexCount:m_terrain_index_count[lod]
                         indexType:MTLIndexTypeUInt32
                       indexBuffer:m_terrain_indices[lod]
                 indexBufferOffset:0];
      }
    }

    void MetalRenderer::draw_sky (const SkyParams& params) {
      if (!m_sky)
        return;

      // Build the dome lazily: 24x24 sphere, radius 4000, positions
      // only (matches the old display list).
      if (!m_sky_verts) {
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
        m_sky_vcount = (uint32_t)(v.size () / 3);
        m_sky_verts =
          [m_device newBufferWithBytes:v.data ()
                                length:v.size () * sizeof (float)
                               options:MTLResourceStorageModeShared];
      }

      id<MTLRenderCommandEncoder> enc = scene_encoder ();
      begin_gpu_pass (enc, GpuPass::Sky);
      [enc setRenderPipelineState:m_sky];
      // Depth test on, write off: terrain occludes the cloud shader.
      [enc setDepthStencilState:m_ds[1][0]];
      [enc setCullMode:MTLCullModeNone];

      // Rotation-only view: the dome follows the camera.
      Mat4 view_rot = m_fp.view;
      view_rot.m[12] = view_rot.m[13] = view_rot.m[14] = 0;

      MoppeSkyUniforms u;
      std::memset (&u, 0, sizeof (u));
      u.view_proj = m4 (m_fp.proj * view_rot);
      u.sun_dir = f4 (params.sun_dir);
      u.fog_color = f4lin (params.fog_color);
      u.params.x = params.time;
      u.params.y = params.sun_height;
      u.params.z = params.cloudiness;

      [enc setVertexBuffer:m_sky_verts offset:0 atIndex:MOPPE_BUF_VERTICES];
      [enc setVertexBytes:&u length:sizeof (u) atIndex:MOPPE_BUF_FRAME];
      [enc setFragmentBytes:&u length:sizeof (u) atIndex:MOPPE_BUF_FRAME];
      [enc drawPrimitives:MTLPrimitiveTypeTriangle
              vertexStart:0
              vertexCount:m_sky_vcount];
    }

    void MetalRenderer::draw_ocean (const OceanParams& params) {
      if (!m_ocean || !m_ocean_verts)
        return;

      id<MTLRenderCommandEncoder> enc = scene_encoder ();
      begin_gpu_pass (enc, GpuPass::Water);
      [enc setRenderPipelineState:m_ocean];
      [enc setDepthStencilState:m_ds[1][1]];
      [enc setCullMode:MTLCullModeNone]; // visible from below too

      MoppeOceanUniforms u;
      std::memset (&u, 0, sizeof (u));
      u.view_proj = m_fu.view_proj;
      u.light_matrix = m4 (m_light_biased);
      u.camera_pos = m_fu.camera_pos;
      u.sun_dir = m_fu.sun_dir;
      u.sun_diffuse = m_fu.sun_diffuse;
      u.sun_specular = m_fu.sun_specular;
      u.ambient = m_fu.ambient;
      u.fog_color = f4lin (params.fog_color, params.fog_scale);
      u.params.x = params.time;
      u.params.y = m_ocean_level;
      u.params.z = m_terrain_params.periodic ? 1.0f : 0.0f;
      u.params.w = m_have_water_levels ? 1.0f : 0.0f;
      u.world_offset.x = params.world_offset.x;
      u.world_offset.z = params.world_offset.z;
      u.shadow.x = m_have_shadow ? m_terrain_params.shadow_strength : 0.0f;
      u.shadow.y =
        m_shadow_map ? 1.0f / (float)m_shadow_map.width : 1.0f / 4096.0f;

      // Shore data: the fragment shader reads the height texture to
      // find the seabed for foam and shallows.
      if (m_have_terrain && m_heights) {
        u.shore.x = 1.0f / m_terrain_params.scale.x;
        u.shore.y = 1.0f / m_terrain_params.scale.z;
        u.shore.z = m_terrain_params.scale.y;
        u.shore.w = (float)m_terrain_params.width;
        [enc setFragmentTexture:m_heights atIndex:MOPPE_TEX_HEIGHTS];
        [enc setVertexTexture:m_heights atIndex:MOPPE_TEX_HEIGHTS];
      }
      id<MTLTexture> water = m_have_water_levels ? m_water_levels : m_heights;
      [enc setVertexTexture:water atIndex:MOPPE_TEX_WATER_LEVELS];
      [enc setFragmentTexture:water atIndex:MOPPE_TEX_WATER_LEVELS_FRAGMENT];
      u.current.x = m_have_water_flow ? 1.0f : 0.0f;
      [enc setFragmentTexture:m_have_water_flow ? m_water_flow : water
                      atIndex:MOPPE_TEX_WATER_FLOW_FRAGMENT];
      if (m_shadow_map)
        [enc setFragmentTexture:m_shadow_map atIndex:MOPPE_TEX_SHADOW];

      // Near standing water renders on the terrain lattice through the
      // mesh pipeline; the coarse grid keeps the horizon. Both passes
      // discard on the same radius so they partition exactly.
      const bool lattice = m_water_tiles_pipeline && m_have_water_levels &&
                           m_have_terrain && m_heights;
      const float fine_radius = 700.0f;
      const float tile_world = 15.0f * m_terrain_params.scale.x;
      const int tiles_side = (int)std::ceil ((2.0f * fine_radius) / tile_world);
      if (lattice) {
        u.tiles.x = std::floor ((m_fp.camera_pos.x - fine_radius) / tile_world);
        u.tiles.y = std::floor ((m_fp.camera_pos.z - fine_radius) / tile_world);
        u.tiles.z = (float)tiles_side;
        u.tiles.w = fine_radius;
      }

      [enc setVertexBuffer:m_ocean_verts offset:0 atIndex:MOPPE_BUF_VERTICES];
      [enc setVertexBytes:&u length:sizeof (u) atIndex:MOPPE_BUF_FRAME];
      [enc setFragmentBytes:&u length:sizeof (u) atIndex:MOPPE_BUF_FRAME];
      [enc drawPrimitives:MTLPrimitiveTypeTriangle
              vertexStart:0
              vertexCount:m_ocean_vcount];

      if (lattice) {
        if (@available (macOS 13.0, iOS 16.0, *)) {
          MoppeOceanUniforms t = u;
          t.tiles.w = -fine_radius;
          [enc setRenderPipelineState:m_water_tiles_pipeline];
          [enc setObjectBytes:&t length:sizeof (t) atIndex:MOPPE_BUF_FRAME];
          [enc setObjectTexture:m_heights atIndex:MOPPE_TEX_HEIGHTS];
          [enc setObjectTexture:m_water_levels atIndex:MOPPE_TEX_WATER_LEVELS];
          [enc setMeshBytes:&t length:sizeof (t) atIndex:MOPPE_BUF_FRAME];
          [enc setMeshTexture:m_heights atIndex:MOPPE_TEX_HEIGHTS];
          [enc setMeshTexture:m_water_levels atIndex:MOPPE_TEX_WATER_LEVELS];
          [enc setFragmentBytes:&t length:sizeof (t) atIndex:MOPPE_BUF_FRAME];
          const NSUInteger total =
            (NSUInteger)tiles_side * (NSUInteger)tiles_side;
          [enc drawMeshThreadgroups:MTLSizeMake ((total + 63) / 64, 1, 1)
            threadsPerObjectThreadgroup:MTLSizeMake (64, 1, 1)
              threadsPerMeshThreadgroup:MTLSizeMake (256, 1, 1)];
        }
      }
    }

    void MetalRenderer::draw_grass (const GrassParams& params) {
      if (!m_grass_pipeline || !m_have_terrain || !m_heights || !m_normals ||
          params.radius <= 0 || params.spacing <= 0 ||
          params.blades_per_cell <= 0)
        return;

      const int side = (int)std::ceil ((2.0f * params.radius) / params.spacing);
      // Patch dimensions mirror GRASS_PATCH_X/Z in grass.metal.
      const int patches_x = (side + 3) / 4;
      const int patches_z = (side + 1) / 2;
      MoppeGrassUniforms u;
      std::memset (&u, 0, sizeof (u));
      // The window origin travels as exact integer cell indices; the shader
      // derives both seeds and world positions from them, so blades stay
      // bit-stable while the window follows the camera.
      u.grid.x =
        std::floor ((m_fp.camera_pos.x - params.radius) / params.spacing);
      u.grid.y =
        std::floor ((m_fp.camera_pos.z - params.radius) / params.spacing);
      u.grid.z = params.spacing;
      u.grid.w = (float)side;
      u.terrain.x = m_terrain_params.scale.x;
      u.terrain.y = m_terrain_params.scale.y;
      u.terrain.z = m_terrain_params.scale.z;
      u.terrain.w = params.radius;
      u.limits.x = m_terrain_params.sea_level_norm;
      u.limits.y = 0.42f;
      u.limits.z = m_terrain_params.periodic ? 1.0f : 0.0f;
      u.limits.w = (float)params.blades_per_cell;
      u.mesh.x = (float)patches_x;
      u.mesh.y = (float)patches_z;

      id<MTLRenderCommandEncoder> enc = scene_encoder ();
      begin_gpu_pass (enc, GpuPass::Grass);
      [enc setDepthStencilState:m_ds[1][1]];
      [enc setCullMode:MTLCullModeNone];
      [enc setFragmentBytes:&m_fu length:sizeof (m_fu) atIndex:MOPPE_BUF_FRAME];
      if (m_shadow_map)
        [enc setFragmentTexture:m_shadow_map atIndex:MOPPE_TEX_SHADOW];

      if (m_grass_mesh_pipeline) {
        if (@available (macOS 13.0, iOS 16.0, *)) {
          [enc setRenderPipelineState:m_grass_mesh_pipeline];
          [enc setObjectBytes:&m_fu
                       length:sizeof (m_fu)
                      atIndex:MOPPE_BUF_FRAME];
          [enc setObjectBytes:&u length:sizeof (u) atIndex:MOPPE_BUF_DRAW];
          [enc setObjectTexture:m_heights atIndex:MOPPE_TEX_HEIGHTS];
          [enc setMeshBytes:&m_fu length:sizeof (m_fu) atIndex:MOPPE_BUF_FRAME];
          [enc setMeshBytes:&u length:sizeof (u) atIndex:MOPPE_BUF_DRAW];
          [enc setMeshTexture:m_heights atIndex:MOPPE_TEX_HEIGHTS];
          [enc setMeshTexture:m_normals atIndex:MOPPE_TEX_NORMALS];
          [enc setMeshTexture:m_have_moisture
                                ? m_moisture
                                : ((MetalTexture*)m_white.get ())->texture
                      atIndex:MOPPE_TEX_MOISTURE];
          const NSUInteger total =
            (NSUInteger)patches_x * (NSUInteger)patches_z;
          [enc drawMeshThreadgroups:MTLSizeMake ((total + 63) / 64, 1, 1)
            threadsPerObjectThreadgroup:MTLSizeMake (64, 1, 1)
              threadsPerMeshThreadgroup:MTLSizeMake (32, 1, 1)];
          return;
        }
      }

      [enc setRenderPipelineState:m_grass_pipeline];
      [enc setVertexBytes:&m_fu length:sizeof (m_fu) atIndex:MOPPE_BUF_FRAME];
      [enc setVertexBytes:&u length:sizeof (u) atIndex:MOPPE_BUF_DRAW];
      [enc setVertexTexture:m_heights atIndex:MOPPE_TEX_HEIGHTS];
      [enc setVertexTexture:m_normals atIndex:MOPPE_TEX_NORMALS];
      [enc setVertexTexture:m_have_moisture
                              ? m_moisture
                              : ((MetalTexture*)m_white.get ())->texture
                    atIndex:MOPPE_TEX_MOISTURE];
      const NSUInteger vertices = 18u * (NSUInteger)params.blades_per_cell;
      const NSUInteger instances = (NSUInteger)side * (NSUInteger)side;
      [enc drawPrimitives:MTLPrimitiveTypeTriangle
              vertexStart:0
              vertexCount:vertices
            instanceCount:instances];
    }

    // -- draw lists ----------------------------------------------------

    size_t MetalRenderer::stream_vertices (const std::vector<Vertex>& verts) {
      const size_t bytes = verts.size () * sizeof (Vertex);
      const size_t offset = m_stream_used;
      const size_t needed = offset + bytes;

      id<MTLBuffer> buf = m_stream[m_slot];
      if (!buf || buf.length < needed) {
        // Native-resolution HUD text plus transient world-space effects can
        // legitimately exceed 1 MiB in Terrain Lab. Avoid replacing the shared
        // per-frame buffer between the scene and HUD encoders in the common
        // case; growth remains available for genuinely larger frames.
        size_t cap = buf ? buf.length : (4 << 20);
        while (cap < needed)
          cap *= 2;
        // Old buffer stays retained by prior encoder commands.
        buf = [m_device newBufferWithLength:cap
                                    options:MTLResourceStorageModeShared];
        if (offset > 0 && m_stream[m_slot])
          std::memcpy (buf.contents, m_stream[m_slot].contents, offset);
        m_stream[m_slot] = buf;
      }

      std::memcpy ((uint8_t*)buf.contents + offset, verts.data (), bytes);
      m_stream_used = needed;
      return offset;
    }

    void MetalRenderer::set_run_state (id<MTLRenderCommandEncoder> enc,
                                       const DrawState& s,
                                       const Texture* tex,
                                       bool hud) {
      if (hud) {
        [enc setRenderPipelineState:m_hud];
        [enc setCullMode:MTLCullModeNone];
      } else {
        [enc setRenderPipelineState:s.additive ? m_uber_add
                                    : s.blend  ? m_uber_blend
                                               : m_uber_opaque];
        [enc setDepthStencilState:m_ds[s.depth_test ? 1 : 0]
                                      [s.depth_write ? 1 : 0]];
        [enc setCullMode:s.cull ? MTLCullModeBack : MTLCullModeNone];
      }

      const MetalTexture* mt = (const MetalTexture*)tex;
      if (!mt)
        mt = (const MetalTexture*)m_white.get ();
      [enc setFragmentTexture:mt->texture atIndex:MOPPE_TEX_COLOR];
      if (!hud && m_shadow_map)
        [enc setFragmentTexture:m_shadow_map atIndex:MOPPE_TEX_SHADOW];
      [enc setFragmentSamplerState:mt->sampler atIndex:0];
    }

    void MetalRenderer::play_list (id<MTLRenderCommandEncoder> enc,
                                   const std::vector<Vertex>& verts,
                                   const std::vector<DrawList::Run>& runs,
                                   bool hud) {
      if (verts.empty ())
        return;
      const size_t base = stream_vertices (verts);
      [enc setVertexBuffer:m_stream[m_slot]
                    offset:base
                   atIndex:MOPPE_BUF_VERTICES];

      for (size_t i = 0; i < runs.size (); ++i) {
        const DrawList::Run& r = runs[i];
        if (r.count == 0)
          continue;
        set_run_state (enc, r.state, r.texture, hud);
        [enc drawPrimitives:MTLPrimitiveTypeTriangle
                vertexStart:r.first
                vertexCount:r.count];
      }
    }

    void MetalRenderer::draw_list (const DrawList& list) {
      if (list.empty ())
        return;
      id<MTLRenderCommandEncoder> enc = scene_encoder ();
      begin_gpu_pass (enc, GpuPass::Scene);

      MoppeDrawUniforms du;
      std::memset (&du, 0, sizeof (du));
      du.model = m4 (Mat4 ());
      du.nrm0.x = 1;
      du.nrm1.y = 1;
      du.nrm2.z = 1;
      [enc setVertexBytes:&du length:sizeof (du) atIndex:MOPPE_BUF_DRAW];
      [enc setVertexBytes:&m_fu length:sizeof (m_fu) atIndex:MOPPE_BUF_FRAME];
      [enc setFragmentBytes:&m_fu length:sizeof (m_fu) atIndex:MOPPE_BUF_FRAME];

      play_list (enc, list.vertices (), list.runs (), false);
    }

    void MetalRenderer::draw_mesh (const Mesh& mesh, const Mat4& model) {
      const MetalMesh& m = (const MetalMesh&)mesh;
      if (!m.vertices)
        return;
      id<MTLRenderCommandEncoder> enc = scene_encoder ();
      begin_gpu_pass (enc, GpuPass::Scene);

      MoppeDrawUniforms du;
      std::memset (&du, 0, sizeof (du));
      du.model = m4 (model);
      const NormalMat nm = NormalMat::from (model);
      du.nrm0 = f4 (nm.c0);
      du.nrm1 = f4 (nm.c1);
      du.nrm2 = f4 (nm.c2);
      [enc setVertexBytes:&du length:sizeof (du) atIndex:MOPPE_BUF_DRAW];
      [enc setVertexBytes:&m_fu length:sizeof (m_fu) atIndex:MOPPE_BUF_FRAME];
      [enc setFragmentBytes:&m_fu length:sizeof (m_fu) atIndex:MOPPE_BUF_FRAME];

      [enc setVertexBuffer:m.vertices offset:0 atIndex:MOPPE_BUF_VERTICES];
      for (size_t i = 0; i < m.runs.size (); ++i) {
        const DrawList::Run& r = m.runs[i];
        if (r.count == 0)
          continue;
        set_run_state (enc, r.state, r.texture, false);
        [enc drawPrimitives:MTLPrimitiveTypeTriangle
                vertexStart:r.first
                vertexCount:r.count];
      }
    }

    void MetalRenderer::draw_rivers (const Mesh& mesh, const Mat4& model) {
      const MetalMesh& m = (const MetalMesh&)mesh;
      if (!m_river || !m.vertices)
        return;
      id<MTLRenderCommandEncoder> enc = scene_encoder ();
      begin_gpu_pass (enc, GpuPass::Water);
      [enc setRenderPipelineState:m_river];
      [enc setDepthStencilState:m_ds_river];
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
      [enc setVertexBytes:&m_fu length:sizeof (m_fu) atIndex:MOPPE_BUF_FRAME];
      [enc setFragmentBytes:&m_fu length:sizeof (m_fu) atIndex:MOPPE_BUF_FRAME];
      if (m_shadow_map)
        [enc setFragmentTexture:m_shadow_map atIndex:MOPPE_TEX_SHADOW];
      [enc setVertexBuffer:m.vertices offset:0 atIndex:MOPPE_BUF_VERTICES];
      for (const DrawList::Run& run : m.runs)
        if (run.count > 0)
          [enc drawPrimitives:MTLPrimitiveTypeTriangle
                  vertexStart:run.first
                  vertexCount:run.count];
    }

    // -- post ----------------------------------------------------------

    void MetalRenderer::apply_underwater (float time) {
      if (!m_underwater)
        return;
      end_scene_encoder ();

      id<MTLTexture> src = m_current;
      id<MTLTexture> dst = (src == m_scene_a) ? m_scene_b : m_scene_a;

      MTLRenderPassDescriptor* rp =
        [MTLRenderPassDescriptor renderPassDescriptor];
      rp.colorAttachments[0].texture = dst;
      rp.colorAttachments[0].loadAction = MTLLoadActionDontCare;
      rp.colorAttachments[0].storeAction = MTLStoreActionStore;
      attach_gpu_pass (rp, GpuPass::Post);

      id<MTLRenderCommandEncoder> enc =
        [m_cmd renderCommandEncoderWithDescriptor:rp];
      enc.label = @"Underwater post-process";
      begin_gpu_pass (enc, GpuPass::Post);
      MoppeQuadUniforms q;
      std::memset (&q, 0, sizeof (q));
      q.tint.x = q.tint.y = q.tint.z = q.tint.w = 1;
      q.params.x = 1; // no zoom
      q.params.y = time;
      [enc setRenderPipelineState:m_underwater];
      [enc setVertexBytes:&q length:sizeof (q) atIndex:MOPPE_BUF_FRAME];
      [enc setFragmentBytes:&q length:sizeof (q) atIndex:MOPPE_BUF_FRAME];
      [enc setFragmentTexture:src atIndex:MOPPE_TEX_SCENE];
      [enc drawPrimitives:MTLPrimitiveTypeTriangle vertexStart:0 vertexCount:3];
      [enc endEncoding];

      m_current = dst;
    }

    void MetalRenderer::apply_motion_blur (float strength) {
      if (!m_ghost || strength <= 0.01f)
        return;
      end_scene_encoder ();

      // Ghost quads: previous frame drawn back 3x, zoomed, faded --
      // then the composite becomes the next "previous frame", which
      // is what makes the radial streaks build up.  A freshly
      // (re)created prev texture holds undefined memory: skip the
      // ghosts and just prime it (the old build's m_blur_valid).
      if (!m_prev_valid) {
        id<MTLBlitCommandEncoder> prime = [m_cmd blitCommandEncoder];
        [prime copyFromTexture:m_current toTexture:m_prev_frame];
        [prime endEncoding];
        m_prev_valid = true;
        return;
      }

      MTLRenderPassDescriptor* rp =
        [MTLRenderPassDescriptor renderPassDescriptor];
      rp.colorAttachments[0].texture = m_current;
      rp.colorAttachments[0].loadAction = MTLLoadActionLoad;
      rp.colorAttachments[0].storeAction = MTLStoreActionStore;
      attach_gpu_pass (rp, GpuPass::Post);

      id<MTLRenderCommandEncoder> enc =
        [m_cmd renderCommandEncoderWithDescriptor:rp];
      enc.label = @"Motion blur";
      begin_gpu_pass (enc, GpuPass::Post);
      [enc setRenderPipelineState:m_ghost];
      for (int i = 1; i <= 3; ++i) {
        MoppeQuadUniforms q;
        std::memset (&q, 0, sizeof (q));
        const float alpha = 0.5f * strength / i;
        q.tint.x = q.tint.y = q.tint.z = 1;
        q.tint.w = alpha;
        q.params.x = 1.0f + 0.012f * i * strength;
        [enc setVertexBytes:&q length:sizeof (q) atIndex:MOPPE_BUF_FRAME];
        [enc setFragmentBytes:&q length:sizeof (q) atIndex:MOPPE_BUF_FRAME];
        [enc setFragmentTexture:m_prev_frame atIndex:MOPPE_TEX_SCENE];
        [enc drawPrimitives:MTLPrimitiveTypeTriangle
                vertexStart:0
                vertexCount:3];
      }
      [enc endEncoding];

      id<MTLBlitCommandEncoder> blit = [m_cmd blitCommandEncoder];
      [blit copyFromTexture:m_current toTexture:m_prev_frame];
      [blit endEncoding];
    }

    void MetalRenderer::apply_scene_blur () {
      if (!m_copy || !m_bloom_blur || !m_bloom_a || !m_bloom_b)
        return;
      end_scene_encoder ();

      const auto pass = [this] (NSString* label,
                                id<MTLRenderPipelineState> pipeline,
                                id<MTLTexture> src,
                                id<MTLTexture> dst,
                                const MoppeQuadUniforms& q) {
        MTLRenderPassDescriptor* rp =
          [MTLRenderPassDescriptor renderPassDescriptor];
        rp.colorAttachments[0].texture = dst;
        rp.colorAttachments[0].loadAction = MTLLoadActionDontCare;
        rp.colorAttachments[0].storeAction = MTLStoreActionStore;
        attach_gpu_pass (rp, GpuPass::Post);
        id<MTLRenderCommandEncoder> enc =
          [m_cmd renderCommandEncoderWithDescriptor:rp];
        enc.label = label;
        begin_gpu_pass (enc, GpuPass::Post);
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
      pass (@"Loading background downsample", m_copy, m_current, m_bloom_a, q);
      q.params.z = 2.0f / (float)m_bloom_a.width;
      pass (@"Loading background horizontal blur",
            m_bloom_blur,
            m_bloom_a,
            m_bloom_b,
            q);
      q.params.z = 0;
      q.params.w = 2.0f / (float)m_bloom_a.height;
      pass (@"Loading background vertical blur",
            m_bloom_blur,
            m_bloom_b,
            m_bloom_a,
            q);
      q.params.w = 0;
      id<MTLTexture> dst = m_current == m_scene_a ? m_scene_b : m_scene_a;
      pass (@"Loading background upsample", m_copy, m_bloom_a, dst, q);
      m_current = dst;
    }

    // -- hud + present ---------------------------------------------------

    void MetalRenderer::draw_hud (const DrawList& list) {
      end_scene_encoder ();

      // One fullscreen pass into `dst` reading `src`.
      auto quad_pass = [&] (GpuPass pass,
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
        attach_gpu_pass (p, pass);
        id<MTLRenderCommandEncoder> e =
          [m_cmd renderCommandEncoderWithDescriptor:p];
        e.label = label;
        begin_gpu_pass (e, pass);
        [e setRenderPipelineState:pso];
        [e setVertexBytes:&q length:sizeof (q) atIndex:MOPPE_BUF_FRAME];
        [e setFragmentBytes:&q length:sizeof (q) atIndex:MOPPE_BUF_FRAME];
        [e setFragmentTexture:src atIndex:MOPPE_TEX_SCENE];
        [e drawPrimitives:MTLPrimitiveTypeTriangle vertexStart:0 vertexCount:3];
        [e endEncoding];
      };

      // Bloom: bright-pass into quarter res, then a separable blur
      // (a -> b horizontal, b -> a vertical).  The bright pass sees
      // the exposed scene so the glow tracks the eye's adaptation.
      const bool bloom_ok = m_bloom_bright && m_bloom_blur && m_bloom_a;
      if (bloom_ok) {
        MoppeQuadUniforms q;
        std::memset (&q, 0, sizeof (q));
        q.tint.x = q.tint.y = q.tint.z = 1;
        q.tint.w = m_exposure * m_fp.exposure_bias;
        q.params.x = 1;
        quad_pass (GpuPass::Bloom,
                   @"Bloom bright pass",
                   m_bloom_bright,
                   m_current,
                   m_bloom_a,
                   q);

        q.params.z = 1.0f / (float)m_bloom_a.width;
        q.params.w = 0;
        quad_pass (GpuPass::Bloom,
                   @"Bloom horizontal blur",
                   m_bloom_blur,
                   m_bloom_a,
                   m_bloom_b,
                   q);
        q.params.z = 0;
        q.params.w = 1.0f / (float)m_bloom_a.height;
        quad_pass (GpuPass::Bloom,
                   @"Bloom vertical blur",
                   m_bloom_blur,
                   m_bloom_b,
                   m_bloom_a,
                   q);
      }

      // Auto-exposure probe: a 32x16 average of this frame, blitted
      // to a CPU-visible buffer and read FRAMES_IN_FLIGHT frames
      // later in update_exposure().
      if (m_probe && m_probe_tex && m_probe_buf[m_slot]) {
        MoppeQuadUniforms q;
        std::memset (&q, 0, sizeof (q));
        q.tint.x = q.tint.y = q.tint.z = q.tint.w = 1;
        q.params.x = 1;
        quad_pass (GpuPass::Exposure,
                   @"Exposure probe",
                   m_probe,
                   m_current,
                   m_probe_tex,
                   q);

        id<MTLBlitCommandEncoder> blit = [m_cmd blitCommandEncoder];
        [blit copyFromTexture:m_probe_tex
                       sourceSlice:0
                       sourceLevel:0
                      sourceOrigin:MTLOriginMake (0, 0, 0)
                        sourceSize:MTLSizeMake (PROBE_W, PROBE_H, 1)
                          toBuffer:m_probe_buf[m_slot]
                 destinationOffset:0
            destinationBytesPerRow:PROBE_W * 16
          destinationBytesPerImage:PROBE_W * PROBE_H * 16];
        [blit endEncoding];
      }

      MTLRenderPassDescriptor* rp =
        [MTLRenderPassDescriptor renderPassDescriptor];
      rp.colorAttachments[0].texture = m_drawable.texture;
      rp.colorAttachments[0].loadAction = MTLLoadActionDontCare;
      rp.colorAttachments[0].storeAction = MTLStoreActionStore;
      attach_gpu_pass (rp, GpuPass::Present);

      id<MTLRenderCommandEncoder> enc =
        [m_cmd renderCommandEncoderWithDescriptor:rp];
      enc.label = @"Present and HUD";
      begin_gpu_pass (enc, GpuPass::Present);

      // Scene quad with the tonemap, grade, bloom, and lens flare.
      if (m_present) {
        MoppeQuadUniforms q;
        std::memset (&q, 0, sizeof (q));
        q.tint.x = q.tint.y = q.tint.z = 1;
        q.tint.w = m_exposure * m_fp.exposure_bias;
        q.params.x = 1;
        q.params.y = m_fp.time;
#if !TARGET_OS_IPHONE
        q.params.z = m_edr_headroom;
        q.params.w = 1;
#endif

        // Project the sun onto the screen for the flare; the game
        // supplies the occlusion term, we add the edge fade.
        if (m_fp.sun_visibility > 0.001f) {
          const Mat4 vp = m_fp.proj * m_fp.view;
          const Vector3D sp = m_fp.camera_pos + m_fp.sun_dir * 4000.0f;
          const float* m = vp.m;
          const float cx = m[0] * sp.x + m[4] * sp.y + m[8] * sp.z + m[12];
          const float cy = m[1] * sp.x + m[5] * sp.y + m[9] * sp.z + m[13];
          const float cw = m[3] * sp.x + m[7] * sp.y + m[11] * sp.z + m[15];
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
              q.sun.z = m_fp.sun_visibility * (edge > 1.0f ? 1.0f : edge) *
                        m_exposure * m_fp.exposure_bias;
              q.sun.w = (float)m_target_w / (float)m_target_h;
            }
          }
        }

        [enc setRenderPipelineState:m_present];
        [enc setVertexBytes:&q length:sizeof (q) atIndex:MOPPE_BUF_FRAME];
        [enc setFragmentBytes:&q length:sizeof (q) atIndex:MOPPE_BUF_FRAME];
        [enc setFragmentTexture:m_current atIndex:MOPPE_TEX_SCENE];
        if (bloom_ok)
          [enc setFragmentTexture:m_bloom_a atIndex:MOPPE_TEX_BLOOM];
        [enc drawPrimitives:MTLPrimitiveTypeTriangle
                vertexStart:0
                vertexCount:3];
      }

      // HUD overlay in point coordinates.
      if (!list.empty () && m_hud) {
        MoppeHudUniforms hu;
        std::memset (&hu, 0, sizeof (hu));
        hu.proj =
          m4 (Mat4::hud_ortho ((float)m_width_pts, (float)m_height_pts));
#if !TARGET_OS_IPHONE
        hu.params.x = 1;
#endif
        [enc setVertexBytes:&hu length:sizeof (hu) atIndex:MOPPE_BUF_FRAME];
        [enc setFragmentBytes:&hu length:sizeof (hu) atIndex:MOPPE_BUF_FRAME];
        play_list (enc, list.vertices (), list.runs (), true);
      }

      finish_gpu_pass (enc);
      [enc endEncoding];
    }

    void MetalRenderer::request_screenshot (const std::string& path) {
      m_screenshot_path = path;
    }

    void MetalRenderer::end_frame () {
      end_scene_encoder (); // in case nothing was drawn

#if !TARGET_OS_IPHONE
      id<MTLBuffer> capture = nil;
      std::size_t capture_row_bytes = 0;
      int capture_width = 0;
      int capture_height = 0;
      const std::string capture_path = m_screenshot_path;
      if (!capture_path.empty () && m_drawable) {
        capture_width = static_cast<int> (m_drawable.texture.width);
        capture_height = static_cast<int> (m_drawable.texture.height);
        capture_row_bytes =
          (static_cast<std::size_t> (capture_width) * 8 + 255) &
          ~static_cast<std::size_t> (255);
        capture =
          [m_device newBufferWithLength:capture_row_bytes * capture_height
                                options:MTLResourceStorageModeShared];
        id<MTLBlitCommandEncoder> blit = [m_cmd blitCommandEncoder];
        [blit copyFromTexture:m_drawable.texture
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

      if (m_drawable
#if !TARGET_OS_IPHONE
          && capture_path.empty ()
#endif
      )
        [m_cmd presentDrawable:m_drawable];

      id<MTLBuffer> timestamp_results = nil;
      const int timestamp_count = m_timestamp_count;
      const std::vector<GpuPass> sample_passes = m_sample_passes;
      if (timestamp_count > 1 && m_timestamp_samples[m_slot]) {
        timestamp_results = m_timestamp_results[m_slot];
        id<MTLBlitCommandEncoder> resolve = [m_cmd blitCommandEncoder];
        resolve.label = @"Resolve frame timestamps";
        [resolve resolveCounters:m_timestamp_samples[m_slot]
                         inRange:NSMakeRange (0, timestamp_count)
               destinationBuffer:timestamp_results
               destinationOffset:0];
        [resolve endEncoding];
      }

      dispatch_semaphore_t sem = m_inflight;
      std::shared_ptr<FrameTiming> timing =
        m_profile_this_frame ? m_frame_timing : nullptr;
      [m_cmd addCompletedHandler:^(id<MTLCommandBuffer> command) {
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
            std::cerr << "  encoder spans (may overlap):";
            for (int i = 0; i < GPU_PASS_COUNT; ++i)
              if (timing->pass_total_ms[i] > 0)
                std::cerr << " " << GPU_PASS_NAMES[i] << "="
                          << timing->pass_total_ms[i] / timing->frames << " ms";
            std::cerr << std::endl;
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
      [m_cmd commit];
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
      if (m_capture_active && m_profile_this_frame &&
          ++m_capture_frames >= m_capture_frame_limit) {
        [m_cmd waitUntilCompleted];
        [[MTLCaptureManager sharedCaptureManager] stopCapture];
        m_capture_active = false;
        std::cerr << "moppe: wrote Metal capture " << m_capture_path
                  << std::endl;
      }
#endif

#if !TARGET_OS_IPHONE
      if (capture) {
        [m_cmd waitUntilCompleted];
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
      m_screenshot_path.clear ();
#endif

      m_cmd = nil;
      m_drawable = nil;
      m_current = nil;
    }

    // ------------------------------------------------------------------

    Renderer* create_metal_renderer (void* mtk_view,
                                     const std::string& lib_path) {
      return new MetalRenderer ((__bridge MTKView*)mtk_view, lib_path);
    }
  }
}
