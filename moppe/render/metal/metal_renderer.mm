// Metal 4 backend for moppe/render. One reusable command buffer per frame:
//
//   scene pass       memoryless MSAA, or temporal color/depth/motion/reactive
//   reconstruction  temporal/spatial MetalFX, or exact linear enlargement
//   post passes      native-size underwater grade / motion-blur feedback
//   present pass     fullscreen final treatment + HUD overlay
//
// Terrain is vertex-pulled from height/normal textures; the shadow
// map is a one-time Depth16Unorm ortho render.  All texture uploads
// go through staging buffers + compute-encoder copies so retained resources
// stay in private storage on TBDR devices.

#import <Metal/Metal.h>
#import <MetalFX/MetalFX.h>
#import <QuartzCore/QuartzCore.h>
#import <TargetConditionals.h>
#if TARGET_OS_IPHONE
#import <UIKit/UIKit.h>
#else
#import <AppKit/AppKit.h>
#import <ImageIO/ImageIO.h>
#endif

#include <moppe/profile.hh>
#include <moppe/render/metal/metal4_frame.hh>
#include <moppe/render/metal/metal_renderer.hh>
#include <moppe/render/metal/shader_types.h>
#include <moppe/render/reflection_geometry.hh>

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
#include <iomanip>
#include <iostream>
#include <limits>
#include <memory>
#include <mutex>
#include <sstream>
#include <stdexcept>
#include <unordered_map>
#include <vector>

namespace moppe {
  namespace render {
    namespace {
      const int FRAMES_IN_FLIGHT = 3;
#if TARGET_OS_TV
      // At television viewing distance 2x MSAA preserves stable terrain edges
      // while halving the dominant scene-pass color and depth sample traffic.
      const int DEFAULT_MSAA_SAMPLES = 2;
#else
      const int DEFAULT_MSAA_SAMPLES = 4;
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
      const std::size_t FRAME_ARENA_CAPACITY = 8 << 20;

      double cpu_time () {
        using Clock = std::chrono::steady_clock;
        return std::chrono::duration<double> (Clock::now ().time_since_epoch ())
          .count ();
      }

      float halton (uint64_t index, uint32_t base) {
        float result = 0.0f;
        float fraction = 1.0f;
        while (index) {
          fraction /= static_cast<float> (base);
          result += fraction * static_cast<float> (index % base);
          index /= base;
        }
        return result;
      }

      // Point resolution is an affordable scene size only when something
      // bounds it.  A Retina panel bounds it by folding pixels into points;
      // a desktop display attached at 1x does not, and a 7680x2160 one hands
      // the scene pass twice a 4K frame.  So the desktop, where the display
      // is whatever the player plugged in, takes the smaller of two bounds:
      // the point-relative rule and a megapixel budget.  An oversized
      // drawable then costs resolution instead of frame rate.  Handheld and
      // television targets ship against known panels and keep the
      // point-relative rule alone.
      float scene_render_scale (float backing_scale,
                                float requested_scale,
                                float scale_override,
                                float megapixel_budget,
                                double drawable_pixels) {
#if TARGET_OS_TV
        // When a 4K television uses a 2x UIKit backing scale, keep the
        // expensive 3D scene relative to point resolution and let the
        // inexpensive present/HUD pass target the native drawable.
        (void)megapixel_budget, (void)drawable_pixels;
        float scale = requested_scale / std::max (1.0f, backing_scale);
#elif TARGET_OS_IPHONE
        (void)backing_scale, (void)megapixel_budget, (void)drawable_pixels;
        float scale = requested_scale;
#else
        const float point_relative = 1.0f / std::max (1.0f, backing_scale);
        // Budgeted area becomes a linear scale through its square root.
        const float affordable =
          (megapixel_budget > 0.0f && drawable_pixels > 0)
            ? (float)std::sqrt (megapixel_budget * 1.0e6 / drawable_pixels)
            : 1.0f;
        float scale = requested_scale * std::min (point_relative, affordable);
#endif
        if (scale_override > 0.0f)
          scale = scale_override;
        return std::clamp (scale, 0.25f, 1.0f);
      }

      enum class ResolvedUpscaling { Native, Linear, Spatial, Temporal };

      const char* upscaling_name (UpscalingMode mode) {
        switch (mode) {
        case UpscalingMode::Linear:
          return "linear";
        case UpscalingMode::Spatial:
          return "spatial";
        case UpscalingMode::Temporal:
          return "temporal";
        }
        return "unknown";
      }

      const char* upscaling_name (ResolvedUpscaling mode) {
        switch (mode) {
        case ResolvedUpscaling::Native:
          return "native";
        case ResolvedUpscaling::Linear:
          return "linear";
        case ResolvedUpscaling::Spatial:
          return "spatial";
        case ResolvedUpscaling::Temporal:
          return "temporal";
        }
        return "unknown";
      }

      enum class GpuPass {
        Shadow,
        Terrain,
        Sky,
        Water,
        Scene,
        Post,
        Bloom,
        Exposure,
        Reflection,
        Reconstruction,
        Interpolation,
        Present,
        Count
      };
      constexpr int GPU_PASS_COUNT = static_cast<int> (GpuPass::Count);
      const char* GPU_PASS_NAMES[GPU_PASS_COUNT] = {
        "shadow",     "terrain", "sky",           "water",
        "scene",      "post",    "bloom",         "exposure",
        "reflection", "upscale", "interpolation", "present"
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

      struct BenchmarkSample {
        uint32_t mask;
        uint32_t partition_mask;
        uint32_t epoch;
        uint32_t frame;
        double gpu_ms;
        std::array<double, GPU_PASS_COUNT> pass_ms;
      };

      struct BenchmarkOutput {
        std::mutex mutex;
        std::vector<BenchmarkSample> samples;
        std::atomic<int> completed { 0 };
        int expected = 0;
        std::string path;
        std::string partition = "standard";
        std::vector<std::string> feature_names;
        std::vector<std::string> block_names;
        bool pass_timing = false;
      };

#if !TARGET_OS_IPHONE
      float metal_half_to_float (std::uint16_t half) {
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
            pixel[0] = linear_byte (metal_half_to_float (row[x * 4]));
            pixel[1] = linear_byte (metal_half_to_float (row[x * 4 + 1]));
            pixel[2] = linear_byte (metal_half_to_float (row[x * 4 + 2]));
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
        static_assert (sizeof (Mat4) == sizeof (MoppeMat4));
        static_assert (alignof (Mat4) == alignof (MoppeMat4));
        MoppeMat4 r;
        std::memcpy (&r, m.bytes (), sizeof (r));
        return r;
      }

      struct MetalTexture : public Texture {
        id<MTLTexture> texture = nil;
        id<MTLSamplerState> sampler = nil;
        id<MTLResidencySet> residency = nil;

        ~MetalTexture () override {
          if (texture && residency) {
            [residency removeAllocation:texture];
            [residency commit];
          }
        }
      };

      struct MetalMesh : public Mesh {
        id<MTLBuffer> vertices = nil;
        std::vector<DrawList::Run> runs;
        id<MTLResidencySet> residency = nil;

        ~MetalMesh () override {
          if (vertices && residency) {
            [residency removeAllocation:vertices];
            [residency commit];
          }
        }
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
        id<MTLRenderPipelineState> shafts = nil;
        id<MTLRenderPipelineState> shafts_add = nil;
        id<MTLRenderPipelineState> gtao = nil;
        id<MTLRenderPipelineState> gtao_blur = nil;
        id<MTLRenderPipelineState> gtao_apply = nil;
        id<MTLRenderPipelineState> bloom_bright = nil;
        id<MTLRenderPipelineState> bloom_blur = nil;
        id<MTLRenderPipelineState> probe = nil;
        id<MTLRenderPipelineState> exposure = nil;
        id<MTLRenderPipelineState> terrain = nil, terrain_shadow = nil;
        id<MTLRenderPipelineState> sky = nil, ocean = nil;
        id<MTLRenderPipelineState> dust_soft = nil, dust_add = nil;
        id<MTLRenderPipelineState> dust_mesh_soft = nil;
        id<MTLRenderPipelineState> dust_mesh_add = nil;
        id<MTLRenderPipelineState> water_tiles = nil;
        id<MTLRenderPipelineState> undergrowth = nil;
        id<MTLRenderPipelineState> forest = nil;
        id<MTLRenderPipelineState> forest_shadow = nil;
        id<MTLRenderPipelineState> river = nil;
#if !TARGET_OS_IPHONE
        id<MTLComputePipelineState> reflection_geometry = nil;
        id<MTLRenderPipelineState> reflection_water_input = nil;
        id<MTLRenderPipelineState> reflection_water_tiles = nil;
        id<MTLComputePipelineState> water_reflection_signal = nil;
        id<MTLComputePipelineState> water_reflection_diagnostic = nil;
#endif
        bool mesh_shaders_ok = false;

        // Depth-stencil: index [test][write], reversed-Z (>=).
        id<MTLDepthStencilState> depth[2][2] {};
        id<MTLDepthStencilState> shadow_depth = nil;
        id<MTLDepthStencilState> river_depth = nil;
        id<MTLSamplerState> sampler_repeat = nil;
        id<MTLSamplerState> sampler_clamp = nil;
        id<MTLTexture> shadow_fallback = nil;
        TexturePtr white, black;
      };

      struct MetalTerrainResources {
        // Shadow maps are published with the completed terrain and remain
        // available to terrain, water, and immediate scene geometry.
        id<MTLTexture> shadow_map = nil;
        Mat4 light_biased;
        bool have_shadow = false;

        id<MTLTexture> heights = nil;
        id<MTLTexture> normals = nil;
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
#if !TARGET_OS_IPHONE
        // Goal 0 atelier only: one bounded, terrain-only BLAS derived from the
        // completed surface. It is absent unless explicitly requested.
        id<MTLBuffer> reflection_vertices = nil;
        id<MTLAccelerationStructure> reflection_structure = nil;
        ReflectionTerrainProxy reflection_proxy;
        NSUInteger reflection_structure_bytes = 0;
        NSUInteger reflection_scratch_bytes = 0;
        double reflection_proxy_ms = 0.0;
        double reflection_build_ms = 0.0;
#endif
      };

      struct MetalForestResources {
        id<MTLBuffer> instances = nil;
        std::uint32_t count = 0;
        float period_x = 0.0f;
        float period_z = 0.0f;
      };

      struct MetalWaterResources {
        id<MTLBuffer> ocean_verts = nil;
        id<MTLBuffer> ocean_indices = nil;
        uint32_t ocean_vcount = 0;
        uint32_t ocean_icount = 0;
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
        id<MTLTexture> scene_a = nil;
        id<MTLTexture> post_a = nil, post_b = nil;
        // Half-resolution sun-shaft scatter; the march is the entire cost
        // of the effect and its result is low-frequency light.
        id<MTLTexture> shafts = nil;
        // Half-resolution ambient occlusion, ping-ponged through its blur.
        id<MTLTexture> ao_a = nil, ao_b = nil;
        id<MTLTexture> motion = nil, reactive = nil;
        id<MTLTexture> prev_frame = nil;
        id<MTLTexture> bloom_a = nil, bloom_b = nil;
        bool prev_valid = false;
        id<MTLTexture> spatial_output = nil;
        id<MTL4FXSpatialScaler> spatial_scaler = nil;
        id<MTL4FXTemporalScaler> temporal_scaler = nil;
        id<MTL4FXFrameInterpolator> frame_interpolator = nil;
        id<MTLTexture> interpolator_color[FRAMES_IN_FLIGHT] {};
        id<MTLTexture> interpolator_composite[FRAMES_IN_FLIGHT] {};
        id<MTLTexture> interpolator_output[FRAMES_IN_FLIGHT] {};
        id<MTLTexture> exposure_tex = nil;
        id<MTLFence> spatial_fence = nil;
        UpscalingMode requested_upscaling = UpscalingMode::Temporal;
        ResolvedUpscaling resolved_upscaling = ResolvedUpscaling::Linear;
        int output_width = 0, output_height = 0;

        // The luminance probe returns through the in-flight ring before it
        // updates exposure, so it belongs with the temporal targets.
        id<MTLTexture> probe_tex = nil;
        id<MTLBuffer> probe_buf[FRAMES_IN_FLIGHT] {};
        float exposure = 1.0f;
        bool temporal_history_valid = false;
        bool interpolation_history_valid = false;
        int width = 0, height = 0;
      };

#if !TARGET_OS_IPHONE
      struct MetalReflectionTargets {
        id<MTLTexture> origin = nil;
        id<MTLTexture> optical_normal = nil;
        id<MTLTexture> depth = nil;
        id<MTLTexture> radiance = nil;
        id<MTLTexture> hit_normal = nil;
        id<MTLTexture> hit_distance = nil;
        id<MTLTexture> validity = nil;
        int width = 0;
        int height = 0;
      };
#endif

      using MetalFrameArena = metal4::FrameArena;
      using MetalArgumentTables = metal4::ArgumentTables;

      struct MetalFrameEncoding {
        // A single scene encoder is shared by terrain, water, and scene
        // passes.  The stream is likewise shared by world DrawLists and HUD.
        int slot = 0;
        uint64_t sequence = 0;
        MetalFrameArena arena[FRAMES_IN_FLIGHT];
        MetalArgumentTables arguments;
        MTLGPUAddress frame_uniforms = 0;
        id<MTL4CommandBuffer> command_buffer = nil;
        id<MTL4CommandAllocator> command_allocators[FRAMES_IN_FLIGHT] {};
        id<MTLSharedEvent> completion_event = nil;
        id<MTL4RenderCommandEncoder> scene_encoder = nil;
        id<CAMetalDrawable> drawable = nil;
        id<CAMetalDrawable> pending_drawable = nil;
        id<MTLTexture> current_scene = nil;
        bool scene_pass_done = false;
        bool reconstructed = false;
        FrameParams params;
        MoppeFrameUniforms uniforms;
        Mat4 current_sky_view_proj;
        Mat4 previous_sky_view_proj;
        int width_pts = 0, height_pts = 0;
        float scale = 1.0f;
        float edr_headroom = 1.0f;
        float interpolation_delta_time = 1.0f / 60.0f;
        float jitter_x = 0.0f, jitter_y = 0.0f;
        std::string screenshot_path;

        // Timestamp state stays whole-frame so pass labels retain their
        // benchmark meaning even though their encoder ownership is split.
        bool profile_this_frame = false;
        id<MTL4CounterHeap> timestamp_heaps[FRAMES_IN_FLIGHT] {};
        // One label for each interval between adjacent timestamp samples.
        // A pass transition in a shared encoder closes the former interval;
        // an encoder end closes the final one.
        std::vector<GpuPass> sample_intervals;
        int timestamp_count = 0;
        GpuPass current_gpu_pass = GpuPass::Count;
        MTL4TimestampGranularity timestamp_granularity =
          MTL4TimestampGranularityRelaxed;
        double timestamp_ms_per_tick = 0.0;

#if !TARGET_OS_IPHONE
        bool capture_active = false;
        int capture_frames = 0;
        int capture_frame_limit = 120;
        int capture_start_frames = 0;
        std::string capture_path;
#endif
      };

      void bind_address (MetalFrameEncoding& frame,
                         MTLRenderStages stage,
                         NSUInteger index,
                         MTLGPUAddress address) {
        metal4::bind_address (frame.arguments, stage, index, address);
      }

      void bind_texture (MetalFrameEncoding& frame,
                         MTLRenderStages stage,
                         NSUInteger index,
                         id<MTLTexture> texture) {
        metal4::bind_texture (frame.arguments, stage, index, texture);
      }

      void bind_sampler (MetalFrameEncoding& frame,
                         MTLRenderStages stage,
                         NSUInteger index,
                         id<MTLSamplerState> sampler) {
        metal4::bind_sampler (frame.arguments, stage, index, sampler);
      }

      void use_arguments (id<MTL4RenderCommandEncoder> encoder,
                          MetalFrameEncoding& frame,
                          MTLRenderStages stages) {
        metal4::use_arguments (encoder, frame.arguments, stages);
      }

      using metal4::wait_for_render_or_blit_writes;
      using metal4::wait_for_render_writes;

      void record_gpu_pass_start (MetalFrameEncoding& frame,
                                  id<MTL4RenderCommandEncoder> encoder,
                                  GpuPass pass) {
        if (!frame.profile_this_frame || !frame.timestamp_heaps[frame.slot] ||
            pass == frame.current_gpu_pass ||
            frame.timestamp_count >= MAX_TIMESTAMP_SAMPLES - 1)
          return;
        if (frame.current_gpu_pass == GpuPass::Count &&
            frame.timestamp_count > 0) {
          // The previous pass's end sample is this pass's start sample. Do
          // not create an unlabeled interval between adjacent encoders.
          frame.current_gpu_pass = pass;
          return;
        }
        [encoder writeTimestampWithGranularity:frame.timestamp_granularity
                                    afterStage:MTLRenderStageFragment
                                      intoHeap:frame.timestamp_heaps[frame.slot]
                                       atIndex:frame.timestamp_count];
        if (frame.timestamp_count > 0 &&
            frame.current_gpu_pass != GpuPass::Count)
          frame.sample_intervals.push_back (frame.current_gpu_pass);
        ++frame.timestamp_count;
        frame.current_gpu_pass = pass;
      }

      void record_gpu_pass_end (MetalFrameEncoding& frame,
                                id<MTL4RenderCommandEncoder> encoder) {
        if (!frame.profile_this_frame || !frame.timestamp_heaps[frame.slot] ||
            frame.timestamp_count == 0 ||
            frame.timestamp_count >= MAX_TIMESTAMP_SAMPLES ||
            frame.current_gpu_pass == GpuPass::Count)
          return;
        [encoder writeTimestampWithGranularity:frame.timestamp_granularity
                                    afterStage:MTLRenderStageFragment
                                      intoHeap:frame.timestamp_heaps[frame.slot]
                                       atIndex:frame.timestamp_count];
        frame.sample_intervals.push_back (frame.current_gpu_pass);
        ++frame.timestamp_count;
        frame.current_gpu_pass = GpuPass::Count;
      }

#if !TARGET_OS_TV
      void record_gpu_pass_start (MetalFrameEncoding& frame, GpuPass pass) {
        if (!frame.profile_this_frame || !frame.timestamp_heaps[frame.slot] ||
            pass == frame.current_gpu_pass ||
            frame.timestamp_count >= MAX_TIMESTAMP_SAMPLES - 1)
          return;
        if (frame.current_gpu_pass == GpuPass::Count &&
            frame.timestamp_count > 0) {
          frame.current_gpu_pass = pass;
          return;
        }
        [frame.command_buffer
          writeTimestampIntoHeap:frame.timestamp_heaps[frame.slot]
                         atIndex:frame.timestamp_count];
        if (frame.timestamp_count > 0 &&
            frame.current_gpu_pass != GpuPass::Count)
          frame.sample_intervals.push_back (frame.current_gpu_pass);
        ++frame.timestamp_count;
        frame.current_gpu_pass = pass;
      }

      void record_gpu_pass_end (MetalFrameEncoding& frame) {
        if (!frame.profile_this_frame || !frame.timestamp_heaps[frame.slot] ||
            frame.timestamp_count == 0 ||
            frame.timestamp_count >= MAX_TIMESTAMP_SAMPLES ||
            frame.current_gpu_pass == GpuPass::Count)
          return;
        [frame.command_buffer
          writeTimestampIntoHeap:frame.timestamp_heaps[frame.slot]
                         atIndex:frame.timestamp_count];
        frame.sample_intervals.push_back (frame.current_gpu_pass);
        ++frame.timestamp_count;
        frame.current_gpu_pass = GpuPass::Count;
      }
#endif

      struct MetalTerrainPassInputs {
        id<MTL4RenderCommandEncoder> encoder;
        const MetalPipelines& pipelines;
        MetalTerrainResources& terrain;
        const MetalWaterResources& water;
        MetalFrameEncoding& frame;
      };

      struct MetalWaterPassInputs {
        id<MTL4RenderCommandEncoder> encoder;
        const MetalPipelines& pipelines;
        const MetalTerrainResources& terrain;
        const MetalWaterResources& water;
        MetalFrameEncoding& frame;
      };

      struct MetalDrawListInputs {
        id<MTLDevice> device;
        id<MTL4RenderCommandEncoder> encoder;
        const MetalPipelines& pipelines;
        const MetalTerrainResources& terrain;
        MetalFrameEncoding& frame;
      };

      struct MetalScenePassInputs {
        id<MTLDevice> device;
        id<MTLResidencySet> residency;
        id<MTL4RenderCommandEncoder> encoder;
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
                                const OceanParams& params,
                                id<MTLRenderPipelineState> ocean = nil,
                                id<MTLRenderPipelineState> tiles = nil);
        static void draw_waterfalls (const MetalWaterPassInputs& inputs,
                                     const Mesh& mesh,
                                     const Mat4& model);
      };

      class MetalDrawListEncoder {
      public:
        static void play (const MetalDrawListInputs& inputs,
                          const std::vector<Vertex>& vertices,
                          const std::vector<DrawList::Run>& runs,
                          bool hud,
                          const std::vector<Vertex>* previous = nullptr);

        static MTLGPUAddress
        stream_vertices (MetalFrameEncoding& frame,
                         const std::vector<Vertex>& vertices);
        static void set_run_state (id<MTL4RenderCommandEncoder> encoder,
                                   const MetalPipelines& pipelines,
                                   const MetalTerrainResources& terrain,
                                   MetalFrameEncoding& frame,
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
                               const DrawList& list,
                               const std::vector<Vertex>& previous,
                               float reactive);
        static void draw_mesh (const MetalScenePassInputs& inputs,
                               const Mesh& mesh,
                               const Mat4& model,
                               const Mat4& previous_model,
                               float reactive);
      };

      class MetalPostPass {
      public:
        static void apply_gtao (const MetalPostPassInputs& inputs,
                                const MoppeGtaoUniforms& gtao,
                                id<MTLTexture> scene_depth);
        static void apply_light_shafts (const MetalPostPassInputs& inputs,
                                        const MoppeShaftUniforms& shaft,
                                        id<MTLTexture> scene_depth,
                                        id<MTLTexture> shadow_map);
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
      MetalRenderer (CAMetalLayer* layer,
                     const std::string& lib_path,
                     int requested_msaa,
                     bool request_frame_interpolation);
      ~MetalRenderer () override;

      // resources
      TexturePtr create_texture (const TextureDesc& desc,
                                 const void* pixels) override;
      MeshPtr create_mesh (const DrawList& recorded) override;

      // world setup
      void
      set_terrain (const TerrainParams& params,
                   std::span<const terrain::SurfaceElevation> heights,
                   std::span<const terrain::TerrainNormal> normals) override;
      void set_terrain_topology_overlay (bool enabled) override;
      void set_terrain_textures (TexturePtr grass,
                                 TexturePtr dirt,
                                 TexturePtr rock,
                                 TexturePtr snow) override;
      void set_terrain_overlay (const TerrainOverlayParams& params,
                                std::span<const float> values) override;
      void clear_terrain_overlay () override;
      void render_terrain_shadow (const Mat4& light_view_proj,
                                  bool include_forest) override;
      void render_local_shadow (const LocalShadowParams& params) override;
      void set_ocean (const OceanSetup& setup,
                      const render::TexturePixels& water_levels) override;
      void set_water_flow (const render::TexturePixels& flow) override;
      void set_terrain_moisture (const render::TexturePixels&) override;
      void set_terrain_forest (const render::TexturePixels&) override;
      void set_terrain_snow_support (const render::TexturePixels&) override;
      void set_terrain_channel_flux (const render::TexturePixels&) override;
      void set_terrain_geology (const render::TexturePixels&) override;
      void set_terrain_shore (const render::TexturePixels&) override;
      void set_terrain_paths (const render::TexturePixels&) override;
      void set_forest (const ForestSetup& setup,
                       std::span<const ForestInstance> instances) override;

      // Shared upload path for typed texture descriptions.
      bool upload_pixels (__strong id<MTLTexture>& texture,
                          const render::TexturePixels& pixels,
                          render::PixelFormat expected,
                          MTLPixelFormat format);
      void blit_into (id<MTLTexture> texture,
                      id<MTLBuffer> staging,
                      int w,
                      int h,
                      int bytes_per_pixel,
                      bool gen_mips);
      void make_resident (id<MTLAllocation> allocation);
      void submit_and_wait (id<MTL4CommandBuffer> command_buffer);

      // frame
      bool begin_frame (const FrameParams& params) override;
      void set_next_drawable (id<CAMetalDrawable> drawable) {
        m_frame.pending_drawable = drawable;
      }
      void set_edr_headroom (float headroom) {
        m_frame.edr_headroom = std::max (1.0f, headroom);
      }
      bool frame_interpolation_supported () const {
        return m_frame_interpolation_requested &&
               m_frame_interpolation_supported;
      }
      bool frame_interpolation_active () const {
        return m_frame_interpolation_enabled && m_targets.frame_interpolator;
      }
      void set_frame_interpolation_enabled (bool enabled) {
        const bool resolved = enabled && frame_interpolation_supported ();
        if (resolved == m_frame_interpolation_enabled)
          return;
        m_frame_interpolation_enabled = resolved;
        m_targets.interpolation_history_valid = false;
      }
      void set_frame_delta_time (float delta_time) {
        if (std::isfinite (delta_time) && delta_time > 0.0f)
          m_frame.interpolation_delta_time = delta_time;
      }
      bool present_rendered_frame (id<CAMetalDrawable> drawable);
      void draw_terrain (const ChunkDraw* chunks, int count) override;
      void draw_sky (const SkyParams& params) override;
      void draw_ocean (const OceanParams& params) override;
      void draw_dust (std::span<const DustEmission> emissions,
                      float logical_time) override;
      void draw_undergrowth (const UndergrowthParams& params) override;
      void draw_forest () override;
      void draw_waterfalls (const Mesh& mesh, const Mat4& model) override;
      void draw_mesh (const Mesh& mesh,
                      const Mat4& model,
                      uint64_t motion_id = 0) override;
      void draw_list (const DrawList& list, uint64_t motion_id = 0) override;
      void reconstruct_scene () override;
      void apply_gtao (const GtaoParams& params) override;
      void apply_light_shafts (const LightShaftParams& params) override;
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
      void ensure_targets (float requested_scale,
                           float scale_override,
                           float megapixel_budget,
                           UpscalingMode upscaling);
      id<MTLTexture> make_target (MTLPixelFormat fmt,
                                  int w,
                                  int h,
                                  int samples,
                                  bool memoryless,
                                  MTLTextureUsage additional_usage = 0);
      void retire_scene_targets ();
      void upload_texture (id<MTLTexture> tex,
                           const void* pixels,
                           int w,
                           int h,
                           int bytes_per_pixel,
                           bool gen_mips);
      id<MTLBuffer> create_private_buffer (const void* bytes,
                                           std::size_t size,
                                           NSString* label);
      id<MTLRenderPipelineState> make_pipeline (NSString* vs,
                                                NSString* fs,
                                                MTLPixelFormat color,
                                                MTLPixelFormat depth,
                                                int samples,
                                                bool blend,
                                                bool additive = false,
                                                bool temporal_outputs = false);

      // scene-pass encoding helpers
      id<MTL4RenderCommandEncoder> scene_encoder ();
      void end_scene_encoder ();
      void update_exposure ();
      void begin_gpu_pass (id<MTL4RenderCommandEncoder> enc, GpuPass pass);
#if !TARGET_OS_IPHONE
      bool reflection_requested () const;
      void build_reflection_geometry ();
      void retire_reflection_geometry ();
      void write_reflection_geometry_report () const;
      void draw_water_reflection_signal (id<MTLBuffer> __strong& diagnostic,
                                         std::size_t& row_bytes,
                                         int& width,
                                         int& height);
      void write_water_reflection_report (const void* diagnostic,
                                          std::size_t row_bytes,
                                          int width,
                                          int height) const;
      void benchmark_water_reflection_query ();
#endif

      CAMetalLayer* m_layer;
      id<MTLDevice> m_device;
      id<MTL4CommandQueue> m_queue;
      id<MTL4CommandAllocator> m_present_allocators[FRAMES_IN_FLIGHT] {};
      id<MTLSharedEvent> m_present_completion_event;
      uint64_t m_present_sequence = 0;
      id<MTLResidencySet> m_residency;
      id<MTLLibrary> m_library;
      id<MTL4Compiler> m_compiler;
      bool m_spatial_upscaling_supported = false;
      bool m_temporal_upscaling_supported = false;
      bool m_frame_interpolation_requested = false;
      bool m_frame_interpolation_supported = false;
      bool m_frame_interpolation_enabled = false;
#if !TARGET_OS_IPHONE
      id<MTL4ArgumentTable> m_reflection_arguments;
      std::string m_reflection_geometry_path;
      std::string m_water_reflection_path;
      std::vector<terrain::SurfaceElevation> m_reflection_heights;
      bool m_reflection_geometry_written = false;
      bool m_water_reflection_written = false;
      OceanParams m_reflection_ocean;
      bool m_have_reflection_ocean = false;
      double m_water_reflection_gpu_ms = -1.0;
      double m_water_reflection_query_gpu_ms = -1.0;
#endif

      MetalPipelines m_pipelines;
      MetalTerrainResources m_terrain_resources;
      MetalForestResources m_forest_resources;
      MetalWaterResources m_water_resources;
      MetalSceneResources m_scene_resources;
      MetalFrameTargets m_targets;
#if !TARGET_OS_IPHONE
      std::array<MetalReflectionTargets, FRAMES_IN_FLIGHT> m_reflection_targets;
#endif
      MetalFrameEncoding m_frame;
      bool m_memoryless_ok = false;
      // Fixed once, before the pipelines are built: every scene pipeline
      // bakes its raster sample count, so this cannot follow a hot setting.
      int m_msaa_samples = DEFAULT_MSAA_SAMPLES;
      bool m_temporal_scene_pipelines = false;
      Mat4 m_previous_view_proj;
      Mat4 m_current_view_proj;
      Mat4 m_previous_sky_view_proj;
      Mat4 m_current_sky_view_proj;
      float m_previous_time = 0.0f;
      bool m_camera_history_valid = false;
      std::unordered_map<uint64_t, Mat4> m_previous_models;
      std::unordered_map<uint64_t, Mat4> m_current_models;
      std::unordered_map<uint64_t, std::vector<Vertex>> m_previous_lists;
      std::unordered_map<uint64_t, std::vector<Vertex>> m_current_lists;
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

    MetalRenderer::MetalRenderer (CAMetalLayer* layer,
                                  const std::string& lib_path,
                                  int requested_msaa,
                                  bool request_frame_interpolation) {
      m_layer = layer;
      m_device = layer.device ? layer.device : MTLCreateSystemDefaultDevice ();
      m_queue = [m_device newMTL4CommandQueue];
      if (!m_queue)
        throw std::runtime_error ("Metal 4 is unavailable");
      const uint64_t timestamp_frequency = [m_device queryTimestampFrequency];
      if (timestamp_frequency > 0)
        m_frame.timestamp_ms_per_tick = 1000.0 / timestamp_frequency;
      m_spatial_upscaling_supported =
        [MTLFXSpatialScalerDescriptor supportsMetal4FX:m_device];
      m_temporal_upscaling_supported =
        [MTLFXTemporalScalerDescriptor supportsMetal4FX:m_device];
      m_frame_interpolation_requested = request_frame_interpolation;
      m_frame_interpolation_supported =
        [MTLFXFrameInterpolatorDescriptor supportsMetal4FX:m_device];
      if (m_spatial_upscaling_supported || m_temporal_upscaling_supported ||
          m_frame_interpolation_supported) {
        MTL4CompilerDescriptor* compiler_desc =
          [[MTL4CompilerDescriptor alloc] init];
        compiler_desc.label = @"Moppe MetalFX compiler";
        NSError* compiler_error = nil;
        m_compiler = [m_device newCompilerWithDescriptor:compiler_desc
                                                   error:&compiler_error];
        if (!m_compiler) {
          m_spatial_upscaling_supported = false;
          m_temporal_upscaling_supported = false;
          m_frame_interpolation_supported = false;
          std::cerr << "moppe: MetalFX unavailable: compiler: "
                    << (compiler_error
                          ? compiler_error.localizedDescription.UTF8String
                          : "unknown error")
                    << std::endl;
        }
      }
      m_frame.completion_event = [m_device newSharedEvent];
      m_frame.completion_event.signaledValue = 0;
      m_present_completion_event = [m_device newSharedEvent];
      m_present_completion_event.signaledValue = 0;
      for (int i = 0; i < FRAMES_IN_FLIGHT; ++i) {
        m_frame.command_allocators[i] = [m_device newCommandAllocator];
        m_present_allocators[i] = [m_device newCommandAllocator];
      }

      NSError* residency_error = nil;
      MTLResidencySetDescriptor* residency_desc =
        [[MTLResidencySetDescriptor alloc] init];
      residency_desc.initialCapacity = 192;
      residency_desc.label = @"Moppe renderer resources";
      m_residency = [m_device newResidencySetWithDescriptor:residency_desc
                                                      error:&residency_error];
      if (!m_residency)
        throw std::runtime_error (
          std::string ("Could not create Metal 4 residency set: ") +
          (residency_error ? residency_error.localizedDescription.UTF8String
                           : "unknown error"));
      [m_queue addResidencySet:m_residency];

      const bool requested_gpu_passes =
        ::getenv ("MOPPE_PROFILE_GPU") != nullptr;
      m_profile_gpu_passes = requested_gpu_passes;
      m_profile_gpu = requested_gpu_passes ||
                      ::getenv ("MOPPE_PROFILE_GPU_SIMPLE") != nullptr;
      m_profile_cpu = ::getenv ("MOPPE_PROFILE_CPU") != nullptr;
      if (m_profile_gpu)
        m_frame_timing = std::make_shared<FrameTiming> ();
      if (const char* path = ::getenv ("MOPPE_BENCHMARK_OUTPUT")) {
        m_benchmark = std::make_shared<BenchmarkOutput> ();
        m_benchmark->path = path;
        m_benchmark->pass_timing =
          ::getenv ("MOPPE_BENCHMARK_PASSES") != nullptr;
        if (const char* expected = ::getenv ("MOPPE_BENCHMARK_EXPECTED"))
          m_benchmark->expected = std::max (1, ::atoi (expected));
        if (const char* names = ::getenv ("MOPPE_BENCHMARK_FEATURES")) {
          std::istringstream input (names);
          std::string name;
          while (std::getline (input, name, ','))
            m_benchmark->feature_names.push_back (name);
        }
        if (const char* partition = ::getenv ("MOPPE_BENCHMARK_PARTITION"))
          m_benchmark->partition = partition;
        if (const char* names = ::getenv ("MOPPE_BENCHMARK_BLOCKS")) {
          std::istringstream input (names);
          std::string name;
          while (std::getline (input, name, ','))
            m_benchmark->block_names.push_back (name);
        }
      }
      // Precise timestamps may split a render encoder, so ordinary benchmark
      // runs stay minimally invasive and pass attribution is explicit.
      if (m_benchmark && m_benchmark->pass_timing) {
        m_profile_gpu_passes = true;
        m_frame.timestamp_granularity = MTL4TimestampGranularityPrecise;
      }

      if (m_profile_gpu_passes) {
        if (m_benchmark && m_benchmark->pass_timing)
          std::cerr << "moppe: GPU pass timing uses Metal 4 counter heaps; "
                       "benchmark spans may add profiling overhead"
                    << std::endl;
        else if (requested_gpu_passes)
          std::cerr << "moppe: GPU pass timing uses Metal 4 counter heaps; "
                       "spans may overlap"
                    << std::endl;
        for (int i = 0; i < FRAMES_IN_FLIGHT; ++i) {
          MTL4CounterHeapDescriptor* desc =
            [[MTL4CounterHeapDescriptor alloc] init];
          desc.type = MTL4CounterHeapTypeTimestamp;
          desc.count = MAX_TIMESTAMP_SAMPLES;
          NSError* error = nil;
          m_frame.timestamp_heaps[i] =
            [m_device newCounterHeapWithDescriptor:desc error:&error];
          m_frame.timestamp_heaps[i].label = @"Moppe frame pass timestamps";
          if (!m_frame.timestamp_heaps[i])
            throw std::runtime_error (
              std::string ("Metal 4 timestamp heap unavailable: ") +
              (error ? error.localizedDescription.UTF8String : "unknown"));
        }
      }

#if !TARGET_OS_IPHONE
      if (const char* requested = ::getenv ("MOPPE_METAL_CAPTURE")) {
        m_frame.capture_path = requested;
        if (const char* frames = ::getenv ("MOPPE_METAL_CAPTURE_FRAMES"))
          m_frame.capture_frame_limit = std::max (1, ::atoi (frames));
      }
      if (const char* requested = ::getenv ("MOPPE_REFLECTION_GEOMETRY")) {
        m_reflection_geometry_path = requested;
        if (m_reflection_geometry_path.empty ())
          throw std::invalid_argument (
            "MOPPE_REFLECTION_GEOMETRY needs an output PNG path");
        if (!m_device.supportsRaytracing)
          throw std::runtime_error (
            "Reflection geometry requested on a device without ray tracing");
      }
      if (const char* requested = ::getenv ("MOPPE_WATER_REFLECTION_SIGNAL")) {
        m_water_reflection_path = requested;
        if (m_water_reflection_path.empty ())
          throw std::invalid_argument (
            "MOPPE_WATER_REFLECTION_SIGNAL needs an output PNG path");
        if (!m_device.supportsRaytracing)
          throw std::runtime_error (
            "Water reflection requested on a device without ray tracing");
      }
#endif

      layer.device = m_device;
#if TARGET_OS_IPHONE
      layer.pixelFormat = MTLPixelFormatBGRA8Unorm;
#else
      layer.pixelFormat = MTLPixelFormatRGBA16Float;
#endif
#if !TARGET_OS_IPHONE
      {
        CGColorSpaceRef linear =
          CGColorSpaceCreateWithName (kCGColorSpaceExtendedLinearSRGB);
        layer.colorspace = linear;
        CGColorSpaceRelease (linear);
        layer.wantsExtendedDynamicRangeContent = YES;
        layer.displaySyncEnabled = YES;
        [m_queue addResidencySet:layer.residencySet];
      }
#else
      [m_queue addResidencySet:layer.residencySet];
#endif
      m_frame.scale = std::max (1.0f, (float)layer.contentsScale);

#if TARGET_OS_SIMULATOR
      m_memoryless_ok = false;
#else
      m_memoryless_ok = [m_device supportsFamily:MTLGPUFamilyApple2];
#endif
      m_pipelines.mesh_shaders_ok =
        [m_device supportsFamily:MTLGPUFamilyMetal3];

      // Read before the pipelines bake their raster sample count.  One is a
      // meaningful setting, not a disabled one: it takes the scene pass off
      // multisampled rasterization and out of a resolve entirely.
      if (![m_device supportsTextureSampleCount:m_msaa_samples]) {
        const int requested_default = m_msaa_samples;
        for (const int candidate : { 4, 2, 1 })
          if (candidate <= requested_default &&
              [m_device supportsTextureSampleCount:candidate]) {
            m_msaa_samples = candidate;
            break;
          }
        std::cerr << "moppe: default " << requested_default
                  << "x MSAA is unavailable; using " << m_msaa_samples << "x"
                  << std::endl;
      }
      if (requested_msaa > 0) {
        if ([m_device supportsTextureSampleCount:requested_msaa])
          m_msaa_samples = requested_msaa;
        else
          std::cerr << "moppe: requested " << requested_msaa
                    << "x MSAA is unavailable; keeping " << m_msaa_samples
                    << 'x' << std::endl;
      } else if (const char* text = ::getenv ("MOPPE_MSAA")) {
        const int wanted = ::atoi (text);
        if ((wanted == 1 || wanted == 2 || wanted == 4) &&
            [m_device supportsTextureSampleCount:wanted])
          m_msaa_samples = wanted;
        else
          std::cerr << "moppe: MOPPE_MSAA=" << text
                    << " is not a supported sample count; keeping "
                    << m_msaa_samples << 'x' << std::endl;
      }

      std::cerr << "moppe: Metal: device=" << m_device.name.UTF8String
                << ", frames-in-flight=" << FRAMES_IN_FLIGHT
                << ", memoryless=" << (m_memoryless_ok ? "yes" : "no")
                << ", msaa=" << m_msaa_samples << 'x' << ", mesh-shaders="
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

      // Metal 4 validation requires every statically declared argument to be
      // bound even when a uniform disables its use. A depth-typed fallback
      // keeps that contract honest for draws made before a world shadow is
      // published.
      MTLTextureDescriptor* shadow_fallback_desc = [MTLTextureDescriptor
        texture2DDescriptorWithPixelFormat:MTLPixelFormatDepth16Unorm
                                     width:1
                                    height:1
                                 mipmapped:NO];
      shadow_fallback_desc.storageMode = MTLStorageModePrivate;
      shadow_fallback_desc.usage = MTLTextureUsageShaderRead;
      m_pipelines.shadow_fallback =
        [m_device newTextureWithDescriptor:shadow_fallback_desc];
      m_pipelines.shadow_fallback.label = @"Moppe inert shadow binding";
      make_resident (m_pipelines.shadow_fallback);

      MTL4ArgumentTableDescriptor* argument_desc =
        [[MTL4ArgumentTableDescriptor alloc] init];
      argument_desc.maxBufferBindCount = 6;
      argument_desc.maxTextureBindCount = 16;
      argument_desc.maxSamplerStateBindCount = 1;
      argument_desc.initializeBindings = YES;
      auto make_arguments = [&] (NSString* label) {
        argument_desc.label = label;
        NSError* error = nil;
        id<MTL4ArgumentTable> table =
          [m_device newArgumentTableWithDescriptor:argument_desc error:&error];
        if (!table)
          throw std::runtime_error (
            std::string ("Could not create Metal 4 argument table: ") +
            (error ? error.localizedDescription.UTF8String : "unknown"));
        return table;
      };
      m_frame.arguments.vertex = make_arguments (@"Moppe vertex bindings");
      m_frame.arguments.fragment = make_arguments (@"Moppe fragment bindings");
      m_frame.arguments.object = make_arguments (@"Moppe object bindings");
      m_frame.arguments.mesh = make_arguments (@"Moppe mesh bindings");
#if !TARGET_OS_IPHONE
      if (m_pipelines.reflection_geometry ||
          m_pipelines.water_reflection_signal)
        m_reflection_arguments =
          make_arguments (@"Moppe reflection atelier bindings");
#endif

      for (int i = 0; i < FRAMES_IN_FLIGHT; ++i) {
        m_frame.arena[i].device = m_device;
        m_frame.arena[i].residency = m_residency;
        m_frame.arena[i].buffer =
          [m_device newBufferWithLength:FRAME_ARENA_CAPACITY
                                options:MTLResourceStorageModeShared];
        m_frame.arena[i].buffer.label =
          [NSString stringWithFormat:@"Moppe frame arena %d", i];
        [m_residency addAllocation:m_frame.arena[i].buffer];
      }
      [m_residency commit];

      // 1x1 white fallback texture.
      TextureDesc wd;
      wd.width = wd.height = 1;
      wd.format = TextureFormat::RGBA8;
      wd.filter = TextureFilter::Nearest;
      const uint8_t white[4] = { 255, 255, 255, 255 };
      m_pipelines.white = create_texture (wd, white);

      // Post shaders statically declare all inputs, so Metal 4 requires a
      // valid bloom texture even when the pass is disabled. More importantly,
      // the shared argument table otherwise retains terrain's dirt texture in
      // slot 1 and the present shader adds it over the entire image.
      const uint8_t black[4] = { 0, 0, 0, 0 };
      m_pipelines.black = create_texture (wd, black);
    }

    MetalRenderer::~MetalRenderer () {
      if (m_frame.sequence)
        [m_frame.completion_event waitUntilSignaledValue:m_frame.sequence
                                               timeoutMS:5000];
#if !TARGET_OS_IPHONE
      retire_reflection_geometry ();
#endif
    }

    id<MTLRenderPipelineState>
    MetalRenderer::make_pipeline (NSString* vs,
                                  NSString* fs,
                                  MTLPixelFormat color,
                                  MTLPixelFormat depth,
                                  int samples,
                                  bool blend,
                                  bool additive,
                                  bool temporal_outputs) {
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
      if (temporal_outputs) {
        d.colorAttachments[1].pixelFormat = MTLPixelFormatRG16Float;
        d.colorAttachments[2].pixelFormat = MTLPixelFormatR8Unorm;
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
      const int scene_samples = m_temporal_scene_pipelines ? 1 : m_msaa_samples;
      const MTLPixelFormat depth = m_temporal_scene_pipelines
                                     ? MTLPixelFormatDepth32Float
                                     : MTLPixelFormatDepth32Float_Stencil8;

#if !TARGET_OS_IPHONE
      if (!m_reflection_geometry_path.empty ()) {
        id<MTLFunction> function =
          [m_library newFunctionWithName:@"reflection_geometry_atelier"];
        NSError* error = nil;
        if (function)
          m_pipelines.reflection_geometry =
            [m_device newComputePipelineStateWithFunction:function
                                                    error:&error];
        if (!m_pipelines.reflection_geometry)
          throw std::runtime_error (
            std::string ("Could not build reflection geometry pipeline: ") +
            (error ? error.localizedDescription.UTF8String
                   : "shader function missing"));
      }
      if (!m_water_reflection_path.empty ()) {
        MTLRenderPipelineDescriptor* input =
          [[MTLRenderPipelineDescriptor alloc] init];
        input.vertexFunction = [m_library newFunctionWithName:@"ocean_vertex"];
        input.fragmentFunction =
          [m_library newFunctionWithName:@"reflection_water_input_fragment"];
        input.rasterSampleCount = 1;
        input.colorAttachments[0].pixelFormat = MTLPixelFormatRGBA32Float;
        input.colorAttachments[1].pixelFormat = MTLPixelFormatRGBA16Float;
        input.depthAttachmentPixelFormat = MTLPixelFormatDepth32Float;
        NSError* error = nil;
        if (input.vertexFunction && input.fragmentFunction)
          m_pipelines.reflection_water_input =
            [m_device newRenderPipelineStateWithDescriptor:input error:&error];
        if (!m_pipelines.reflection_water_input)
          throw std::runtime_error (
            std::string ("Could not build reflection water-input pipeline: ") +
            (error ? error.localizedDescription.UTF8String
                   : "shader function missing"));

        const auto make_compute = [&] (NSString* name) {
          id<MTLFunction> function = [m_library newFunctionWithName:name];
          NSError* compute_error = nil;
          id<MTLComputePipelineState> result =
            function
              ? [m_device newComputePipelineStateWithFunction:function
                                                        error:&compute_error]
              : nil;
          if (!result)
            throw std::runtime_error (
              std::string ("Could not build ") + name.UTF8String + ": " +
              (compute_error ? compute_error.localizedDescription.UTF8String
                             : "shader function missing"));
          return result;
        };
        m_pipelines.water_reflection_signal =
          make_compute (@"water_reflection_signal");
        m_pipelines.water_reflection_diagnostic =
          make_compute (@"water_reflection_diagnostic");
      }
#endif

      m_pipelines.uber_opaque = make_pipeline (@"uber_vertex",
                                               @"uber_fragment",
                                               scene,
                                               depth,
                                               scene_samples,
                                               false,
                                               false,
                                               m_temporal_scene_pipelines);
      m_pipelines.uber_blend = make_pipeline (@"uber_vertex",
                                              @"uber_fragment",
                                              scene,
                                              depth,
                                              scene_samples,
                                              true,
                                              false,
                                              m_temporal_scene_pipelines);
      m_pipelines.uber_add = make_pipeline (@"uber_vertex",
                                            @"uber_fragment",
                                            scene,
                                            depth,
                                            scene_samples,
                                            true,
                                            true,
                                            m_temporal_scene_pipelines);
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
      m_pipelines.shafts = make_pipeline (@"quad_vertex",
                                          @"shafts_gather_fragment",
                                          scene,
                                          MTLPixelFormatInvalid,
                                          1,
                                          false);
      m_pipelines.shafts_add = make_pipeline (@"quad_vertex",
                                              @"shafts_add_fragment",
                                              scene,
                                              MTLPixelFormatInvalid,
                                              1,
                                              false);
      m_pipelines.gtao = make_pipeline (@"quad_vertex",
                                        @"gtao_gather_fragment",
                                        MTLPixelFormatR8Unorm,
                                        MTLPixelFormatInvalid,
                                        1,
                                        false);
      m_pipelines.gtao_blur = make_pipeline (@"quad_vertex",
                                             @"gtao_blur_fragment",
                                             MTLPixelFormatR8Unorm,
                                             MTLPixelFormatInvalid,
                                             1,
                                             false);
      m_pipelines.gtao_apply = make_pipeline (@"quad_vertex",
                                              @"gtao_apply_fragment",
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
      m_pipelines.exposure = make_pipeline (@"quad_vertex",
                                            @"exposure_fragment",
                                            MTLPixelFormatR16Float,
                                            MTLPixelFormatInvalid,
                                            1,
                                            false);
      m_pipelines.terrain = make_pipeline (@"terrain_vertex",
                                           @"terrain_fragment",
                                           scene,
                                           depth,
                                           scene_samples,
                                           false,
                                           false,
                                           m_temporal_scene_pipelines);
      m_pipelines.terrain_shadow = make_pipeline (@"terrain_shadow_vertex",
                                                  nil,
                                                  MTLPixelFormatInvalid,
                                                  MTLPixelFormatDepth16Unorm,
                                                  1,
                                                  false);
      m_pipelines.sky = make_pipeline (@"sky_vertex",
                                       @"sky_fragment",
                                       scene,
                                       depth,
                                       scene_samples,
                                       false,
                                       false,
                                       m_temporal_scene_pipelines);
      m_pipelines.ocean = make_pipeline (@"ocean_vertex",
                                         @"ocean_fragment",
                                         scene,
                                         depth,
                                         scene_samples,
                                         true,
                                         false,
                                         m_temporal_scene_pipelines);
      m_pipelines.dust_soft = make_pipeline (@"dust_vertex",
                                             @"dust_fragment",
                                             scene,
                                             depth,
                                             scene_samples,
                                             true,
                                             false,
                                             m_temporal_scene_pipelines);
      m_pipelines.dust_add = make_pipeline (@"dust_vertex",
                                            @"dust_fragment",
                                            scene,
                                            depth,
                                            scene_samples,
                                            true,
                                            true,
                                            m_temporal_scene_pipelines);
      if (m_pipelines.mesh_shaders_ok) {
        const auto make_dust_mesh = [&] (bool additive) {
          MTLMeshRenderPipelineDescriptor* p =
            [[MTLMeshRenderPipelineDescriptor alloc] init];
          p.meshFunction = [m_library newFunctionWithName:@"dust_mesh"];
          p.fragmentFunction = [m_library newFunctionWithName:@"dust_fragment"];
          p.rasterSampleCount = scene_samples;
          p.colorAttachments[0].pixelFormat = scene;
          if (m_temporal_scene_pipelines) {
            p.colorAttachments[1].pixelFormat = MTLPixelFormatRG16Float;
            p.colorAttachments[2].pixelFormat = MTLPixelFormatR8Unorm;
          }
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
          if (!m_temporal_scene_pipelines)
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

        // Lattice water tiles: the near horizontal-water surface on the
        // terrain's own sample grid, sharing the ocean fragment shader.
        MTLMeshRenderPipelineDescriptor* w =
          [[MTLMeshRenderPipelineDescriptor alloc] init];
        w.objectFunction = [m_library newFunctionWithName:@"water_tile_object"];
        w.meshFunction = [m_library newFunctionWithName:@"water_tile_mesh"];
        w.fragmentFunction = [m_library newFunctionWithName:@"ocean_fragment"];
        w.rasterSampleCount = scene_samples;
        w.colorAttachments[0].pixelFormat = scene;
        if (m_temporal_scene_pipelines) {
          w.colorAttachments[1].pixelFormat = MTLPixelFormatRG16Float;
          w.colorAttachments[2].pixelFormat = MTLPixelFormatR8Unorm;
        }
        w.colorAttachments[0].blendingEnabled = YES;
        w.colorAttachments[0].sourceRGBBlendFactor = MTLBlendFactorSourceAlpha;
        w.colorAttachments[0].destinationRGBBlendFactor =
          MTLBlendFactorOneMinusSourceAlpha;
        w.colorAttachments[0].sourceAlphaBlendFactor =
          MTLBlendFactorSourceAlpha;
        w.colorAttachments[0].destinationAlphaBlendFactor =
          MTLBlendFactorOneMinusSourceAlpha;
        w.depthAttachmentPixelFormat = depth;
        if (!m_temporal_scene_pipelines)
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

#if !TARGET_OS_IPHONE
        if (!m_water_reflection_path.empty ()) {
          MTLMeshRenderPipelineDescriptor* reflection =
            [[MTLMeshRenderPipelineDescriptor alloc] init];
          reflection.objectFunction =
            [m_library newFunctionWithName:@"water_tile_object"];
          reflection.meshFunction =
            [m_library newFunctionWithName:@"water_tile_mesh"];
          reflection.fragmentFunction =
            [m_library newFunctionWithName:@"reflection_water_input_fragment"];
          reflection.rasterSampleCount = 1;
          reflection.colorAttachments[0].pixelFormat =
            MTLPixelFormatRGBA32Float;
          reflection.colorAttachments[1].pixelFormat =
            MTLPixelFormatRGBA16Float;
          reflection.depthAttachmentPixelFormat = MTLPixelFormatDepth32Float;
          reflection.payloadMemoryLength = 1024;
          reflection.maxTotalThreadsPerObjectThreadgroup = 64;
          reflection.maxTotalThreadsPerMeshThreadgroup = 256;
          if (reflection.objectFunction && reflection.meshFunction &&
              reflection.fragmentFunction) {
            NSError* error = nil;
            m_pipelines.reflection_water_tiles = [m_device
              newRenderPipelineStateWithMeshDescriptor:reflection
                                               options:MTLPipelineOptionNone
                                            reflection:nil
                                                 error:&error];
            if (!m_pipelines.reflection_water_tiles)
              throw std::runtime_error (
                std::string ("Could not build reflection water tiles: ") +
                (error ? error.localizedDescription.UTF8String : "unknown"));
          }
        }
#endif

        // Undergrowth: grass and occasional ferns generated per frame from
        // the world's own fields. Opaque, depth-written, no vertex buffer
        // at all -- the object stage decides which ground is worth a
        // threadgroup and the mesh stage grows the shoots.
        MTLMeshRenderPipelineDescriptor* g =
          [[MTLMeshRenderPipelineDescriptor alloc] init];
        g.objectFunction =
          [m_library newFunctionWithName:@"undergrowth_object"];
        g.meshFunction = [m_library newFunctionWithName:@"undergrowth_mesh"];
        g.fragmentFunction =
          [m_library newFunctionWithName:@"undergrowth_fragment"];
        g.rasterSampleCount = scene_samples;
        g.colorAttachments[0].pixelFormat = scene;
        if (m_temporal_scene_pipelines) {
          g.colorAttachments[1].pixelFormat = MTLPixelFormatRG16Float;
          g.colorAttachments[2].pixelFormat = MTLPixelFormatR8Unorm;
        }
        g.depthAttachmentPixelFormat = depth;
        if (!m_temporal_scene_pipelines)
          g.stencilAttachmentPixelFormat = depth;
        g.payloadMemoryLength = 1024;
        g.maxTotalThreadsPerObjectThreadgroup = 64;
        g.maxTotalThreadsPerMeshThreadgroup = MOPPE_UNDERGROWTH_MESH_THREADS;
        if (g.objectFunction && g.meshFunction && g.fragmentFunction) {
          NSError* error = nil;
          m_pipelines.undergrowth = [m_device
            newRenderPipelineStateWithMeshDescriptor:g
                                             options:MTLPipelineOptionNone
                                          reflection:nil
                                               error:&error];
          if (!m_pipelines.undergrowth)
            std::cerr << "moppe: undergrowth pipeline failed: "
                      << (error ? error.localizedDescription.UTF8String : "?")
                      << std::endl;
        }

        // Trees are compact instances expanded into reusable organs. The
        // object stage makes the projected-detail decision; the mesh stage
        // never sees or retains a complete tree mesh.
        MTLMeshRenderPipelineDescriptor* forest =
          [[MTLMeshRenderPipelineDescriptor alloc] init];
        forest.objectFunction =
          [m_library newFunctionWithName:@"forest_object"];
        forest.meshFunction = [m_library newFunctionWithName:@"forest_mesh"];
        forest.fragmentFunction =
          [m_library newFunctionWithName:@"forest_fragment"];
        forest.rasterSampleCount = scene_samples;
        forest.colorAttachments[0].pixelFormat = scene;
        if (m_temporal_scene_pipelines) {
          forest.colorAttachments[1].pixelFormat = MTLPixelFormatRG16Float;
          forest.colorAttachments[2].pixelFormat = MTLPixelFormatR8Unorm;
        }
        forest.depthAttachmentPixelFormat = depth;
        if (!m_temporal_scene_pipelines)
          forest.stencilAttachmentPixelFormat = depth;
        forest.payloadMemoryLength = 2048;
        forest.maxTotalThreadsPerObjectThreadgroup =
          MOPPE_FOREST_OBJECT_THREADS;
        forest.maxTotalThreadsPerMeshThreadgroup = MOPPE_FOREST_MESH_THREADS;
        if (forest.objectFunction && forest.meshFunction &&
            forest.fragmentFunction) {
          NSError* error = nil;
          m_pipelines.forest = [m_device
            newRenderPipelineStateWithMeshDescriptor:forest
                                             options:MTLPipelineOptionNone
                                          reflection:nil
                                               error:&error];
          if (!m_pipelines.forest)
            throw std::runtime_error (
              std::string ("Could not build forest pipeline: ") +
              (error ? error.localizedDescription.UTF8String : "unknown"));
        }

        MTLMeshRenderPipelineDescriptor* forest_shadow =
          [[MTLMeshRenderPipelineDescriptor alloc] init];
        forest_shadow.objectFunction =
          [m_library newFunctionWithName:@"forest_shadow_object"];
        forest_shadow.meshFunction =
          [m_library newFunctionWithName:@"forest_shadow_mesh"];
        forest_shadow.rasterSampleCount = 1;
        forest_shadow.depthAttachmentPixelFormat = MTLPixelFormatDepth16Unorm;
        forest_shadow.payloadMemoryLength = 1024;
        forest_shadow.maxTotalThreadsPerObjectThreadgroup =
          MOPPE_FOREST_OBJECT_THREADS;
        forest_shadow.maxTotalThreadsPerMeshThreadgroup =
          MOPPE_FOREST_MESH_THREADS;
        if (forest_shadow.objectFunction && forest_shadow.meshFunction) {
          NSError* error = nil;
          m_pipelines.forest_shadow = [m_device
            newRenderPipelineStateWithMeshDescriptor:forest_shadow
                                             options:MTLPipelineOptionNone
                                          reflection:nil
                                               error:&error];
          if (!m_pipelines.forest_shadow)
            throw std::runtime_error (
              std::string ("Could not build forest shadow pipeline: ") +
              (error ? error.localizedDescription.UTF8String : "unknown"));
        }
      }
      m_pipelines.river = make_pipeline (@"river_vertex",
                                         @"river_fragment",
                                         scene,
                                         depth,
                                         scene_samples,
                                         true,
                                         false,
                                         m_temporal_scene_pipelines);

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
        // This descriptor is the recipe for the immutable sampler states
        // below. Metal 4 argument tables bind those states by resource ID;
        // Apple's opt-in for that indirect use is still named
        // supportArgumentBuffers. Live rendering worked without it, but
        // Xcode Metal capture replay segfaulted, so every table-bound sampler
        // must enable it.
        d.supportArgumentBuffers = YES;
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
      make_resident (staging);
      blit_into (tex, staging, w, h, bpp, gen_mips);
      [m_residency removeAllocation:staging];
      [m_residency commit];
    }

    void MetalRenderer::make_resident (id<MTLAllocation> allocation) {
      if (!allocation)
        return;
      [m_residency addAllocation:allocation];
      [m_residency commit];
    }

    void MetalRenderer::submit_and_wait (id<MTL4CommandBuffer> command_buffer) {
      id<MTLSharedEvent> completed = [m_device newSharedEvent];
      [command_buffer endCommandBuffer];
      const id<MTL4CommandBuffer> commands[] = { command_buffer };
      [m_queue commit:commands count:1];
      [m_queue signalEvent:completed value:1];
      if (![completed waitUntilSignaledValue:1 timeoutMS:5000])
        throw std::runtime_error ("Timed out waiting for Metal 4 submission");
    }

    id<MTLBuffer> MetalRenderer::create_private_buffer (const void* bytes,
                                                        std::size_t size,
                                                        NSString* label) {
      if (!bytes || size == 0)
        return nil;
      id<MTLBuffer> staging =
        [m_device newBufferWithBytes:bytes
                              length:size
                             options:MTLResourceStorageModeShared];
      id<MTLBuffer> buffer =
        [m_device newBufferWithLength:size
                              options:MTLResourceStorageModePrivate];
      buffer.label = label;
      make_resident (staging);
      make_resident (buffer);
      id<MTL4CommandAllocator> allocator = [m_device newCommandAllocator];
      id<MTL4CommandBuffer> command = [m_device newCommandBuffer];
      [command beginCommandBufferWithAllocator:allocator];
      id<MTL4ComputeCommandEncoder> copy = [command computeCommandEncoder];
      [copy copyFromBuffer:staging
              sourceOffset:0
                  toBuffer:buffer
         destinationOffset:0
                      size:size];
      [copy endEncoding];
      submit_and_wait (command);
      [m_residency removeAllocation:staging];
      [m_residency commit];
      return buffer;
    }

#if !TARGET_OS_IPHONE
    bool MetalRenderer::reflection_requested () const {
      return !m_reflection_geometry_path.empty () ||
             !m_water_reflection_path.empty ();
    }

    void MetalRenderer::retire_reflection_geometry () {
      if (m_terrain_resources.reflection_structure && m_frame.sequence &&
          ![m_frame.completion_event waitUntilSignaledValue:m_frame.sequence
                                                  timeoutMS:5000])
        throw std::runtime_error (
          "Timed out retiring reflection geometry from an in-flight frame");
      if (m_terrain_resources.reflection_structure)
        [m_residency removeAllocation:m_terrain_resources.reflection_structure];
      if (m_terrain_resources.reflection_vertices)
        [m_residency removeAllocation:m_terrain_resources.reflection_vertices];
      if (m_terrain_resources.reflection_structure ||
          m_terrain_resources.reflection_vertices)
        [m_residency commit];
      m_terrain_resources.reflection_structure = nil;
      m_terrain_resources.reflection_vertices = nil;
      m_terrain_resources.reflection_proxy = {};
      m_terrain_resources.reflection_structure_bytes = 0;
      m_terrain_resources.reflection_scratch_bytes = 0;
      m_terrain_resources.reflection_proxy_ms = 0.0;
      m_terrain_resources.reflection_build_ms = 0.0;
      m_reflection_geometry_written = false;
      m_water_reflection_written = false;
    }

    void MetalRenderer::build_reflection_geometry () {
      if (!reflection_requested () ||
          m_terrain_resources.reflection_structure ||
          m_reflection_heights.empty () || !m_frame.drawable)
        return;

      const Mat4& view = m_frame.params.view;
      Vec3 forward (-view.element (2), 0.0f, -view.element (10));
      const float forward_length = std::sqrt (dot (forward, forward));
      if (forward_length > 1e-4f)
        forward = forward * (1.0f / forward_length);
      else
        forward = Vec3 (0.0f, 0.0f, -1.0f);
      const Vec3 focus = m_frame.params.camera_pos + forward * 1024.0f;
      const double proxy_start = cpu_time ();
      ReflectionTerrainProxy proxy = build_reflection_terrain_proxy (
        m_terrain_resources.params,
        m_reflection_heights,
        focus,
        m_frame.params.proj * m_frame.params.view,
        static_cast<int> (m_frame.drawable.texture.width),
        static_cast<int> (m_frame.drawable.texture.height),
        8,
        2048.0f);
      const double proxy_ms = (cpu_time () - proxy_start) * 1000.0;

      static_assert (sizeof (ReflectionProxyVertex) == 12);
      const std::size_t vertex_bytes =
        proxy.triangles.size () * sizeof (ReflectionProxyVertex);
      id<MTLBuffer> vertices = create_private_buffer (
        proxy.triangles.data (), vertex_bytes, @"Moppe reflection terrain");

      MTLAccelerationStructureTriangleGeometryDescriptor* geometry =
        [MTLAccelerationStructureTriangleGeometryDescriptor descriptor];
      geometry.label = @"Moppe reflection terrain triangles";
      geometry.vertexBuffer = vertices;
      geometry.vertexBufferOffset = 0;
      geometry.vertexFormat = MTLAttributeFormatFloat3;
      geometry.vertexStride = sizeof (ReflectionProxyVertex);
      geometry.triangleCount = proxy.metrics.triangle_count;
      geometry.opaque = YES;

      MTLPrimitiveAccelerationStructureDescriptor* descriptor =
        [MTLPrimitiveAccelerationStructureDescriptor descriptor];
      descriptor.geometryDescriptors = @[ geometry ];
      descriptor.usage = MTLAccelerationStructureUsagePreferFastIntersection;
      const MTLAccelerationStructureSizes sizes =
        [m_device accelerationStructureSizesWithDescriptor:descriptor];
      id<MTLAccelerationStructure> structure = [m_device
        newAccelerationStructureWithSize:sizes.accelerationStructureSize];
      structure.label = @"Moppe reflection terrain BLAS";
      id<MTLBuffer> scratch =
        [m_device newBufferWithLength:sizes.buildScratchBufferSize
                              options:MTLResourceStorageModePrivate];
      scratch.label = @"Moppe reflection terrain build scratch";
      if (!vertices || !structure || !scratch)
        throw std::runtime_error (
          "Could not allocate reflection geometry resources");
      make_resident (structure);
      make_resident (scratch);

      const double build_start = cpu_time ();
      id<MTLCommandQueue> build_queue = [m_device newCommandQueue];
      build_queue.label = @"Moppe reflection geometry builder";
      id<MTLCommandBuffer> command = [build_queue commandBuffer];
      id<MTLAccelerationStructureCommandEncoder> encoder =
        [command accelerationStructureCommandEncoder];
      [encoder buildAccelerationStructure:structure
                               descriptor:descriptor
                            scratchBuffer:scratch
                      scratchBufferOffset:0];
      [encoder endEncoding];
      [command commit];
      [command waitUntilCompleted];
      if (command.error)
        throw std::runtime_error (
          std::string ("Reflection geometry build failed: ") +
          command.error.localizedDescription.UTF8String);
      const double build_ms = (cpu_time () - build_start) * 1000.0;
      [m_residency removeAllocation:scratch];
      [m_residency commit];

      m_terrain_resources.reflection_vertices = vertices;
      m_terrain_resources.reflection_structure = structure;
      m_terrain_resources.reflection_proxy = std::move (proxy);
      m_terrain_resources.reflection_structure_bytes =
        sizes.accelerationStructureSize;
      m_terrain_resources.reflection_scratch_bytes =
        sizes.buildScratchBufferSize;
      m_terrain_resources.reflection_proxy_ms = proxy_ms;
      m_terrain_resources.reflection_build_ms = build_ms;
      m_reflection_heights.clear ();
      m_reflection_heights.shrink_to_fit ();

      const ReflectionProxyMetrics& metrics =
        m_terrain_resources.reflection_proxy.metrics;
      std::cerr << "moppe: reflection geometry: triangles="
                << metrics.triangle_count << ", vertices=" << vertex_bytes
                << " B, blas=" << sizes.accelerationStructureSize
                << " B, scratch=" << sizes.buildScratchBufferSize
                << " B, proxy=" << proxy_ms << " ms, build=" << build_ms
                << " ms" << std::endl;
    }

    void MetalRenderer::draw_water_reflection_signal (
      id<MTLBuffer> __strong& diagnostic,
      std::size_t& row_bytes,
      int& diagnostic_width,
      int& diagnostic_height) {
      MetalReflectionTargets& targets = m_reflection_targets[m_frame.slot];
      if (m_water_reflection_path.empty () || !m_have_reflection_ocean ||
          !m_pipelines.reflection_water_input ||
          !m_pipelines.water_reflection_signal ||
          !m_terrain_resources.reflection_structure || !targets.origin)
        return;

      record_gpu_pass_start (m_frame, GpuPass::Reflection);
      MTL4RenderPassDescriptor* pass = [[MTL4RenderPassDescriptor alloc] init];
      pass.colorAttachments[0].texture = targets.origin;
      pass.colorAttachments[0].loadAction = MTLLoadActionClear;
      pass.colorAttachments[0].storeAction = MTLStoreActionStore;
      pass.colorAttachments[0].clearColor = MTLClearColorMake (0, 0, 0, 0);
      pass.colorAttachments[1].texture = targets.optical_normal;
      pass.colorAttachments[1].loadAction = MTLLoadActionClear;
      pass.colorAttachments[1].storeAction = MTLStoreActionStore;
      pass.colorAttachments[1].clearColor = MTLClearColorMake (0, 0, 0, 0);
      pass.depthAttachment.texture = targets.depth;
      pass.depthAttachment.loadAction = MTLLoadActionClear;
      pass.depthAttachment.storeAction = MTLStoreActionDontCare;
      pass.depthAttachment.clearDepth = 0.0;
      id<MTL4RenderCommandEncoder> input =
        [m_frame.command_buffer renderCommandEncoderWithDescriptor:pass];
      input.label = @"Standing-water reflection inputs";
      [input setFrontFacingWinding:MTLWindingCounterClockwise];
      MetalWaterPass::draw_ocean (
        { input, m_pipelines, m_terrain_resources, m_water_resources, m_frame },
        m_reflection_ocean,
        m_pipelines.reflection_water_input,
        m_pipelines.reflection_water_tiles);
      [input endEncoding];

      const int width = targets.width;
      const int height = targets.height;
      MoppeWaterReflectionUniforms uniforms {};
      uniforms.camera = f4 (m_frame.params.camera_pos);
      uniforms.camera.w = 8192.0f;
      uniforms.sun_dir = m_frame.uniforms.sun_dir;
      uniforms.sun_colour = m_frame.uniforms.sun_diffuse;
      uniforms.ambient = m_frame.uniforms.ambient;
      uniforms.fog_colour = m_frame.uniforms.fog_color;
      uniforms.dimensions = { static_cast<float> (width),
                              static_cast<float> (height),
                              static_cast<float> (width * 3),
                              static_cast<float> (height * 2) };
      const MTLGPUAddress uniform_address =
        m_frame.arena[m_frame.slot].write (uniforms);
      [m_reflection_arguments
          setResource:m_terrain_resources.reflection_structure.gpuResourceID
        atBufferIndex:MOPPE_BUF_REFLECTION_AS];
      [m_reflection_arguments setAddress:uniform_address
                                 atIndex:MOPPE_BUF_REFLECTION_UNIFORMS];
      [m_reflection_arguments
        setAddress:m_terrain_resources.reflection_vertices.gpuAddress
           atIndex:MOPPE_BUF_REFLECTION_VERTICES];
      [m_reflection_arguments setTexture:targets.origin.gpuResourceID
                                 atIndex:MOPPE_TEX_REFLECTION_ORIGIN];
      [m_reflection_arguments setTexture:targets.optical_normal.gpuResourceID
                                 atIndex:MOPPE_TEX_REFLECTION_OPTICAL_NORMAL];
      [m_reflection_arguments setTexture:targets.radiance.gpuResourceID
                                 atIndex:MOPPE_TEX_REFLECTION_RADIANCE];
      [m_reflection_arguments setTexture:targets.hit_normal.gpuResourceID
                                 atIndex:MOPPE_TEX_REFLECTION_HIT_NORMAL];
      [m_reflection_arguments setTexture:targets.hit_distance.gpuResourceID
                                 atIndex:MOPPE_TEX_REFLECTION_HIT_DISTANCE];
      [m_reflection_arguments setTexture:targets.validity.gpuResourceID
                                 atIndex:MOPPE_TEX_REFLECTION_VALIDITY];

      id<MTL4ComputeCommandEncoder> query =
        [m_frame.command_buffer computeCommandEncoder];
      query.label = @"Raw standing-water reflection query";
      [query barrierAfterQueueStages:MTLStageFragment
                        beforeStages:MTLStageDispatch
                   visibilityOptions:MTL4VisibilityOptionDevice];
      [query setComputePipelineState:m_pipelines.water_reflection_signal];
      [query setArgumentTable:m_reflection_arguments];
      [query dispatchThreads:MTLSizeMake (width, height, 1)
        threadsPerThreadgroup:MTLSizeMake (8, 8, 1)];
      [query endEncoding];
      record_gpu_pass_end (m_frame);

      if (m_frame.screenshot_path.empty () || m_water_reflection_written)
        return;
      diagnostic_width = width * 3;
      diagnostic_height = height * 2;
      row_bytes = (static_cast<std::size_t> (diagnostic_width) * 8 + 255) &
                  ~static_cast<std::size_t> (255);
      diagnostic = [m_device newBufferWithLength:row_bytes * diagnostic_height
                                         options:MTLResourceStorageModeShared];
      diagnostic.label = @"Moppe water reflection signal diagnostic";
      make_resident (diagnostic);

      uniforms.output.x = static_cast<float> (row_bytes / 8);
      const MTLGPUAddress diagnostic_uniforms =
        m_frame.arena[m_frame.slot].write (uniforms);
      [m_reflection_arguments setAddress:diagnostic_uniforms
                                 atIndex:MOPPE_BUF_REFLECTION_UNIFORMS];
      [m_reflection_arguments setAddress:diagnostic.gpuAddress
                                 atIndex:MOPPE_BUF_REFLECTION_OUTPUT];
      id<MTL4ComputeCommandEncoder> display =
        [m_frame.command_buffer computeCommandEncoder];
      display.label = @"Water reflection signal diagnostic";
      [display barrierAfterQueueStages:MTLStageDispatch
                          beforeStages:MTLStageDispatch
                     visibilityOptions:MTL4VisibilityOptionDevice];
      [display setComputePipelineState:m_pipelines.water_reflection_diagnostic];
      [display setArgumentTable:m_reflection_arguments];
      [display dispatchThreads:MTLSizeMake (
                                 diagnostic_width, diagnostic_height, 1)
         threadsPerThreadgroup:MTLSizeMake (8, 8, 1)];
      [display endEncoding];
    }

    void MetalRenderer::write_water_reflection_report (const void* diagnostic,
                                                       std::size_t row_bytes,
                                                       int width,
                                                       int height) const {
      const int signal_width = width / 3;
      const int signal_height = height / 2;
      std::size_t inputs = 0;
      std::size_t visible = 0;
      std::size_t hits = 0;
      const auto* bytes = static_cast<const std::byte*> (diagnostic);
      for (int y = 0; y < signal_height; ++y) {
        const auto* row = reinterpret_cast<const std::uint16_t*> (
          bytes + static_cast<std::size_t> (y + signal_height) * row_bytes);
        for (int x = 0; x < signal_width; ++x) {
          const std::size_t pixel =
            static_cast<std::size_t> (x + 2 * signal_width) * 4;
          inputs += metal_half_to_float (row[pixel]) > 0.5f;
          visible += metal_half_to_float (row[pixel + 1]) > 0.5f;
          hits += metal_half_to_float (row[pixel + 2]) > 0.5f;
        }
      }

      const std::size_t pixels =
        static_cast<std::size_t> (signal_width) * signal_height;
      const std::size_t persistent_bytes_per_pixel =
        16 + 8 + 8 + 8 + 4 + 4 + (m_memoryless_ok ? 0 : 4);
      std::ofstream output (m_water_reflection_path + ".txt");
      output << std::fixed << std::setprecision (3);
      output << "water_reflection_goal=1\n";
      output << "device=" << m_device.name.UTF8String << '\n';
      output << "surface=actual_clipped_displaced_standing_water\n";
      output << "running_water=excluded\n";
      output << "query=metal4_compute_argument_table\n";
      output << "visibility=camera_to_water_terrain_ray\n";
      output << "ordinary_water_rendering=unchanged\n";
      output << "signal_dimensions=" << signal_width << 'x' << signal_height
             << '\n';
      output << "linear_resolution_scale=0.250\n";
      output << "panels=origin_depth,optical_normal,raw_radiance,hit_normal,"
                "hit_distance,validity\n";
      output << "signal_pixels=" << pixels << '\n';
      output << "water_input_pixels=" << inputs << '\n';
      output << "visible_water_pixels=" << visible << '\n';
      output << "terrain_hit_pixels=" << hits << '\n';
      output << "water_input_coverage="
             << (pixels ? static_cast<double> (inputs) / pixels : 0.0) << '\n';
      output << "visible_fraction="
             << (inputs ? static_cast<double> (visible) / inputs : 0.0) << '\n';
      output << "terrain_hit_fraction="
             << (visible ? static_cast<double> (hits) / visible : 0.0) << '\n';
      output << "signal_persistent_bytes_per_slot="
             << pixels * persistent_bytes_per_pixel << '\n';
      output << "signal_persistent_bytes_inflight="
             << pixels * persistent_bytes_per_pixel * FRAMES_IN_FLIGHT << '\n';
      output << "signal_transient_depth_bytes="
             << (m_memoryless_ok ? pixels * 4 : 0) << '\n';
      output << "reflection_counter_span_ms=" << m_water_reflection_gpu_ms
             << '\n';
      output << "isolated_ray_query_gpu_ms=" << m_water_reflection_query_gpu_ms
             << '\n';
      output << "acceleration_structure_bytes="
             << m_terrain_resources.reflection_structure_bytes << '\n';
      output << "terrain_proxy_triangles="
             << m_terrain_resources.reflection_proxy.metrics.triangle_count
             << '\n';
    }

    void MetalRenderer::benchmark_water_reflection_query () {
      const MetalReflectionTargets& targets =
        m_reflection_targets[m_frame.slot];
      if (m_water_reflection_path.empty () ||
          !m_pipelines.water_reflection_signal ||
          !m_terrain_resources.reflection_structure || !targets.origin)
        return;

      constexpr int iterations = 32;
      id<MTL4CommandAllocator> allocator = [m_device newCommandAllocator];
      id<MTL4CommandBuffer> command = [m_device newCommandBuffer];
      [command beginCommandBufferWithAllocator:allocator];
      for (int iteration = 0; iteration < iterations; ++iteration) {
        id<MTL4ComputeCommandEncoder> query = [command computeCommandEncoder];
        query.label = @"Isolated water reflection query benchmark";
        if (iteration > 0)
          [query barrierAfterQueueStages:MTLStageDispatch
                            beforeStages:MTLStageDispatch
                       visibilityOptions:MTL4VisibilityOptionDevice];
        [query setComputePipelineState:m_pipelines.water_reflection_signal];
        [query setArgumentTable:m_reflection_arguments];
        [query dispatchThreads:MTLSizeMake (targets.width, targets.height, 1)
          threadsPerThreadgroup:MTLSizeMake (8, 8, 1)];
        [query endEncoding];
      }
      [command endCommandBuffer];

      __block double elapsed_ms = -1.0;
      dispatch_semaphore_t feedback_done = dispatch_semaphore_create (0);
      MTL4CommitOptions* options = [[MTL4CommitOptions alloc] init];
      [options addFeedbackHandler:^(id<MTL4CommitFeedback> feedback) {
        if (!feedback.error && feedback.GPUEndTime >= feedback.GPUStartTime)
          elapsed_ms = 1000.0 * (feedback.GPUEndTime - feedback.GPUStartTime);
        dispatch_semaphore_signal (feedback_done);
      }];
      id<MTLSharedEvent> completed = [m_device newSharedEvent];
      const id<MTL4CommandBuffer> commands[] = { command };
      [m_queue commit:commands count:1 options:options];
      [m_queue signalEvent:completed value:1];
      if (![completed waitUntilSignaledValue:1 timeoutMS:5000])
        throw std::runtime_error (
          "Timed out benchmarking the water reflection query");
      if (dispatch_semaphore_wait (
            feedback_done,
            dispatch_time (DISPATCH_TIME_NOW, 5 * NSEC_PER_SEC)) != 0)
        throw std::runtime_error (
          "Timed out receiving water reflection query timing");
      if (elapsed_ms >= 0.0)
        m_water_reflection_query_gpu_ms = elapsed_ms / iterations;
    }

    void MetalRenderer::write_reflection_geometry_report () const {
      const ReflectionTerrainProxy& proxy =
        m_terrain_resources.reflection_proxy;
      const ReflectionProxyMetrics& metrics = proxy.metrics;
      std::ofstream output (m_reflection_geometry_path + ".txt");
      output << std::fixed << std::setprecision (3);
      output << "reflection_geometry_goal=0\n";
      output << "device=" << m_device.name.UTF8String << '\n';
      output << "source=authoritative_completed_surface\n";
      output << "geometry=bounded_periodic_terrain_only\n";
      output << "builder=metal_acceleration_structure_encoder\n";
      output << "query=metal4_compute_argument_table\n";
      output << "usage=prefer_fast_intersection\n";
      output << "ordinary_water_rendering=unchanged\n";
      output << "panels=normal,distance,primitive_barycentric,hit_mask\n";
      output << "source_stride=" << proxy.source_stride << '\n';
      output << "cells=" << proxy.cells_x << 'x' << proxy.cells_z << '\n';
      output << "bounds_m=" << proxy.minimum_x << ',' << proxy.minimum_z << ','
             << proxy.maximum_x << ',' << proxy.maximum_z << '\n';
      output << "triangles=" << metrics.triangle_count << '\n';
      output << "vertex_bytes="
             << proxy.triangles.size () * sizeof (ReflectionProxyVertex)
             << '\n';
      const std::size_t source_height_bytes =
        static_cast<std::size_t> (m_terrain_resources.params.width) *
        m_terrain_resources.params.height * sizeof (terrain::SurfaceElevation);
      const std::size_t vertex_bytes =
        proxy.triangles.size () * sizeof (ReflectionProxyVertex);
      output << "temporary_source_height_bytes=" << source_height_bytes << '\n';
      output << "acceleration_structure_bytes="
             << m_terrain_resources.reflection_structure_bytes << '\n';
      output << "build_scratch_bytes="
             << m_terrain_resources.reflection_scratch_bytes << '\n';
      output << "retained_gpu_bytes="
             << vertex_bytes + m_terrain_resources.reflection_structure_bytes
             << '\n';
      output << "peak_build_gpu_bytes="
             << vertex_bytes + m_terrain_resources.reflection_structure_bytes +
                  m_terrain_resources.reflection_scratch_bytes
             << '\n';
      output << "proxy_generation_ms="
             << m_terrain_resources.reflection_proxy_ms << '\n';
      output << "acceleration_structure_build_ms="
             << m_terrain_resources.reflection_build_ms << '\n';
      output << "height_samples=" << metrics.source_sample_count << '\n';
      output << "height_rms_m=" << metrics.height_rms_m << '\n';
      output << "height_p95_m=" << metrics.height_p95_m << '\n';
      output << "height_max_m=" << metrics.height_max_m << '\n';
      output << "projected_samples=" << metrics.projected_sample_count << '\n';
      output << "projected_p95_px=" << metrics.projected_p95_px << '\n';
      output << "projected_max_px=" << metrics.projected_max_px << '\n';
    }
#endif

    void MetalRenderer::blit_into (id<MTLTexture> tex,
                                   id<MTLBuffer> staging,
                                   int w,
                                   int h,
                                   int bpp,
                                   bool gen_mips) {
      const size_t bytes = (size_t)w * h * bpp;
      id<MTL4CommandAllocator> allocator = [m_device newCommandAllocator];
      id<MTL4CommandBuffer> cmd = [m_device newCommandBuffer];
      [cmd beginCommandBufferWithAllocator:allocator];
      id<MTL4ComputeCommandEncoder> blit = [cmd computeCommandEncoder];
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
      submit_and_wait (cmd);
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
      t->residency = m_residency;
      make_resident (t->texture);

      MTLSamplerDescriptor* sd = [[MTLSamplerDescriptor alloc] init];
      // This sampler is also bound into a Metal 4 argument table by resource
      // ID. Without the descriptor's argument-buffer opt-in, live rendering
      // worked but Xcode Metal capture replay segfaulted.
      sd.supportArgumentBuffers = YES;
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
      m->residency = m_residency;
      m->runs.assign (recorded.runs ().begin (), recorded.runs ().end ());
      if (!recorded.vertices ().empty ())
        m->vertices =
          create_private_buffer (recorded.vertices ().data (),
                                 recorded.vertices ().size () * sizeof (Vertex),
                                 @"Moppe retained mesh");
      return MeshPtr (m);
    }

    // -- world setup ---------------------------------------------------

    void MetalRenderer::set_terrain (
      const TerrainParams& params,
      std::span<const terrain::SurfaceElevation> heights,
      std::span<const terrain::TerrainNormal> normals) {
      MOPPE_PROFILE_ZONE ("MetalRenderer::set_terrain");
      const int w = params.width, h = params.height;
      const std::size_t sample_count = static_cast<std::size_t> (w) * h;
      if (w < 2 || h < 2 || heights.size () != sample_count ||
          normals.size () != sample_count || params.scale[1] != 1.0f)
        throw std::invalid_argument ("invalid Metal terrain raster");
#if !TARGET_OS_IPHONE
      if (reflection_requested ()) {
        retire_reflection_geometry ();
        m_reflection_heights.assign (heights.begin (), heights.end ());
      }
#endif
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
        make_resident (m_terrain_resources.heights);
      }
      upload_texture (m_terrain_resources.heights,
                      heights.data (),
                      w,
                      h,
                      sizeof (float),
                      false);
      // Normals: RG16Snorm xz, y reconstructed in the shader.
      std::vector<int16_t> packed ((size_t)w * h * 2);
      for (size_t i = 0; i < (size_t)w * h; ++i) {
        const Vec3 n = normals[i].numerical_value_in (mp_units::one);
        const float x = std::clamp (n[0], -1.0f, 1.0f);
        const float z = std::clamp (n[2], -1.0f, 1.0f);
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
        make_resident (m_terrain_resources.normals);
      }
      upload_texture (
        m_terrain_resources.normals, packed.data (), w, h, 4, false);

      // Shared chunk-local index templates.  The finest inserts one
      // virtual vertex between source samples; progressively coarser
      // levels use source strides 1, 2, 4, and 8.
      for (int lod = 0; lod < TERRAIN_LOD_COUNT; ++lod) {
        if (m_terrain_resources.indices[lod])
          continue;
        const int vpr = TERRAIN_LOD_VERTS[lod];
        std::vector<uint32_t> indices;
        const int rows = vpr - 1;
        indices.reserve ((size_t)rows * (vpr * 2 + 1));
        for (int row = 0; row < rows; ++row) {
          for (int x = 0; x < vpr; ++x) {
            indices.push_back ((uint32_t)(row * vpr + x));
            indices.push_back ((uint32_t)((row + 1) * vpr + x));
          }
          indices.push_back (0xFFFFFFFFu); // strip restart
        }
        m_terrain_resources.index_count[lod] =
          static_cast<uint32_t> (indices.size ());
        m_terrain_resources.indices[lod] = create_private_buffer (
          indices.data (),
          indices.size () * sizeof (uint32_t),
          [NSString stringWithFormat:@"Moppe terrain LOD %d indices", lod]);
      }

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
        make_resident (m_terrain_resources.overlay);
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

    void MetalRenderer::render_terrain_shadow (const Mat4& light_view_proj,
                                               bool include_forest) {
      MOPPE_PROFILE_ZONE ("MetalRenderer::render_terrain_shadow");
      if (!m_pipelines.terrain_shadow || !m_terrain_resources.have_terrain)
        return;

      constexpr int shadow_size = 4096;

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
        make_resident (m_terrain_resources.shadow_map);
      }

      // Bias: xy from NDC [-1,1] to uv [0,1], with the Metal
      // texture-space Y flip; z is already [0,1].
      Mat4 bias;
      bias.set_element (0, 0.5f);
      bias.set_element (5, -0.5f);
      bias.set_element (12, 0.5f);
      bias.set_element (13, 0.5f);
      m_terrain_resources.light_biased = bias * light_view_proj;

      MTL4RenderPassDescriptor* rp = [[MTL4RenderPassDescriptor alloc] init];
      rp.depthAttachment.texture = m_terrain_resources.shadow_map;
      rp.depthAttachment.loadAction = MTLLoadActionClear;
      rp.depthAttachment.storeAction = MTLStoreActionStore;
      rp.depthAttachment.clearDepth = 1.0;

      id<MTL4CommandAllocator> allocator = [m_device newCommandAllocator];
      id<MTL4CommandBuffer> cmd = [m_device newCommandBuffer];
      [cmd beginCommandBufferWithAllocator:allocator];
      // This setup pass runs on the world-generation thread. Tracy 0.13.1's
      // Metal server crashes when a context first receives a GPU zone from
      // that worker and later receives frame zones from the render thread.
      // Keep its existing command-buffer timing until upstream supports this
      // cross-thread pattern reliably.
      id<MTL4RenderCommandEncoder> enc =
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

      constexpr int requested_step = 1;
      int shadow_lod = TERRAIN_NATIVE_LOD;
      while (shadow_lod + 1 < TERRAIN_LOD_COUNT &&
             TERRAIN_LOD_STEP[shadow_lod] < requested_step)
        ++shadow_lod;
      const float shadow_step = TERRAIN_LOD_STEP[shadow_lod];
      const int chunks = m_terrain_resources.params.width / CHUNK_CELLS;
      const NSUInteger draw_count = 9 * chunks * chunks;
      MetalFrameEncoding scratch;
      scratch.arena[0].buffer = [m_device
        newBufferWithLength:sizeof (u) +
                            draw_count * (sizeof (MoppeChunkUniforms) + 256)
                    options:MTLResourceStorageModeShared];
      make_resident (scratch.arena[0].buffer);
      MTL4ArgumentTableDescriptor* argument_desc =
        [[MTL4ArgumentTableDescriptor alloc] init];
      argument_desc.maxBufferBindCount = 6;
      argument_desc.maxTextureBindCount = 16;
      argument_desc.initializeBindings = YES;
      const auto make_shadow_arguments = [&] (NSString* label) {
        argument_desc.label = label;
        NSError* argument_error = nil;
        id<MTL4ArgumentTable> table =
          [m_device newArgumentTableWithDescriptor:argument_desc
                                             error:&argument_error];
        if (!table)
          throw std::runtime_error (
            std::string ("Could not create shadow argument table: ") +
            (argument_error ? argument_error.localizedDescription.UTF8String
                            : "unknown"));
        return table;
      };
      scratch.arguments.vertex =
        make_shadow_arguments (@"Moppe terrain shadow bindings");
      scratch.arguments.object =
        make_shadow_arguments (@"Moppe forest shadow object bindings");
      scratch.arguments.mesh =
        make_shadow_arguments (@"Moppe forest shadow mesh bindings");
      bind_address (scratch,
                    MTLRenderStageVertex,
                    MOPPE_BUF_FRAME,
                    scratch.arena[0].write (u));
      bind_texture (scratch,
                    MTLRenderStageVertex,
                    MOPPE_TEX_HEIGHTS,
                    m_terrain_resources.heights);
      use_arguments (enc, scratch, MTLRenderStageVertex);
      const float period_x =
        m_terrain_resources.params.width * m_terrain_resources.params.scale[0];
      const float period_z =
        m_terrain_resources.params.height * m_terrain_resources.params.scale[2];
      // The visible ground is periodic. Neighbouring copies must participate
      // in this canonical tile's shadow map too, or occluders disappear at a
      // wrap edge and leave a ruler-straight lighting discontinuity.
      for (int tile_z = -1; tile_z <= 1; ++tile_z)
        for (int tile_x = -1; tile_x <= 1; ++tile_x)
          for (int cz = 0; cz < chunks; ++cz)
            for (int cx = 0; cx < chunks; ++cx) {
              MoppeChunkUniforms c;
              std::memset (&c, 0, sizeof (c));
              c.origin_x = cx * CHUNK_CELLS;
              c.origin_z = cz * CHUNK_CELLS;
              c.step = shadow_step;
              c.verts_per_row = TERRAIN_LOD_VERTS[shadow_lod];
              c.parent_step = shadow_step;
              c.world_offset.x = tile_x * period_x;
              c.world_offset.z = tile_z * period_z;
              bind_address (scratch,
                            MTLRenderStageVertex,
                            MOPPE_BUF_CHUNK,
                            scratch.arena[0].write (c));
              [enc drawIndexedPrimitives:MTLPrimitiveTypeTriangleStrip
                              indexCount:m_terrain_resources
                                           .index_count[shadow_lod]
                               indexType:MTLIndexTypeUInt32
                             indexBuffer:m_terrain_resources.indices[shadow_lod]
                                           .gpuAddress
                       indexBufferLength:m_terrain_resources.indices[shadow_lod]
                                           .length
                           instanceCount:1];
            }

      if (include_forest && m_pipelines.forest_shadow &&
          m_forest_resources.instances && m_forest_resources.count > 0) {
        MoppeForestUniforms forest;
        std::memset (&forest, 0, sizeof (forest));
        forest.view_proj = m4 (light_view_proj);
        forest.world.x = m_forest_resources.period_x;
        forest.world.y = m_forest_resources.period_z;
        forest.world.z = static_cast<float> (m_forest_resources.count);
        const MTLGPUAddress forest_uniforms = scratch.arena[0].write (forest);
        for (MTLRenderStages stage :
             { MTLRenderStageObject, MTLRenderStageMesh }) {
          bind_address (scratch, stage, MOPPE_BUF_FRAME, forest_uniforms);
          bind_address (scratch,
                        stage,
                        MOPPE_BUF_FOREST,
                        m_forest_resources.instances.gpuAddress);
        }
        use_arguments (enc, scratch, MTLRenderStageObject | MTLRenderStageMesh);
        [enc setRenderPipelineState:m_pipelines.forest_shadow];
        [enc setDepthStencilState:m_pipelines.shadow_depth];
        [enc setCullMode:MTLCullModeNone];
        const NSUInteger candidates =
          static_cast<NSUInteger> (m_forest_resources.count) * 9;
        [enc drawMeshThreadgroups:MTLSizeMake ((candidates +
                                                MOPPE_FOREST_OBJECT_THREADS -
                                                1) /
                                                 MOPPE_FOREST_OBJECT_THREADS,
                                               1,
                                               1)
          threadsPerObjectThreadgroup:MTLSizeMake (
                                        MOPPE_FOREST_OBJECT_THREADS, 1, 1)
            threadsPerMeshThreadgroup:MTLSizeMake (
                                        MOPPE_FOREST_MESH_THREADS, 1, 1)];
      }
      [enc endEncoding];
      submit_and_wait (cmd);
      [m_residency removeAllocation:scratch.arena[0].buffer];
      [m_residency commit];
      if (::getenv ("MOPPE_PROFILE_SHADOW")) {
        std::cerr << "terrain shadow: " << shadow_size << "px, step "
                  << shadow_step << "\n";
      }
      m_terrain_resources.have_shadow = true;
    }

    void MetalRenderer::render_local_shadow (const LocalShadowParams& params) {
      MOPPE_PROFILE_ZONE ("MetalRenderer::render_local_shadow");
      if (!m_frame.command_buffer || m_frame.scene_encoder ||
          !m_pipelines.terrain_shadow || !m_terrain_resources.have_terrain)
        return;

      constexpr int shadow_size = 2048;
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
        make_resident (m_terrain_resources.shadow_map);
      }

      Mat4 bias;
      bias.set_element (0, 0.5f);
      bias.set_element (5, -0.5f);
      bias.set_element (12, 0.5f);
      bias.set_element (13, 0.5f);
      m_terrain_resources.light_biased = bias * params.light_view_proj;

      MTL4RenderPassDescriptor* rp = [[MTL4RenderPassDescriptor alloc] init];
      rp.depthAttachment.texture = m_terrain_resources.shadow_map;
      rp.depthAttachment.loadAction = MTLLoadActionClear;
      rp.depthAttachment.storeAction = MTLStoreActionStore;
      rp.depthAttachment.clearDepth = 1.0;
      id<MTL4RenderCommandEncoder> enc =
        [m_frame.command_buffer renderCommandEncoderWithDescriptor:rp];
      record_gpu_pass_start (m_frame, enc, GpuPass::Shadow);
      [enc setRenderPipelineState:m_pipelines.terrain_shadow];
      [enc setDepthStencilState:m_pipelines.shadow_depth];
      [enc setCullMode:MTLCullModeNone];
      [enc setFrontFacingWinding:MTLWindingCounterClockwise];
      [enc setDepthBias:2.0f slopeScale:2.0f clamp:0.0f];

      MoppeTerrainUniforms terrain_uniforms;
      std::memset (&terrain_uniforms, 0, sizeof (terrain_uniforms));
      terrain_uniforms.view_proj = m4 (params.light_view_proj);
      terrain_uniforms.params0.x = m_terrain_resources.params.scale[0];
      terrain_uniforms.params0.y = m_terrain_resources.params.scale[1];
      terrain_uniforms.params0.z = m_terrain_resources.params.scale[2];
      bind_address (m_frame,
                    MTLRenderStageVertex,
                    MOPPE_BUF_FRAME,
                    m_frame.arena[m_frame.slot].write (terrain_uniforms));
      bind_texture (m_frame,
                    MTLRenderStageVertex,
                    MOPPE_TEX_HEIGHTS,
                    m_terrain_resources.heights);
      use_arguments (enc, m_frame, MTLRenderStageVertex);

      const TerrainParams& terrain = m_terrain_resources.params;
      const int chunks = terrain.width / CHUNK_CELLS;
      const float chunk_width = CHUNK_CELLS * terrain.scale[0];
      const float chunk_depth = CHUNK_CELLS * terrain.scale[2];
      const float period_x = terrain.width * terrain.scale[0];
      const float period_z = terrain.height * terrain.scale[2];
      const Vec3 focus = position_value (params.focus);
      const float reach = params.radius.numerical_value_in (u::m);
      const int centre_x =
        static_cast<int> (std::floor (focus[0] / chunk_width));
      const int centre_z =
        static_cast<int> (std::floor (focus[2] / chunk_depth));
      const int reach_x =
        static_cast<int> (std::ceil (reach / chunk_width)) + 1;
      const int reach_z =
        static_cast<int> (std::ceil (reach / chunk_depth)) + 1;
      const auto floor_div = [] (int value, int divisor) {
        const int quotient = value / divisor;
        const int remainder = value % divisor;
        return quotient - (remainder < 0 ? 1 : 0);
      };
      const auto positive_mod = [] (int value, int divisor) {
        const int remainder = value % divisor;
        return remainder < 0 ? remainder + divisor : remainder;
      };
      const float chunk_radius = 0.5f * std::sqrt (chunk_width * chunk_width +
                                                   chunk_depth * chunk_depth);
      const float draw_reach = reach + chunk_radius;
      for (int world_z = centre_z - reach_z; world_z <= centre_z + reach_z;
           ++world_z)
        for (int world_x = centre_x - reach_x; world_x <= centre_x + reach_x;
             ++world_x) {
          const float dx = (world_x + 0.5f) * chunk_width - focus[0];
          const float dz = (world_z + 0.5f) * chunk_depth - focus[2];
          if (dx * dx + dz * dz > draw_reach * draw_reach)
            continue;

          MoppeChunkUniforms chunk;
          std::memset (&chunk, 0, sizeof (chunk));
          chunk.origin_x = positive_mod (world_x, chunks) * CHUNK_CELLS;
          chunk.origin_z = positive_mod (world_z, chunks) * CHUNK_CELLS;
          chunk.step = TERRAIN_LOD_STEP[TERRAIN_NATIVE_LOD];
          chunk.verts_per_row = TERRAIN_LOD_VERTS[TERRAIN_NATIVE_LOD];
          chunk.parent_step = chunk.step;
          chunk.world_offset.x = floor_div (world_x, chunks) * period_x;
          chunk.world_offset.z = floor_div (world_z, chunks) * period_z;
          bind_address (m_frame,
                        MTLRenderStageVertex,
                        MOPPE_BUF_CHUNK,
                        m_frame.arena[m_frame.slot].write (chunk));
          [enc drawIndexedPrimitives:MTLPrimitiveTypeTriangleStrip
                          indexCount:m_terrain_resources
                                       .index_count[TERRAIN_NATIVE_LOD]
                           indexType:MTLIndexTypeUInt32
                         indexBuffer:m_terrain_resources
                                       .indices[TERRAIN_NATIVE_LOD]
                                       .gpuAddress
                   indexBufferLength:m_terrain_resources
                                       .indices[TERRAIN_NATIVE_LOD]
                                       .length
                       instanceCount:1];
        }

      if (params.include_forest && m_pipelines.forest_shadow &&
          m_forest_resources.instances && m_forest_resources.count > 0) {
        MoppeForestUniforms forest;
        std::memset (&forest, 0, sizeof (forest));
        forest.view_proj = m4 (params.light_view_proj);
        forest.camera_pos = f4 (focus);
        forest.world.x = m_forest_resources.period_x;
        forest.world.y = m_forest_resources.period_z;
        forest.world.z = static_cast<float> (m_forest_resources.count);
        forest.world.w = 1.0f;
        const MTLGPUAddress uniforms =
          m_frame.arena[m_frame.slot].write (forest);
        for (MTLRenderStages stage :
             { MTLRenderStageObject, MTLRenderStageMesh }) {
          bind_address (m_frame, stage, MOPPE_BUF_FRAME, uniforms);
          bind_address (m_frame,
                        stage,
                        MOPPE_BUF_FOREST,
                        m_forest_resources.instances.gpuAddress);
        }
        use_arguments (enc, m_frame, MTLRenderStageObject | MTLRenderStageMesh);
        [enc setRenderPipelineState:m_pipelines.forest_shadow];
        [enc setDepthStencilState:m_pipelines.shadow_depth];
        [enc setCullMode:MTLCullModeNone];
        const NSUInteger candidates = m_forest_resources.count;
        [enc drawMeshThreadgroups:MTLSizeMake ((candidates +
                                                MOPPE_FOREST_OBJECT_THREADS -
                                                1) /
                                                 MOPPE_FOREST_OBJECT_THREADS,
                                               1,
                                               1)
          threadsPerObjectThreadgroup:MTLSizeMake (
                                        MOPPE_FOREST_OBJECT_THREADS, 1, 1)
            threadsPerMeshThreadgroup:MTLSizeMake (
                                        MOPPE_FOREST_MESH_THREADS, 1, 1)];
      }
      record_gpu_pass_end (m_frame, enc);
      [enc endEncoding];

      m_terrain_resources.have_shadow = true;
      m_frame.uniforms.light_matrix = m4 (m_terrain_resources.light_biased);
      m_frame.uniforms.shadow.x = terrain.shadow_strength;
      m_frame.uniforms.shadow.y = 1.0f / shadow_size;
      m_frame.frame_uniforms =
        m_frame.arena[m_frame.slot].write (m_frame.uniforms);
    }

    void MetalRenderer::set_ocean (const OceanSetup& setup,
                                   const render::TexturePixels& water_levels) {
      MOPPE_PROFILE_ZONE ("MetalRenderer::set_ocean");
      // Regular triangle water grid, indexed. One draw still covers sea
      // and lakes together; the indices are what keep a corner shared by
      // six triangles from being shaded six times. This vertex stage
      // reads the horizontal-water sheet and rides the swell, so its cost
      // is most of what the grid spends, and the plain-triangle form
      // spent it 540,000 times over 90,601 distinct corners.
      const int cells = setup.cells;
      const int side = cells + 1;
      const float step = 2 * setup.half_extent / cells;
      const float x0 = setup.center[0] - setup.half_extent;
      const float z0 = setup.center[2] - setup.half_extent;

      std::vector<float> verts;
      verts.reserve ((size_t)side * side * 3);
      for (int j = 0; j < side; ++j)
        for (int i = 0; i < side; ++i) {
          verts.push_back (x0 + i * step);
          verts.push_back (setup.level);
          verts.push_back (z0 + j * step);
        }

      // Same two triangles per cell, and the same winding, as the
      // unrolled form emitted.
      std::vector<uint32_t> indices;
      indices.reserve ((size_t)cells * cells * 6);
      for (int j = 0; j < cells; ++j)
        for (int i = 0; i < cells; ++i) {
          const uint32_t v = (uint32_t)(j * side + i);
          const uint32_t quad[6] = { v,     v + side, v + 1,
                                     v + 1, v + side, v + side + 1 };
          for (const uint32_t index : quad)
            indices.push_back (index);
        }

      m_water_resources.ocean_level = setup.level;
      m_water_resources.ocean_vcount = (uint32_t)(verts.size () / 3);
      m_water_resources.ocean_icount = (uint32_t)indices.size ();
      m_water_resources.ocean_verts = create_private_buffer (
        verts.data (), verts.size () * sizeof (float), @"Moppe ocean vertices");
      m_water_resources.ocean_indices =
        create_private_buffer (indices.data (),
                               indices.size () * sizeof (uint32_t),
                               @"Moppe ocean indices");

      // Physical elevation and wave amplitude write their final RG32F bytes
      // directly into staging storage.
      m_water_resources.have_water_levels =
        upload_pixels (m_water_resources.water_levels,
                       water_levels,
                       render::PixelFormat::rg32f,
                       MTLPixelFormatRG32Float);
    }

    void MetalRenderer::set_water_flow (const render::TexturePixels& flow) {
      MOPPE_PROFILE_ZONE ("MetalRenderer::set_water_flow");
      // Planar velocity narrows once into the RG16F staging allocation.
      m_water_resources.have_water_flow =
        upload_pixels (m_water_resources.water_flow,
                       flow,
                       render::PixelFormat::rg16f,
                       MTLPixelFormatRG16Float);
    }

    // Every typed texture reaches the GPU the same way: check the source
    // covers this lattice and carries the promised format, make sure the
    // matching texture exists, then let the source write straight into
    // staging memory.
    bool MetalRenderer::upload_pixels (__strong id<MTLTexture>& texture,
                                       const render::TexturePixels& pixels,
                                       render::PixelFormat expected,
                                       MTLPixelFormat format) {
      const int width = m_terrain_resources.params.width;
      const int height = m_terrain_resources.params.height;
      if (pixels.empty () || width <= 0 || height <= 0 ||
          pixels.format () != expected ||
          pixels.width () != static_cast<std::size_t> (width) ||
          pixels.height () != static_cast<std::size_t> (height))
        return false;
      if (!texture || texture.width != static_cast<NSUInteger> (width) ||
          texture.height != static_cast<NSUInteger> (height)) {
        MTLTextureDescriptor* td =
          [MTLTextureDescriptor texture2DDescriptorWithPixelFormat:format
                                                             width:width
                                                            height:height
                                                         mipmapped:NO];
        td.storageMode = MTLStorageModePrivate;
        td.usage = MTLTextureUsageShaderRead;
        texture = [m_device newTextureWithDescriptor:td];
        make_resident (texture);
      }
      id<MTLBuffer> staging =
        [m_device newBufferWithLength:pixels.byte_size ()
                              options:MTLResourceStorageModeShared];
      make_resident (staging);
      pixels.write_into (static_cast<std::byte*> (staging.contents));
      blit_into (texture,
                 staging,
                 width,
                 height,
                 static_cast<int> (render::bytes_per_pixel (pixels.format ())),
                 false);
      [m_residency removeAllocation:staging];
      [m_residency commit];
      return true;
    }

    void
    MetalRenderer::set_terrain_geology (const render::TexturePixels& geology) {
      MOPPE_PROFILE_ZONE ("MetalRenderer::set_terrain_geology");
      m_terrain_resources.have_geology =
        upload_pixels (m_terrain_resources.geology,
                       geology,
                       render::PixelFormat::rg16f,
                       MTLPixelFormatRG16Float);
    }

    void
    MetalRenderer::set_terrain_shore (const render::TexturePixels& distance) {
      m_terrain_resources.have_shore = upload_pixels (m_terrain_resources.shore,
                                                      distance,
                                                      render::PixelFormat::r16f,
                                                      MTLPixelFormatR16Float);
    }

    void
    MetalRenderer::set_terrain_paths (const render::TexturePixels& influence) {
      m_terrain_resources.have_paths =
        upload_pixels (m_terrain_resources.paths,
                       influence,
                       render::PixelFormat::rg16f,
                       MTLPixelFormatRG16Float);
    }

    void MetalRenderer::set_terrain_moisture (
      const render::TexturePixels& moisture) {
      MOPPE_PROFILE_ZONE ("MetalRenderer::set_terrain_moisture");
      m_terrain_resources.have_moisture =
        upload_pixels (m_terrain_resources.moisture,
                       moisture,
                       render::PixelFormat::r16f,
                       MTLPixelFormatR16Float);
    }

    void
    MetalRenderer::set_terrain_forest (const render::TexturePixels& cover) {
      MOPPE_PROFILE_ZONE ("MetalRenderer::set_terrain_forest");
      m_terrain_resources.have_forest =
        upload_pixels (m_terrain_resources.forest,
                       cover,
                       render::PixelFormat::r16f,
                       MTLPixelFormatR16Float);
    }

    void MetalRenderer::set_terrain_snow_support (
      const render::TexturePixels& support) {
      MOPPE_PROFILE_ZONE ("MetalRenderer::set_terrain_snow_support");
      m_terrain_resources.have_snow_support =
        upload_pixels (m_terrain_resources.snow_support,
                       support,
                       render::PixelFormat::r16f,
                       MTLPixelFormatR16Float);
    }

    // Direction times a [0,1] activity fits comfortably in half precision.
    void MetalRenderer::set_terrain_channel_flux (
      const render::TexturePixels& flux) {
      MOPPE_PROFILE_ZONE ("MetalRenderer::set_terrain_channel_flux");
      m_terrain_resources.have_channel_flux =
        upload_pixels (m_terrain_resources.channel_flux,
                       flux,
                       render::PixelFormat::rg16f,
                       MTLPixelFormatRG16Float);
    }

    void MetalRenderer::set_forest (const ForestSetup& setup,
                                    std::span<const ForestInstance> instances) {
      MOPPE_PROFILE_ZONE ("MetalRenderer::set_forest");
      std::vector<MoppeForestInstance> packed;
      packed.reserve (instances.size ());
      for (const ForestInstance& instance : instances) {
        const Vec3 root = position_value (instance.root);
        const Vec3 up =
          instance.ground_normal.numerical_value_in (mp_units::one);
        const float height = instance.height.numerical_value_in (u::m);
        const float radius = instance.crown_radius.numerical_value_in (u::m);
        if (!std::isfinite (height) || !std::isfinite (radius) ||
            height <= 0.0f || radius <= 0.0f)
          throw std::invalid_argument ("invalid typed forest individual");
        MoppeForestInstance gpu {};
        gpu.root_height = f4 (root, height);
        gpu.up_radius = f4 (up, radius);
        gpu.ecology.x =
          instance.canopy_cover.numerical_value_in (mp_units::one);
        gpu.ecology.y = instance.moisture.numerical_value_in (mp_units::one);
        gpu.identity.x = instance.seed;
        gpu.identity.y = static_cast<std::uint32_t> (instance.species);
        gpu.identity.z = static_cast<std::uint32_t> (instance.age);
        packed.push_back (gpu);
      }

      if (m_forest_resources.instances && m_frame.sequence &&
          ![m_frame.completion_event waitUntilSignaledValue:m_frame.sequence
                                                  timeoutMS:5000])
        throw std::runtime_error (
          "Timed out replacing an in-flight forest instance buffer");
      if (m_forest_resources.instances) {
        [m_residency removeAllocation:m_forest_resources.instances];
        [m_residency commit];
      }
      m_forest_resources.instances =
        create_private_buffer (packed.data (),
                               packed.size () * sizeof (MoppeForestInstance),
                               @"Moppe forest individuals");
      m_forest_resources.count = static_cast<std::uint32_t> (packed.size ());
      const Vec3 period = extent_value (setup.period);
      m_forest_resources.period_x = period[0];
      m_forest_resources.period_z = period[2];
    }

    // -- targets -------------------------------------------------------

    id<MTLTexture>
    MetalRenderer::make_target (MTLPixelFormat fmt,
                                int w,
                                int h,
                                int samples,
                                bool memoryless,
                                MTLTextureUsage additional_usage) {
      MTLTextureDescriptor* td =
        [MTLTextureDescriptor texture2DDescriptorWithPixelFormat:fmt
                                                           width:w
                                                          height:h
                                                       mipmapped:NO];
      td.textureType =
        samples > 1 ? MTLTextureType2DMultisample : MTLTextureType2D;
      td.sampleCount = samples;
      // A tile-resident attachment never leaves the pass that writes it, so
      // it carries no shader-read usage even at one sample per pixel.
      const bool tile_only = memoryless && m_memoryless_ok;
      td.usage = MTLTextureUsageRenderTarget | additional_usage |
                 (samples > 1 || tile_only ? 0 : MTLTextureUsageShaderRead);
      td.storageMode =
        tile_only ? MTLStorageModeMemoryless : MTLStorageModePrivate;
      id<MTLTexture> texture = [m_device newTextureWithDescriptor:td];
      // Memoryless attachments exist only in tile memory and cannot belong to
      // a residency set. Every resource that survives its pass is resident.
      if (!tile_only)
        make_resident (texture);
      return texture;
    }

    void MetalRenderer::retire_scene_targets () {
      if (m_frame.sequence &&
          ![m_frame.completion_event waitUntilSignaledValue:m_frame.sequence
                                                  timeoutMS:5000])
        throw std::runtime_error ("Timed out retiring Metal 4 frame targets");
      if (m_present_sequence &&
          ![m_present_completion_event waitUntilSignaledValue:m_present_sequence
                                                    timeoutMS:5000])
        throw std::runtime_error (
          "Timed out retiring Metal 4 presentation targets");
      std::vector<id<MTLTexture>> targets;
      const auto retire = [&] (id<MTLTexture> texture) {
        if (texture && texture.storageMode != MTLStorageModeMemoryless &&
            std::find (targets.begin (), targets.end (), texture) ==
              targets.end ()) {
          targets.push_back (texture);
          [m_residency removeAllocation:texture];
        }
      };
      retire (m_targets.msaa_color);
      retire (m_targets.msaa_depth);
      retire (m_targets.scene_a);
      retire (m_targets.post_a);
      retire (m_targets.post_b);
      retire (m_targets.shafts);
      retire (m_targets.ao_a);
      retire (m_targets.ao_b);
      retire (m_targets.motion);
      retire (m_targets.reactive);
      retire (m_targets.prev_frame);
      retire (m_targets.bloom_a);
      retire (m_targets.bloom_b);
      for (int i = 0; i < FRAMES_IN_FLIGHT; ++i) {
        retire (m_targets.interpolator_color[i]);
        retire (m_targets.interpolator_composite[i]);
        retire (m_targets.interpolator_output[i]);
      }
#if !TARGET_OS_IPHONE
      for (const MetalReflectionTargets& reflection : m_reflection_targets) {
        retire (reflection.origin);
        retire (reflection.optical_normal);
        retire (reflection.depth);
        retire (reflection.radiance);
        retire (reflection.hit_normal);
        retire (reflection.hit_distance);
        retire (reflection.validity);
      }
#endif
      if (m_targets.spatial_scaler) {
        m_targets.spatial_scaler.colorTexture = nil;
        m_targets.spatial_scaler.outputTexture = nil;
      }
      if (m_targets.temporal_scaler) {
        m_targets.temporal_scaler.colorTexture = nil;
        m_targets.temporal_scaler.depthTexture = nil;
        m_targets.temporal_scaler.motionTexture = nil;
        m_targets.temporal_scaler.reactiveMaskTexture = nil;
        m_targets.temporal_scaler.exposureTexture = nil;
        m_targets.temporal_scaler.outputTexture = nil;
      }
      if (m_targets.frame_interpolator) {
        m_targets.frame_interpolator.colorTexture = nil;
        m_targets.frame_interpolator.prevColorTexture = nil;
        m_targets.frame_interpolator.depthTexture = nil;
        m_targets.frame_interpolator.motionTexture = nil;
        m_targets.frame_interpolator.uiTexture = nil;
        m_targets.frame_interpolator.outputTexture = nil;
      }
      retire (m_targets.spatial_output);
      retire (m_targets.exposure_tex);
      m_targets.spatial_scaler = nil;
      m_targets.temporal_scaler = nil;
      m_targets.frame_interpolator = nil;
      m_targets.spatial_fence = nil;
      if (!targets.empty ())
        [m_residency commit];
#if !TARGET_OS_IPHONE
      m_reflection_targets.fill ({});
#endif
    }

    void MetalRenderer::ensure_targets (float requested_scale,
                                        float scale_override,
                                        float megapixel_budget,
                                        UpscalingMode upscaling) {
      id<CAMetalDrawable> pending = m_frame.pending_drawable;
      const int drawable_w = pending
                               ? static_cast<int> (pending.texture.width)
                               : static_cast<int> (m_layer.drawableSize.width);
      const int drawable_h = pending
                               ? static_cast<int> (pending.texture.height)
                               : static_cast<int> (m_layer.drawableSize.height);
      if (drawable_w == 0 || drawable_h == 0)
        return;
      const CGSize points = m_layer.bounds.size;
      const float backing_scale =
        points.width > 0 ? drawable_w / (float)points.width : 1.0f;
      const float scale = scene_render_scale (backing_scale,
                                              requested_scale,
                                              scale_override,
                                              megapixel_budget,
                                              (double)drawable_w * drawable_h);
      const int w = std::max (1, (int)std::round (drawable_w * scale));
      const int h = std::max (1, (int)std::round (drawable_h * scale));
      const bool native = w == drawable_w && h == drawable_h;
      ResolvedUpscaling resolved =
        native ? ResolvedUpscaling::Native : ResolvedUpscaling::Linear;
      if (!native && upscaling == UpscalingMode::Temporal) {
        if (m_temporal_upscaling_supported)
          resolved = ResolvedUpscaling::Temporal;
        else if (m_spatial_upscaling_supported)
          resolved = ResolvedUpscaling::Spatial;
      } else if (!native && upscaling == UpscalingMode::Spatial &&
                 m_spatial_upscaling_supported) {
        resolved = ResolvedUpscaling::Spatial;
      }
      const bool wants_frame_interpolation =
        m_frame_interpolation_enabled && m_frame_interpolation_supported &&
        resolved == ResolvedUpscaling::Temporal;
      if (w == m_targets.width && h == m_targets.height &&
          drawable_w == m_targets.output_width &&
          drawable_h == m_targets.output_height &&
          upscaling == m_targets.requested_upscaling &&
          resolved == m_targets.resolved_upscaling && m_targets.msaa_color &&
          static_cast<bool> (m_targets.frame_interpolator) ==
            wants_frame_interpolation)
        return;

      retire_scene_targets ();
      m_targets.width = w;
      m_targets.height = h;
      m_targets.output_width = drawable_w;
      m_targets.output_height = drawable_h;
      m_targets.requested_upscaling = upscaling;
      m_targets.resolved_upscaling = resolved;

      MTLTextureUsage color_input_usage = 0;
      MTLTextureUsage depth_input_usage = 0;
      MTLTextureUsage motion_input_usage = 0;
      MTLTextureUsage reactive_input_usage = 0;
      MTLTextureUsage interpolation_color_usage = 0;
      MTLTextureUsage interpolation_ui_usage = 0;
      MTLTextureUsage interpolation_output_usage = 0;
      const char* resolution_reason = native ? "scene-matches-drawable"
                                      : upscaling == UpscalingMode::Linear
                                        ? "requested"
                                        : "unsupported";
      MTLTextureUsage reconstruction_output_usage = 0;
      if (resolved == ResolvedUpscaling::Temporal) {
        resolution_reason = "supported";
        MTLFXTemporalScalerDescriptor* descriptor =
          [[MTLFXTemporalScalerDescriptor alloc] init];
        descriptor.colorTextureFormat = MTLPixelFormatRGBA16Float;
        descriptor.depthTextureFormat = MTLPixelFormatDepth32Float;
        descriptor.motionTextureFormat = MTLPixelFormatRG16Float;
        descriptor.outputTextureFormat = MTLPixelFormatRGBA16Float;
        descriptor.inputWidth = w;
        descriptor.inputHeight = h;
        descriptor.outputWidth = drawable_w;
        descriptor.outputHeight = drawable_h;
        descriptor.autoExposureEnabled = NO;
        descriptor.requiresSynchronousInitialization = YES;
        descriptor.reactiveMaskTextureEnabled = YES;
        descriptor.reactiveMaskTextureFormat = MTLPixelFormatR8Unorm;
        m_targets.temporal_scaler =
          [descriptor newTemporalScalerWithDevice:m_device compiler:m_compiler];
        if (m_targets.temporal_scaler) {
          m_targets.spatial_fence = [m_device newFence];
          m_targets.spatial_fence.label = @"Moppe MetalFX temporal fence";
          m_targets.temporal_scaler.fence = m_targets.spatial_fence;
          color_input_usage = m_targets.temporal_scaler.colorTextureUsage;
          // The sun-shaft march reads the stored scene depth after
          // reconstruction, on top of whatever the scaler requires.
          depth_input_usage = m_targets.temporal_scaler.depthTextureUsage |
                              MTLTextureUsageShaderRead;
          motion_input_usage = m_targets.temporal_scaler.motionTextureUsage;
          reactive_input_usage = m_targets.temporal_scaler.reactiveTextureUsage;
          reconstruction_output_usage =
            m_targets.temporal_scaler.outputTextureUsage;
        } else {
          m_temporal_upscaling_supported = false;
          m_targets.resolved_upscaling = m_spatial_upscaling_supported
                                           ? ResolvedUpscaling::Spatial
                                           : ResolvedUpscaling::Linear;
          resolved = m_targets.resolved_upscaling;
          resolution_reason = "temporal-creation-failed";
          std::cerr << "moppe: MetalFX temporal scaler creation failed; "
                       "using spatial/linear fallback"
                    << std::endl;
        }
      }
      if (resolved == ResolvedUpscaling::Spatial) {
        if (upscaling != UpscalingMode::Temporal)
          resolution_reason = "supported";
        MTLFXSpatialScalerDescriptor* descriptor =
          [[MTLFXSpatialScalerDescriptor alloc] init];
        descriptor.colorTextureFormat = MTLPixelFormatRGBA16Float;
        descriptor.outputTextureFormat = MTLPixelFormatRGBA16Float;
        descriptor.inputWidth = w;
        descriptor.inputHeight = h;
        descriptor.outputWidth = drawable_w;
        descriptor.outputHeight = drawable_h;
        descriptor.colorProcessingMode =
          MTLFXSpatialScalerColorProcessingModeHDR;
        m_targets.spatial_scaler =
          [descriptor newSpatialScalerWithDevice:m_device compiler:m_compiler];
        if (m_targets.spatial_scaler) {
          if (!m_targets.spatial_fence)
            m_targets.spatial_fence = [m_device newFence];
          m_targets.spatial_fence.label = @"Moppe MetalFX spatial fence";
          m_targets.spatial_scaler.fence = m_targets.spatial_fence;
          color_input_usage = m_targets.spatial_scaler.colorTextureUsage;
          reconstruction_output_usage =
            m_targets.spatial_scaler.outputTextureUsage;
        } else {
          m_targets.resolved_upscaling = ResolvedUpscaling::Linear;
          m_spatial_upscaling_supported = false;
          resolution_reason = "scaler-creation-failed";
          std::cerr << "moppe: MetalFX spatial scaler creation failed; "
                       "using linear enlargement"
                    << std::endl;
        }
      }
      if (resolved == ResolvedUpscaling::Temporal &&
          m_frame_interpolation_enabled && m_frame_interpolation_supported &&
          m_targets.temporal_scaler) {
        MTLFXFrameInterpolatorDescriptor* descriptor =
          [[MTLFXFrameInterpolatorDescriptor alloc] init];
        descriptor.colorTextureFormat = MTLPixelFormatRGBA16Float;
        descriptor.outputTextureFormat = MTLPixelFormatRGBA16Float;
        descriptor.depthTextureFormat = MTLPixelFormatDepth32Float;
        descriptor.motionTextureFormat = MTLPixelFormatRG16Float;
        descriptor.uiTextureFormat = MTLPixelFormatRGBA16Float;
        descriptor.scaler = m_targets.temporal_scaler;
        descriptor.inputWidth = w;
        descriptor.inputHeight = h;
        descriptor.outputWidth = drawable_w;
        descriptor.outputHeight = drawable_h;
        m_targets.frame_interpolator =
          [descriptor newFrameInterpolatorWithDevice:m_device
                                            compiler:m_compiler];
        if (m_targets.frame_interpolator) {
          m_targets.frame_interpolator.fence = m_targets.spatial_fence;
          interpolation_color_usage =
            m_targets.frame_interpolator.colorTextureUsage;
          interpolation_ui_usage = m_targets.frame_interpolator.uiTextureUsage;
          interpolation_output_usage =
            m_targets.frame_interpolator.outputTextureUsage;
        } else {
          m_frame_interpolation_supported = false;
          std::cerr << "moppe: MetalFX frame interpolator creation failed; "
                       "presenting rendered frames directly"
                    << std::endl;
        }
      }
      const bool temporal_scene =
        m_targets.resolved_upscaling == ResolvedUpscaling::Temporal;
      if (m_temporal_scene_pipelines != temporal_scene) {
        m_temporal_scene_pipelines = temporal_scene;
        build_pipelines ();
      }
      m_targets.scene_a = make_target (
        MTLPixelFormatRGBA16Float, w, h, 1, false, color_input_usage);
      // Without multisampling there is nothing to resolve, so the scene pass
      // draws straight into the texture the resolve would have produced.
      const int scene_samples = temporal_scene ? 1 : m_msaa_samples;
      m_targets.msaa_color =
        scene_samples > 1
          ? make_target (MTLPixelFormatRGBA16Float, w, h, scene_samples, true)
          : m_targets.scene_a;
      m_targets.msaa_depth =
        make_target (temporal_scene ? MTLPixelFormatDepth32Float
                                    : MTLPixelFormatDepth32Float_Stencil8,
                     w,
                     h,
                     scene_samples,
                     !temporal_scene,
                     depth_input_usage);
      if (temporal_scene) {
        m_targets.motion = make_target (
          MTLPixelFormatRG16Float, w, h, 1, false, motion_input_usage);
        m_targets.reactive = make_target (
          MTLPixelFormatR8Unorm, w, h, 1, false, reactive_input_usage);
      }
      m_targets.prev_frame = make_target (
        MTLPixelFormatRGBA16Float, drawable_w, drawable_h, 1, false);
      m_targets.post_a = make_target (
        MTLPixelFormatRGBA16Float, drawable_w, drawable_h, 1, false);
      m_targets.post_b = make_target (
        MTLPixelFormatRGBA16Float, drawable_w, drawable_h, 1, false);
      const int half_w = drawable_w / 2 > 0 ? drawable_w / 2 : 1;
      const int half_h = drawable_h / 2 > 0 ? drawable_h / 2 : 1;
      m_targets.shafts =
        make_target (MTLPixelFormatRGBA16Float, half_w, half_h, 1, false);
      m_targets.ao_a =
        make_target (MTLPixelFormatR8Unorm, half_w, half_h, 1, false);
      m_targets.ao_b =
        make_target (MTLPixelFormatR8Unorm, half_w, half_h, 1, false);
      const int bw = drawable_w / 4 > 0 ? drawable_w / 4 : 1;
      const int bh = drawable_h / 4 > 0 ? drawable_h / 4 : 1;
      m_targets.bloom_a =
        make_target (MTLPixelFormatRGBA16Float, bw, bh, 1, false);
      m_targets.bloom_b =
        make_target (MTLPixelFormatRGBA16Float, bw, bh, 1, false);
#if !TARGET_OS_IPHONE
      if (!m_water_reflection_path.empty ()) {
        const int rw = std::max (1, drawable_w / 4);
        const int rh = std::max (1, drawable_h / 4);
        for (MetalReflectionTargets& reflection : m_reflection_targets) {
          reflection.width = rw;
          reflection.height = rh;
          reflection.origin =
            make_target (MTLPixelFormatRGBA32Float, rw, rh, 1, false);
          reflection.optical_normal =
            make_target (MTLPixelFormatRGBA16Float, rw, rh, 1, false);
          reflection.depth =
            make_target (MTLPixelFormatDepth32Float, rw, rh, 1, true);
          reflection.radiance = make_target (MTLPixelFormatRGBA16Float,
                                             rw,
                                             rh,
                                             1,
                                             false,
                                             MTLTextureUsageShaderWrite);
          reflection.hit_normal = make_target (MTLPixelFormatRGBA16Float,
                                               rw,
                                               rh,
                                               1,
                                               false,
                                               MTLTextureUsageShaderWrite);
          reflection.hit_distance = make_target (MTLPixelFormatR32Float,
                                                 rw,
                                                 rh,
                                                 1,
                                                 false,
                                                 MTLTextureUsageShaderWrite);
          reflection.validity = make_target (MTLPixelFormatRGBA8Unorm,
                                             rw,
                                             rh,
                                             1,
                                             false,
                                             MTLTextureUsageShaderWrite);
          reflection.origin.label = @"Moppe reflection water origins";
          reflection.optical_normal.label =
            @"Moppe reflection water optical normals";
          reflection.radiance.label = @"Moppe raw water reflection radiance";
          reflection.hit_normal.label = @"Moppe water reflection hit normals";
          reflection.hit_distance.label =
            @"Moppe water reflection hit distance";
          reflection.validity.label = @"Moppe water reflection validity";
        }
      }
#endif
      if (m_targets.spatial_scaler || m_targets.temporal_scaler) {
        m_targets.spatial_output = make_target (MTLPixelFormatRGBA16Float,
                                                drawable_w,
                                                drawable_h,
                                                1,
                                                false,
                                                reconstruction_output_usage);
        m_targets.spatial_output.label = @"Moppe reconstructed HDR";
      }
      if (m_targets.temporal_scaler) {
        m_targets.exposure_tex =
          make_target (MTLPixelFormatR16Float, 1, 1, 1, false);
        m_targets.exposure_tex.label = @"Moppe temporal exposure";
      }
      if (m_targets.frame_interpolator) {
        for (int i = 0; i < FRAMES_IN_FLIGHT; ++i) {
          m_targets.interpolator_color[i] =
            make_target (MTLPixelFormatRGBA16Float,
                         drawable_w,
                         drawable_h,
                         1,
                         false,
                         interpolation_color_usage);
          m_targets.interpolator_composite[i] =
            make_target (MTLPixelFormatRGBA16Float,
                         drawable_w,
                         drawable_h,
                         1,
                         false,
                         interpolation_ui_usage);
          m_targets.interpolator_output[i] =
            make_target (MTLPixelFormatRGBA16Float,
                         drawable_w,
                         drawable_h,
                         1,
                         false,
                         interpolation_output_usage);
          m_targets.interpolator_color[i].label = @"Moppe interpolator color";
          m_targets.interpolator_composite[i].label =
            @"Moppe interpolator UI composite";
          m_targets.interpolator_output[i].label = @"Moppe interpolated output";
        }
      }
      std::cerr << "moppe: render targets: actual-drawable=" << drawable_w
                << 'x' << drawable_h << ", scene=" << w << 'x' << h << " ("
                << (w * (double)h / 1.0e6) << " MP)"
                << ", render-scale=" << scale << ", msaa=" << scene_samples
                << "x, upscaling=requested:"
                << upscaling_name (m_targets.requested_upscaling)
                << ",resolved:" << upscaling_name (m_targets.resolved_upscaling)
                << ",reason:" << resolution_reason << ", frame-interpolation="
                << (m_targets.frame_interpolator ? "on" : "off") << std::endl;
      if (!m_targets.probe_tex) {
        m_targets.probe_tex =
          make_target (MTLPixelFormatRGBA32Float, PROBE_W, PROBE_H, 1, false);
        for (int i = 0; i < FRAMES_IN_FLIGHT; ++i)
          m_targets.probe_buf[i] =
            [m_device newBufferWithLength:PROBE_W * PROBE_H * 16
                                  options:MTLResourceStorageModeShared];
        for (id<MTLBuffer> buffer : m_targets.probe_buf)
          make_resident (buffer);
      }
      // Freshly created: undefined contents until the first blur blit.
      m_targets.prev_valid = false;
      m_targets.temporal_history_valid = false;
      m_targets.interpolation_history_valid = false;
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
        ensure_targets (params.scene_scale,
                        params.render_scale_override,
                        params.scene_megapixel_budget,
                        params.upscaling);
      }
      const double targets_done = cpu_time ();
      if (!m_targets.msaa_color)
        return false;

      const uint64_t next_sequence = m_frame.sequence + 1;
      {
        MOPPE_PROFILE_ZONE ("MetalRenderer::wait_for_inflight_frame");
        constexpr uint64_t wait_timeout_ms = 1000;
        if (next_sequence > FRAMES_IN_FLIGHT &&
            ![m_frame.completion_event
              waitUntilSignaledValue:next_sequence - FRAMES_IN_FLIGHT
                           timeoutMS:wait_timeout_ms])
          throw std::runtime_error ("Timed out waiting for a Metal 4 frame");
        const uint64_t reused_sequence = next_sequence > FRAMES_IN_FLIGHT
                                           ? next_sequence - FRAMES_IN_FLIGHT
                                           : 0;
        if (reused_sequence && m_present_sequence >= reused_sequence &&
            ![m_present_completion_event
              waitUntilSignaledValue:reused_sequence
                           timeoutMS:wait_timeout_ms])
          throw std::runtime_error (
            "Timed out waiting for a Metal 4 presentation frame");
      }
      const double inflight_done = cpu_time ();

      {
        MOPPE_PROFILE_ZONE ("MetalRenderer::acquire_drawable");
        if (m_frame.pending_drawable) {
          m_frame.drawable = m_frame.pending_drawable;
          m_frame.pending_drawable = nil;
        } else {
          m_frame.drawable = [m_layer nextDrawable];
        }
      }
      const double drawable_done = cpu_time ();
      if (!m_frame.drawable)
        return false;
      if (m_profile_cpu) {
        m_cpu_frame_start = frame_start;
        m_cpu_encode_start = drawable_done;
        m_cpu_targets_total += targets_done - frame_start;
        m_cpu_inflight_total += inflight_done - targets_done;
        m_cpu_drawable_total += drawable_done - inflight_done;
      }

      m_frame.sequence = next_sequence;
      m_frame.slot = static_cast<int> ((next_sequence - 1) % FRAMES_IN_FLIGHT);
      m_frame.arena[m_frame.slot].reset ();
      id<MTL4CommandAllocator> allocator =
        m_frame.command_allocators[m_frame.slot];
      [allocator reset];
      // A command allocator is explicitly reusable after endCommandBuffer;
      // a command-buffer recording object is not resubmitted. Keeping this
      // distinction also makes the Metal validation layer agree with the
      // real Metal 4 submission path.
      m_frame.command_buffer = [m_device newCommandBuffer];
      [m_frame.command_buffer beginCommandBufferWithAllocator:allocator];
      if (m_frame.timestamp_heaps[m_frame.slot])
        [m_frame.timestamp_heaps[m_frame.slot]
          invalidateCounterRange:NSMakeRange (0, MAX_TIMESTAMP_SAMPLES)];
      m_frame.profile_this_frame = params.profile;
      m_frame.timestamp_count = 0;
      m_frame.sample_intervals.clear ();
      m_frame.current_gpu_pass = GpuPass::Count;

#if !TARGET_OS_IPHONE
      // MOPPE_METAL_CAPTURE_START delays the trace so it lands on the view
      // under study -- a demo ride, not the loading screen.
      static const int capture_start = [] {
        const char* start = ::getenv ("MOPPE_METAL_CAPTURE_START");
        return start ? std::max (0, ::atoi (start)) : 0;
      }();
      const bool capture_reached =
        ++m_frame.capture_start_frames > capture_start;
      if (params.profile && !m_frame.capture_path.empty () && capture_reached &&
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

      const CGSize points = m_layer.bounds.size;
      m_frame.scale = points.width > 0 ? (float)m_frame.drawable.texture.width /
                                           (float)points.width
                                       : 1.0f;
      m_frame.width_pts = (int)points.width;
      m_frame.height_pts = (int)points.height;

      m_frame.params = params;
      std::memset (&m_frame.uniforms, 0, sizeof (m_frame.uniforms));
      m_current_view_proj = params.proj * params.view;
      const bool temporal =
        m_targets.resolved_upscaling == ResolvedUpscaling::Temporal;
      m_frame.jitter_x = temporal ? halton (next_sequence, 2) - 0.5f : 0.0f;
      m_frame.jitter_y = temporal ? halton (next_sequence, 3) - 0.5f : 0.0f;
      Mat4 clip_jitter;
      clip_jitter.set_element (12, 2.0f * m_frame.jitter_x / m_targets.width);
      clip_jitter.set_element (13, -2.0f * m_frame.jitter_y / m_targets.height);
      const Mat4 jittered_view_proj =
        (temporal ? clip_jitter * params.proj : params.proj) * params.view;
      Mat4 sky_view = params.view;
      sky_view.set_element (12, 0.0f);
      sky_view.set_element (13, 0.0f);
      sky_view.set_element (14, 0.0f);
      m_current_sky_view_proj = params.proj * sky_view;
      m_frame.current_sky_view_proj = m_current_sky_view_proj;
      m_frame.previous_sky_view_proj = m_camera_history_valid
                                         ? m_previous_sky_view_proj
                                         : m_current_sky_view_proj;
      m_frame.uniforms.view_proj = m4 (jittered_view_proj);
      m_frame.uniforms.unjittered_view_proj = m4 (m_current_view_proj);
      m_frame.uniforms.previous_view_proj = m4 (
        m_camera_history_valid ? m_previous_view_proj : m_current_view_proj);
      m_frame.uniforms.light_matrix = m4 (m_terrain_resources.light_biased);
      m_frame.uniforms.camera_pos = f4 (params.camera_pos);
      m_frame.uniforms.sun_dir = f4 (params.sun_dir);
      m_frame.uniforms.sun_diffuse = f4lin (params.sun_diffuse);
      m_frame.uniforms.sun_specular = f4lin (params.sun_specular);
      m_frame.uniforms.ambient = f4lin (params.ambient);
      m_frame.uniforms.fog_color = f4lin (params.clear_color, params.fog_scale);
      m_frame.uniforms.misc.x = params.time;
      m_frame.uniforms.misc.y = params.cloud_cover;
      if (m_terrain_resources.have_terrain) {
        m_frame.uniforms.misc.z = m_terrain_resources.params.sea_level;
        m_frame.uniforms.misc.w = m_terrain_resources.params.land_relief;
      }
      m_frame.uniforms.shadow.x = m_terrain_resources.have_shadow
                                    ? m_terrain_resources.params.shadow_strength
                                    : 0.0f;
      m_frame.uniforms.shadow.y =
        m_terrain_resources.shadow_map
          ? 1.0f / (float)m_terrain_resources.shadow_map.width
          : 1.0f / 4096.0f;
      m_frame.uniforms.temporal.x = static_cast<float> (m_targets.width);
      m_frame.uniforms.temporal.y = static_cast<float> (m_targets.height);
      m_frame.uniforms.temporal.z =
        m_camera_history_valid ? m_previous_time : params.time;
      m_frame.uniforms.temporal.w = temporal ? 1.0f : 0.0f;

      update_exposure ();

      m_frame.frame_uniforms =
        m_frame.arena[m_frame.slot].write (m_frame.uniforms);
      m_frame.current_scene = m_targets.scene_a;
      m_frame.scene_encoder = nil;
      m_frame.scene_pass_done = false;
      m_frame.reconstructed = false;
      m_current_models.clear ();
      m_current_lists.clear ();
#if !TARGET_OS_IPHONE
      m_have_reflection_ocean = false;
#endif
      return true;
    }

    id<MTL4RenderCommandEncoder> MetalRenderer::scene_encoder () {
      if (m_frame.scene_encoder)
        return m_frame.scene_encoder;

      MTL4RenderPassDescriptor* rp = [[MTL4RenderPassDescriptor alloc] init];
      rp.colorAttachments[0].texture = m_targets.msaa_color;
      rp.colorAttachments[0].loadAction = MTLLoadActionClear;
      if (m_targets.msaa_color.sampleCount > 1) {
        rp.colorAttachments[0].resolveTexture = m_targets.scene_a;
        rp.colorAttachments[0].storeAction = MTLStoreActionMultisampleResolve;
      } else {
        rp.colorAttachments[0].storeAction = MTLStoreActionStore;
      }
      if (m_targets.motion) {
        rp.colorAttachments[1].texture = m_targets.motion;
        rp.colorAttachments[1].loadAction = MTLLoadActionClear;
        rp.colorAttachments[1].storeAction = MTLStoreActionStore;
        rp.colorAttachments[1].clearColor = MTLClearColorMake (0, 0, 0, 0);
      }
      if (m_targets.reactive) {
        rp.colorAttachments[2].texture = m_targets.reactive;
        rp.colorAttachments[2].loadAction = MTLLoadActionClear;
        rp.colorAttachments[2].storeAction = MTLStoreActionStore;
        rp.colorAttachments[2].clearColor = MTLClearColorMake (0, 0, 0, 0);
      }
      rp.colorAttachments[0].clearColor =
        MTLClearColorMake (std::pow (m_frame.params.clear_color.red, 2.2f),
                           std::pow (m_frame.params.clear_color.green, 2.2f),
                           std::pow (m_frame.params.clear_color.blue, 2.2f),
                           1.0);
      rp.depthAttachment.texture = m_targets.msaa_depth;
      rp.depthAttachment.loadAction = MTLLoadActionClear;
      rp.depthAttachment.storeAction = m_targets.temporal_scaler
                                         ? MTLStoreActionStore
                                         : MTLStoreActionDontCare;
      rp.depthAttachment.clearDepth = 0.0; // reversed-Z far
      if (!m_targets.temporal_scaler) {
        rp.stencilAttachment.texture = m_targets.msaa_depth;
        rp.stencilAttachment.loadAction = MTLLoadActionClear;
        rp.stencilAttachment.storeAction = MTLStoreActionDontCare;
        rp.stencilAttachment.clearStencil = 0;
      }

      m_frame.scene_encoder =
        [m_frame.command_buffer renderCommandEncoderWithDescriptor:rp];
      m_frame.scene_encoder.label = @"World scene";
      // Apple GPUs execute this as one tile render pass. Draw-level
      // timestamps inside it collapse to the pass boundary, so attribute the
      // encoder as a whole instead of publishing misleading terrain/sky
      // fragments.
      record_gpu_pass_start (m_frame, m_frame.scene_encoder, GpuPass::Scene);
      [m_frame.scene_encoder setFrontFacingWinding:MTLWindingCounterClockwise];
      return m_frame.scene_encoder;
    }

    void MetalRenderer::begin_gpu_pass (id<MTL4RenderCommandEncoder> enc,
                                        GpuPass pass) {
      (void)enc;
      (void)pass;
    }

    void MetalRenderer::end_scene_encoder () {
      if (m_frame.scene_encoder) {
        if (m_targets.spatial_fence)
          [m_frame.scene_encoder updateFence:m_targets.spatial_fence
                          afterEncoderStages:MTLStageFragment];
        record_gpu_pass_end (m_frame, m_frame.scene_encoder);
        [m_frame.scene_encoder endEncoding];
        m_frame.scene_encoder = nil;
        m_frame.scene_pass_done = true;
      } else if (!m_frame.scene_pass_done) {
        // Nothing was drawn: run an empty pass so sceneA is cleared.
        scene_encoder ();
        if (m_targets.spatial_fence)
          [m_frame.scene_encoder updateFence:m_targets.spatial_fence
                          afterEncoderStages:MTLStageFragment];
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
      MetalFrameEncoding& frame = inputs.frame;
      id<MTL4RenderCommandEncoder> enc = inputs.encoder;

      [enc setRenderPipelineState:pipelines.terrain];
      [enc setDepthStencilState:pipelines.depth[1][1]];
      [enc setCullMode:MTLCullModeBack];

      MoppeTerrainUniforms u;
      std::memset (&u, 0, sizeof (u));
      u.view_proj = frame.uniforms.view_proj;
      u.unjittered_view_proj = frame.uniforms.unjittered_view_proj;
      u.previous_view_proj = frame.uniforms.previous_view_proj;
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
      u.params1.x = 1.0f;
      u.params1.y = terrain.params.sea_level;
      u.params1.z = terrain.have_shadow ? terrain.params.shadow_strength : 0;
      u.params1.w = terrain.shadow_map ? 1.0f / (float)terrain.shadow_map.width
                                       : 1.0f / 4096.0f;
      u.params2.x = frame.params.time;
      u.params2.y = frame.params.cloud_cover;
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
      u.params6.x = terrain.params.fragment_normals ? 1.0f : 0.0f;
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
      u.params7.z = terrain.params.land_relief;
      u.params7.w = terrain.params.grass_cover_boost;
      u.temporal = frame.uniforms.temporal;

      const MTLGPUAddress uniforms = frame.arena[frame.slot].write (u);
      bind_address (frame, MTLRenderStageVertex, MOPPE_BUF_FRAME, uniforms);
      bind_address (frame, MTLRenderStageFragment, MOPPE_BUF_FRAME, uniforms);
      bind_texture (
        frame, MTLRenderStageVertex, MOPPE_TEX_HEIGHTS, terrain.heights);
      bind_texture (
        frame, MTLRenderStageVertex, MOPPE_TEX_NORMALS, terrain.normals);

      MetalTexture* fallback =
        static_cast<MetalTexture*> (pipelines.white.get ());
      MetalTexture* grass = static_cast<MetalTexture*> (terrain.grass.get ());
      MetalTexture* dirt = static_cast<MetalTexture*> (terrain.dirt.get ());
      MetalTexture* rock = static_cast<MetalTexture*> (terrain.rock.get ());
      MetalTexture* snow = static_cast<MetalTexture*> (terrain.snow.get ());
      bind_texture (frame,
                    MTLRenderStageFragment,
                    MOPPE_TEX_GRASS,
                    (grass ? grass : fallback)->texture);
      bind_sampler (
        frame, MTLRenderStageFragment, 0, (grass ? grass : fallback)->sampler);
      bind_texture (frame,
                    MTLRenderStageFragment,
                    MOPPE_TEX_DIRT,
                    (dirt ? dirt : fallback)->texture);
      bind_texture (frame,
                    MTLRenderStageFragment,
                    MOPPE_TEX_ROCK,
                    (rock ? rock : fallback)->texture);
      bind_texture (frame,
                    MTLRenderStageFragment,
                    MOPPE_TEX_SNOW,
                    (snow ? snow : fallback)->texture);
      bind_texture (frame,
                    MTLRenderStageFragment,
                    MOPPE_TEX_SHADOW,
                    terrain.shadow_map ? terrain.shadow_map
                                       : pipelines.shadow_fallback);
      bind_texture (frame,
                    MTLRenderStageFragment,
                    MOPPE_TEX_TERRAIN_OVERLAY,
                    terrain.overlay ? terrain.overlay : terrain.heights);
      bind_texture (frame,
                    MTLRenderStageFragment,
                    MOPPE_TEX_TERRAIN_MOISTURE,
                    terrain.have_moisture ? terrain.moisture : terrain.heights);
      bind_texture (frame,
                    MTLRenderStageFragment,
                    MOPPE_TEX_TERRAIN_WATER,
                    water.have_water_levels ? water.water_levels
                                            : terrain.heights);
      bind_texture (frame,
                    MTLRenderStageFragment,
                    MOPPE_TEX_TERRAIN_GEOLOGY,
                    terrain.have_geology ? terrain.geology : terrain.heights);
      bind_texture (frame,
                    MTLRenderStageFragment,
                    MOPPE_TEX_TERRAIN_NORMALS,
                    terrain.normals);
      bind_texture (frame,
                    MTLRenderStageFragment,
                    MOPPE_TEX_TERRAIN_SHORE,
                    terrain.have_shore ? terrain.shore : terrain.heights);
      bind_texture (frame,
                    MTLRenderStageFragment,
                    MOPPE_TEX_TERRAIN_PATHS,
                    terrain.have_paths ? terrain.paths : terrain.heights);
      bind_texture (frame,
                    MTLRenderStageFragment,
                    MOPPE_TEX_TERRAIN_FOREST,
                    terrain.have_forest ? terrain.forest : terrain.heights);
      bind_texture (frame,
                    MTLRenderStageFragment,
                    MOPPE_TEX_TERRAIN_SNOW_SUPPORT,
                    terrain.have_snow_support ? terrain.snow_support
                                              : terrain.heights);
      bind_texture (frame,
                    MTLRenderStageFragment,
                    MOPPE_TEX_TERRAIN_CHANNEL_FLUX,
                    terrain.have_channel_flux ? terrain.channel_flux
                                              : terrain.heights);
      use_arguments (enc, frame, MTLRenderStageVertex | MTLRenderStageFragment);

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
        bind_address (frame,
                      MTLRenderStageVertex,
                      MOPPE_BUF_CHUNK,
                      frame.arena[frame.slot].write (c));
        [enc drawIndexedPrimitives:MTLPrimitiveTypeTriangleStrip
                        indexCount:terrain.index_count[lod]
                         indexType:MTLIndexTypeUInt32
                       indexBuffer:terrain.indices[lod].gpuAddress
                 indexBufferLength:terrain.indices[lod].length
                     instanceCount:1];
      }
    }

    void MetalRenderer::draw_terrain (const ChunkDraw* chunks, int count) {
      if (!m_pipelines.terrain || !m_terrain_resources.have_terrain ||
          count == 0)
        return;
      id<MTL4RenderCommandEncoder> encoder = scene_encoder ();
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
      id<MTL4RenderCommandEncoder> enc = inputs.encoder;
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
        [inputs.residency addAllocation:scene.sky_verts];
        [inputs.residency commit];
      }

      [enc setRenderPipelineState:pipelines.sky];
      // Depth test on, write off: terrain occludes the cloud shader.
      [enc setDepthStencilState:pipelines.depth[1][0]];
      [enc setCullMode:MTLCullModeNone];

      MoppeSkyUniforms u;
      std::memset (&u, 0, sizeof (u));
      Mat4 sky_jitter;
      sky_jitter.set_element (
        12, 2.0f * frame.jitter_x / frame.uniforms.temporal.x);
      sky_jitter.set_element (
        13, -2.0f * frame.jitter_y / frame.uniforms.temporal.y);
      u.view_proj = m4 (sky_jitter * frame.current_sky_view_proj);
      u.unjittered_view_proj = m4 (frame.current_sky_view_proj);
      u.previous_view_proj = m4 (frame.previous_sky_view_proj);
      u.sun_dir = f4 (params.sun_dir);
      u.fog_color = f4lin (params.fog_color);
      u.params.x = params.time;
      u.params.y = params.sun_height;
      u.params.z = params.cloudiness;
      u.temporal = frame.uniforms.temporal;

      const MTLGPUAddress uniforms = frame.arena[frame.slot].write (u);
      bind_address (frame,
                    MTLRenderStageVertex,
                    MOPPE_BUF_VERTICES,
                    scene.sky_verts.gpuAddress);
      bind_address (frame, MTLRenderStageVertex, MOPPE_BUF_FRAME, uniforms);
      bind_address (frame, MTLRenderStageFragment, MOPPE_BUF_FRAME, uniforms);
      use_arguments (enc, frame, MTLRenderStageVertex | MTLRenderStageFragment);
      [enc drawPrimitives:MTLPrimitiveTypeTriangle
              vertexStart:0
              vertexCount:scene.sky_vcount];
    }

    void MetalRenderer::draw_sky (const SkyParams& params) {
      if (!m_pipelines.sky)
        return;
      id<MTL4RenderCommandEncoder> encoder = scene_encoder ();
      begin_gpu_pass (encoder, GpuPass::Sky);
      MetalScenePass::draw_sky ({ m_device,
                                  m_residency,
                                  encoder,
                                  m_pipelines,
                                  m_terrain_resources,
                                  m_scene_resources,
                                  m_frame },
                                params);
    }

    void MetalWaterPass::draw_ocean (const MetalWaterPassInputs& inputs,
                                     const OceanParams& params,
                                     id<MTLRenderPipelineState> ocean,
                                     id<MTLRenderPipelineState> tiles) {
      id<MTL4RenderCommandEncoder> enc = inputs.encoder;
      const MetalPipelines& pipelines = inputs.pipelines;
      const MetalTerrainResources& terrain = inputs.terrain;
      const MetalWaterResources& water_resources = inputs.water;
      MetalFrameEncoding& frame = inputs.frame;

      ocean = ocean ? ocean : pipelines.ocean;
      tiles = tiles ? tiles : pipelines.water_tiles;
      [enc setRenderPipelineState:ocean];
      [enc setDepthStencilState:pipelines.depth[1][1]];
      [enc setCullMode:MTLCullModeNone]; // visible from below too

      MoppeOceanUniforms u;
      std::memset (&u, 0, sizeof (u));
      u.view_proj = frame.uniforms.view_proj;
      u.unjittered_view_proj = frame.uniforms.unjittered_view_proj;
      u.previous_view_proj = frame.uniforms.previous_view_proj;
      u.light_matrix = m4 (terrain.light_biased);
      u.camera_pos = frame.uniforms.camera_pos;
      u.sun_dir = frame.uniforms.sun_dir;
      u.sun_diffuse = frame.uniforms.sun_diffuse;
      u.sun_specular = frame.uniforms.sun_specular;
      u.ambient = frame.uniforms.ambient;
      u.fog_color = f4lin (params.fog_color, params.fog_scale);
      u.params.x = params.time;
      u.params.y = water_resources.ocean_level;
      u.params.z = frame.params.cloud_cover;
      u.params.w = water_resources.have_water_levels ? 1.0f : 0.0f;
      u.world_offset.x = params.world_offset[0];
      u.world_offset.z = params.world_offset[2];
      u.shadow.x = terrain.have_shadow ? terrain.params.shadow_strength : 0.0f;
      u.shadow.y = terrain.shadow_map ? 1.0f / (float)terrain.shadow_map.width
                                      : 1.0f / 4096.0f;
      u.temporal = frame.uniforms.temporal;

      // Shore data: the fragment shader reads the height texture to
      // find the seabed for foam and shallows.
      if (terrain.have_terrain && terrain.heights) {
        u.shore.x = 1.0f / terrain.params.scale[0];
        u.shore.y = 1.0f / terrain.params.scale[2];
        u.shore.z = terrain.params.scale[1];
        u.shore.w = (float)terrain.params.width;
        bind_texture (
          frame, MTLRenderStageFragment, MOPPE_TEX_HEIGHTS, terrain.heights);
        bind_texture (
          frame, MTLRenderStageVertex, MOPPE_TEX_HEIGHTS, terrain.heights);
      }
      id<MTLTexture> water = water_resources.have_water_levels
                               ? water_resources.water_levels
                               : terrain.heights;
      if (!water)
        water = static_cast<MetalTexture*> (pipelines.white.get ())->texture;
      bind_texture (frame, MTLRenderStageVertex, MOPPE_TEX_WATER_LEVELS, water);
      bind_texture (
        frame, MTLRenderStageFragment, MOPPE_TEX_WATER_LEVELS_FRAGMENT, water);
      u.current.x = water_resources.have_water_flow ? 1.0f : 0.0f;
      bind_texture (frame,
                    MTLRenderStageFragment,
                    MOPPE_TEX_WATER_FLOW_FRAGMENT,
                    water_resources.have_water_flow ? water_resources.water_flow
                                                    : water);
      u.current.y = terrain.have_geology ? 1.0f : 0.0f;
      bind_texture (frame,
                    MTLRenderStageFragment,
                    MOPPE_TEX_WATER_GEOLOGY_FRAGMENT,
                    terrain.have_geology ? terrain.geology : water);
      bind_texture (frame,
                    MTLRenderStageFragment,
                    MOPPE_TEX_SHADOW,
                    terrain.shadow_map ? terrain.shadow_map
                                       : pipelines.shadow_fallback);

      // Near standing water renders on the terrain lattice through the
      // mesh pipeline; the coarse grid keeps the horizon. Both passes
      // discard on the same radius so they partition exactly.
      const bool lattice = tiles && water_resources.have_water_levels &&
                           terrain.have_terrain && terrain.heights;
      const float fine_radius = 700.0f;
      // Must match WATER_TILE_CELLS in ocean.metal: the CPU places the tile
      // window while the mesh shader expands each origin into exact cells.
      const float tile_world = 8.0f * terrain.params.scale[0];
      const int tiles_side = (int)std::ceil ((2.0f * fine_radius) / tile_world);
      if (lattice) {
        u.tiles.x =
          std::floor ((frame.params.camera_pos[0] - fine_radius) / tile_world);
        u.tiles.y =
          std::floor ((frame.params.camera_pos[2] - fine_radius) / tile_world);
        u.tiles.z = (float)tiles_side;
        u.tiles.w = fine_radius;
      }

      const MTLGPUAddress uniforms = frame.arena[frame.slot].write (u);
      bind_address (frame,
                    MTLRenderStageVertex,
                    MOPPE_BUF_VERTICES,
                    water_resources.ocean_verts.gpuAddress);
      bind_address (frame, MTLRenderStageVertex, MOPPE_BUF_FRAME, uniforms);
      bind_address (frame, MTLRenderStageFragment, MOPPE_BUF_FRAME, uniforms);
      use_arguments (enc, frame, MTLRenderStageVertex | MTLRenderStageFragment);
      [enc drawIndexedPrimitives:MTLPrimitiveTypeTriangle
                      indexCount:water_resources.ocean_icount
                       indexType:MTLIndexTypeUInt32
                     indexBuffer:water_resources.ocean_indices.gpuAddress
               indexBufferLength:water_resources.ocean_indices.length
                   instanceCount:1];

      if (lattice) {
        MoppeOceanUniforms t = u;
        t.tiles.w = -fine_radius;
        const MTLGPUAddress tile_uniforms = frame.arena[frame.slot].write (t);
        [enc setRenderPipelineState:tiles];
        for (MTLRenderStages stage :
             { MTLRenderStageObject, MTLRenderStageMesh }) {
          bind_address (frame, stage, MOPPE_BUF_FRAME, tile_uniforms);
          bind_texture (frame, stage, MOPPE_TEX_HEIGHTS, terrain.heights);
          bind_texture (
            frame, stage, MOPPE_TEX_WATER_LEVELS, water_resources.water_levels);
        }
        bind_address (
          frame, MTLRenderStageFragment, MOPPE_BUF_FRAME, tile_uniforms);
        use_arguments (enc,
                       frame,
                       MTLRenderStageObject | MTLRenderStageMesh |
                         MTLRenderStageFragment);
        const NSUInteger total =
          (NSUInteger)tiles_side * (NSUInteger)tiles_side;
        [enc drawMeshThreadgroups:MTLSizeMake ((total + 63) / 64, 1, 1)
          threadsPerObjectThreadgroup:MTLSizeMake (64, 1, 1)
            threadsPerMeshThreadgroup:MTLSizeMake (256, 1, 1)];
      }
    }

    void MetalRenderer::draw_ocean (const OceanParams& params) {
      if (!m_pipelines.ocean || !m_water_resources.ocean_verts)
        return;
      id<MTL4RenderCommandEncoder> encoder = scene_encoder ();
      begin_gpu_pass (encoder, GpuPass::Water);
      MetalWaterPass::draw_ocean ({ encoder,
                                    m_pipelines,
                                    m_terrain_resources,
                                    m_water_resources,
                                    m_frame },
                                  params);
#if !TARGET_OS_IPHONE
      if (!m_water_reflection_path.empty ()) {
        m_reflection_ocean = params;
        m_have_reflection_ocean = true;
      }
#endif
    }

    void MetalScenePass::draw_dust (const MetalScenePassInputs& inputs,
                                    std::span<const DustEmission> emissions,
                                    float logical_time) {
      id<MTL4RenderCommandEncoder> enc = inputs.encoder;
      const MetalPipelines& pipelines = inputs.pipelines;
      MetalFrameEncoding& frame = inputs.frame;

      MoppeDustUniforms uniforms {};
      uniforms.camera_right = f4 (frame.params.cam_right);
      uniforms.camera_up = f4 (frame.params.cam_up);
      uniforms.params.x = logical_time;
      uniforms.params.y = frame.uniforms.temporal.z;

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
          const MTLGPUAddress emissions_address =
            frame.arena[frame.slot].write (
              std::span<const MoppeDustEmission> (packed));
          const MTLGPUAddress dust_uniforms =
            frame.arena[frame.slot].write (uniforms);
          [enc setRenderPipelineState:mesh_pipeline];
          bind_address (
            frame, MTLRenderStageMesh, MOPPE_BUF_VERTICES, emissions_address);
          bind_address (
            frame, MTLRenderStageMesh, MOPPE_BUF_FRAME, frame.frame_uniforms);
          bind_address (
            frame, MTLRenderStageMesh, MOPPE_BUF_DRAW, dust_uniforms);
          use_arguments (enc, frame, MTLRenderStageMesh);
          [enc drawMeshThreadgroups:MTLSizeMake (packed.size (), 1, 1)
            threadsPerObjectThreadgroup:MTLSizeMake (1, 1, 1)
              threadsPerMeshThreadgroup:MTLSizeMake (64, 1, 1)];
          continue;
        }

        [enc setRenderPipelineState:additive ? pipelines.dust_add
                                             : pipelines.dust_soft];
        bind_address (
          frame, MTLRenderStageVertex, MOPPE_BUF_FRAME, frame.frame_uniforms);
        bind_address (frame,
                      MTLRenderStageVertex,
                      MOPPE_BUF_DRAW,
                      frame.arena[frame.slot].write (uniforms));
        for (const MoppeDustEmission& emission : packed) {
          bind_address (frame,
                        MTLRenderStageVertex,
                        MOPPE_BUF_VERTICES,
                        frame.arena[frame.slot].write (emission));
          use_arguments (enc, frame, MTLRenderStageVertex);
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
      id<MTL4RenderCommandEncoder> encoder = scene_encoder ();
      MetalScenePass::draw_dust ({ m_device,
                                   m_residency,
                                   encoder,
                                   m_pipelines,
                                   m_terrain_resources,
                                   m_scene_resources,
                                   m_frame },
                                 emissions,
                                 logical_time);
    }

    void MetalRenderer::draw_undergrowth (const UndergrowthParams& params) {
      const MetalTerrainResources& terrain = m_terrain_resources;
      const MetalWaterResources& water = m_water_resources;
      if (!m_pipelines.undergrowth || !terrain.have_terrain ||
          !terrain.have_forest || !terrain.have_moisture || !terrain.have_paths)
        return;
      {
        MoppeUndergrowthUniforms u;
        std::memset (&u, 0, sizeof (u));
        u.view_proj = m_frame.uniforms.view_proj;
        u.unjittered_view_proj = m_frame.uniforms.unjittered_view_proj;
        u.previous_view_proj = m_frame.uniforms.previous_view_proj;
        u.light_matrix = m_frame.uniforms.light_matrix;
        u.camera_pos = m_frame.uniforms.camera_pos;
        u.sun_dir = m_frame.uniforms.sun_dir;
        u.sun_diffuse = m_frame.uniforms.sun_diffuse;
        u.sun_specular = m_frame.uniforms.sun_specular;
        u.ambient = m_frame.uniforms.ambient;
        u.fog_color = m_frame.uniforms.fog_color;
        u.shadow = m_frame.uniforms.shadow;
        u.relief.x = m_frame.uniforms.misc.z;
        u.relief.y = m_frame.uniforms.misc.w;
        u.relief.z = terrain.have_snow_support ? 1.0f : 0.0f;
        u.relief.w = water.have_water_levels ? 1.0f : 0.0f;
        u.temporal = m_frame.uniforms.temporal;

        const TerrainParams& tp = terrain.params;
        u.lattice.x = 1.0f / tp.scale[0];
        u.lattice.y = 1.0f / tp.scale[2];
        u.lattice.z = tp.scale[1];
        u.lattice.w = (float)tp.width;

        // The tile window is anchored to the world lattice rather than to the
        // camera, so a plant keeps its identity as the rider moves and no
        // amount of travelling makes the floor reshuffle itself.
        // A sub-metre cell gives the field enough independent roots to read as
        // a sward rather than a lattice of broad four-way plant proxies.
        const float tile_world = 0.60f;
        const int tiles_side =
          (int)std::ceil ((2.0f * params.reach) / tile_world);
        u.tiles.x = std::floor ((m_frame.params.camera_pos[0] - params.reach) /
                                tile_world);
        u.tiles.y = std::floor ((m_frame.params.camera_pos[2] - params.reach) /
                                tile_world);
        u.tiles.z = (float)tiles_side;
        u.tiles.w = tile_world;
        u.params.x = params.time;
        u.params.y = params.cloud_cover;
        u.params.z = params.reach;
        u.params.w = params.density;
        u.interaction.x = params.interaction_position[0];
        u.interaction.y = params.interaction_position[1];
        u.interaction.z = params.interaction_position[2];
        u.interaction.w = params.interaction_radius;

        id<MTL4RenderCommandEncoder> enc = scene_encoder ();
        begin_gpu_pass (enc, GpuPass::Scene);
        [enc setRenderPipelineState:m_pipelines.undergrowth];
        [enc setDepthStencilState:m_pipelines.depth[1][1]];
        [enc setCullMode:MTLCullModeNone];
        const MTLGPUAddress uniforms = m_frame.arena[m_frame.slot].write (u);
        for (MTLRenderStages stage : { MTLRenderStageObject,
                                       MTLRenderStageMesh,
                                       MTLRenderStageFragment })
          bind_address (m_frame, stage, MOPPE_BUF_FRAME, uniforms);
        const auto bind = [&] (id<MTLTexture> texture, NSUInteger slot) {
          bind_texture (m_frame, MTLRenderStageObject, slot, texture);
          bind_texture (m_frame, MTLRenderStageMesh, slot, texture);
        };
        bind (terrain.heights, MOPPE_TEX_HEIGHTS);
        bind (terrain.normals, MOPPE_TEX_TERRAIN_NORMALS);
        bind (terrain.forest ? terrain.forest : terrain.heights,
              MOPPE_TEX_TERRAIN_FOREST);
        bind (terrain.moisture ? terrain.moisture : terrain.heights,
              MOPPE_TEX_TERRAIN_MOISTURE);
        bind (terrain.paths ? terrain.paths : terrain.heights,
              MOPPE_TEX_TERRAIN_PATHS);
        bind (terrain.have_snow_support ? terrain.snow_support
                                        : terrain.heights,
              MOPPE_TEX_TERRAIN_SNOW_SUPPORT);
        bind (water.have_water_levels ? water.water_levels : terrain.heights,
              MOPPE_TEX_TERRAIN_WATER);
        bind_texture (m_frame,
                      MTLRenderStageFragment,
                      MOPPE_TEX_SHADOW,
                      terrain.shadow_map ? terrain.shadow_map
                                         : m_pipelines.shadow_fallback);
        use_arguments (enc,
                       m_frame,
                       MTLRenderStageObject | MTLRenderStageMesh |
                         MTLRenderStageFragment);
        const NSUInteger total =
          (NSUInteger)tiles_side * (NSUInteger)tiles_side;
        [enc drawMeshThreadgroups:MTLSizeMake ((total + 63) / 64, 1, 1)
          threadsPerObjectThreadgroup:MTLSizeMake (64, 1, 1)
            threadsPerMeshThreadgroup:MTLSizeMake (
                                        MOPPE_UNDERGROWTH_MESH_THREADS, 1, 1)];
      }
    }

    void MetalRenderer::draw_forest () {
      const MetalForestResources& forest = m_forest_resources;
      const MetalTerrainResources& terrain = m_terrain_resources;
      if (!m_pipelines.forest || !forest.instances || forest.count == 0)
        return;

      MoppeForestUniforms u;
      std::memset (&u, 0, sizeof (u));
      u.view_proj = m_frame.uniforms.view_proj;
      u.unjittered_view_proj = m_frame.uniforms.unjittered_view_proj;
      u.previous_view_proj = m_frame.uniforms.previous_view_proj;
      u.light_matrix = m_frame.uniforms.light_matrix;
      u.camera_pos = m_frame.uniforms.camera_pos;
      u.sun_dir = m_frame.uniforms.sun_dir;
      u.sun_diffuse = m_frame.uniforms.sun_diffuse;
      u.sun_specular = m_frame.uniforms.sun_specular;
      u.ambient = m_frame.uniforms.ambient;
      u.fog_color = m_frame.uniforms.fog_color;
      u.world.x = forest.period_x;
      u.world.y = forest.period_z;
      u.world.z = static_cast<float> (forest.count);
      u.params = m_frame.uniforms.misc;
      u.shadow = m_frame.uniforms.shadow;
      u.temporal = m_frame.uniforms.temporal;

      id<MTL4RenderCommandEncoder> enc = scene_encoder ();
      begin_gpu_pass (enc, GpuPass::Scene);
      [enc setRenderPipelineState:m_pipelines.forest];
      [enc setDepthStencilState:m_pipelines.depth[1][1]];
      [enc setCullMode:MTLCullModeNone];
      const MTLGPUAddress uniforms = m_frame.arena[m_frame.slot].write (u);
      for (MTLRenderStages stage :
           { MTLRenderStageObject, MTLRenderStageMesh, MTLRenderStageFragment })
        bind_address (m_frame, stage, MOPPE_BUF_FRAME, uniforms);
      for (MTLRenderStages stage : { MTLRenderStageObject, MTLRenderStageMesh })
        bind_address (
          m_frame, stage, MOPPE_BUF_FOREST, forest.instances.gpuAddress);
      bind_texture (m_frame,
                    MTLRenderStageFragment,
                    MOPPE_TEX_SHADOW,
                    terrain.shadow_map ? terrain.shadow_map
                                       : m_pipelines.shadow_fallback);
      use_arguments (enc,
                     m_frame,
                     MTLRenderStageObject | MTLRenderStageMesh |
                       MTLRenderStageFragment);
      // One object threadgroup per individual: a hero assembly owns the
      // whole payload instead of sharing it with seven neighbours.
      [enc drawMeshThreadgroups:MTLSizeMake (forest.count, 1, 1)
        threadsPerObjectThreadgroup:MTLSizeMake (
                                      MOPPE_FOREST_OBJECT_THREADS, 1, 1)
          threadsPerMeshThreadgroup:MTLSizeMake (
                                      MOPPE_FOREST_MESH_THREADS, 1, 1)];
    }

    // -- draw lists ----------------------------------------------------

    MTLGPUAddress
    MetalDrawListEncoder::stream_vertices (MetalFrameEncoding& frame,
                                           const std::vector<Vertex>& verts) {
      return frame.arena[frame.slot].write (std::span<const Vertex> (verts));
    }

    void
    MetalDrawListEncoder::set_run_state (id<MTL4RenderCommandEncoder> enc,
                                         const MetalPipelines& pipelines,
                                         const MetalTerrainResources& terrain,
                                         MetalFrameEncoding& frame,
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
      bind_texture (
        frame, MTLRenderStageFragment, MOPPE_TEX_COLOR, mt->texture);
      if (!hud)
        bind_texture (frame,
                      MTLRenderStageFragment,
                      MOPPE_TEX_SHADOW,
                      terrain.shadow_map ? terrain.shadow_map
                                         : pipelines.shadow_fallback);
      bind_sampler (frame, MTLRenderStageFragment, 0, mt->sampler);
      use_arguments (enc, frame, MTLRenderStageFragment);
    }

    void MetalDrawListEncoder::play (const MetalDrawListInputs& inputs,
                                     const std::vector<Vertex>& verts,
                                     const std::vector<DrawList::Run>& runs,
                                     bool hud,
                                     const std::vector<Vertex>* previous) {
      id<MTL4RenderCommandEncoder> enc = inputs.encoder;
      MetalFrameEncoding& frame = inputs.frame;

      if (verts.empty ())
        return;
      const MTLGPUAddress vertices = stream_vertices (frame, verts);
      bind_address (frame, MTLRenderStageVertex, MOPPE_BUF_VERTICES, vertices);
      if (!hud) {
        const MTLGPUAddress previous_vertices =
          previous && previous != &verts ? stream_vertices (frame, *previous)
                                         : vertices;
        bind_address (frame,
                      MTLRenderStageVertex,
                      MOPPE_BUF_PREVIOUS_VERTICES,
                      previous_vertices);
      }
      use_arguments (enc, frame, MTLRenderStageVertex);

      for (size_t i = 0; i < runs.size (); ++i) {
        const DrawList::Run& r = runs[i];
        if (r.count == 0)
          continue;
        set_run_state (enc,
                       inputs.pipelines,
                       inputs.terrain,
                       frame,
                       r.state,
                       r.texture,
                       hud);
        [enc drawPrimitives:MTLPrimitiveTypeTriangle
                vertexStart:r.first
                vertexCount:r.count];
      }
    }

    void MetalScenePass::draw_list (const MetalScenePassInputs& inputs,
                                    const DrawList& list,
                                    const std::vector<Vertex>& previous,
                                    float reactive) {
      id<MTLDevice> device = inputs.device;
      id<MTL4RenderCommandEncoder> enc = inputs.encoder;
      const MetalPipelines& pipelines = inputs.pipelines;
      const MetalTerrainResources& terrain = inputs.terrain;
      MetalFrameEncoding& frame = inputs.frame;

      MoppeDrawUniforms du;
      std::memset (&du, 0, sizeof (du));
      du.model = m4 (Mat4 ());
      du.previous_model = m4 (Mat4 ());
      du.nrm0.x = 1;
      du.nrm1.y = 1;
      du.nrm2.z = 1;
      du.temporal.x = &previous == &list.vertices () ? 0.0f : 1.0f;
      du.temporal.y = reactive;
      bind_address (frame,
                    MTLRenderStageVertex,
                    MOPPE_BUF_DRAW,
                    frame.arena[frame.slot].write (du));
      bind_address (
        frame, MTLRenderStageVertex, MOPPE_BUF_FRAME, frame.frame_uniforms);
      bind_address (
        frame, MTLRenderStageFragment, MOPPE_BUF_FRAME, frame.frame_uniforms);

      MetalDrawListEncoder::play ({ device, enc, pipelines, terrain, frame },
                                  list.vertices (),
                                  list.runs (),
                                  false,
                                  &previous);
    }

    void MetalRenderer::draw_list (const DrawList& list, uint64_t motion_id) {
      if (list.empty ())
        return;
      id<MTL4RenderCommandEncoder> encoder = scene_encoder ();
      begin_gpu_pass (encoder, GpuPass::Scene);
      const std::vector<Vertex>* previous = &list.vertices ();
      bool have_previous = false;
      if (motion_id && m_camera_history_valid) {
        const auto found = m_previous_lists.find (motion_id);
        if (found != m_previous_lists.end () &&
            found->second.size () == list.vertices ().size ()) {
          previous = &found->second;
          have_previous = true;
        }
      }
      if (motion_id)
        m_current_lists[motion_id] = list.vertices ();
      MetalScenePass::draw_list ({ m_device,
                                   m_residency,
                                   encoder,
                                   m_pipelines,
                                   m_terrain_resources,
                                   m_scene_resources,
                                   m_frame },
                                 list,
                                 *previous,
                                 motion_id ? (have_previous ? 0.12f : 1.0f)
                                           : 0.0f);
    }

    void MetalScenePass::draw_mesh (const MetalScenePassInputs& inputs,
                                    const Mesh& mesh,
                                    const Mat4& model,
                                    const Mat4& previous_model,
                                    float reactive) {
      id<MTL4RenderCommandEncoder> enc = inputs.encoder;
      const MetalPipelines& pipelines = inputs.pipelines;
      const MetalTerrainResources& terrain = inputs.terrain;
      MetalFrameEncoding& frame = inputs.frame;
      const MetalMesh& m = (const MetalMesh&)mesh;

      MoppeDrawUniforms du;
      std::memset (&du, 0, sizeof (du));
      du.model = m4 (model);
      du.previous_model = m4 (previous_model);
      const NormalMat nm = NormalMat::from (model);
      du.nrm0 = f4 (nm.c0);
      du.nrm1 = f4 (nm.c1);
      du.nrm2 = f4 (nm.c2);
      du.temporal.y = reactive;
      bind_address (frame,
                    MTLRenderStageVertex,
                    MOPPE_BUF_DRAW,
                    frame.arena[frame.slot].write (du));
      bind_address (
        frame, MTLRenderStageVertex, MOPPE_BUF_FRAME, frame.frame_uniforms);
      bind_address (
        frame, MTLRenderStageFragment, MOPPE_BUF_FRAME, frame.frame_uniforms);
      bind_address (
        frame, MTLRenderStageVertex, MOPPE_BUF_VERTICES, m.vertices.gpuAddress);
      bind_address (frame,
                    MTLRenderStageVertex,
                    MOPPE_BUF_PREVIOUS_VERTICES,
                    m.vertices.gpuAddress);
      use_arguments (enc, frame, MTLRenderStageVertex | MTLRenderStageFragment);
      for (size_t i = 0; i < m.runs.size (); ++i) {
        const DrawList::Run& r = m.runs[i];
        if (r.count == 0)
          continue;
        MetalDrawListEncoder::set_run_state (
          enc, pipelines, terrain, frame, r.state, r.texture, false);
        [enc drawPrimitives:MTLPrimitiveTypeTriangle
                vertexStart:r.first
                vertexCount:r.count];
      }
    }

    void MetalRenderer::draw_mesh (const Mesh& mesh,
                                   const Mat4& model,
                                   uint64_t motion_id) {
      const MetalMesh& metal_mesh = (const MetalMesh&)mesh;
      if (!metal_mesh.vertices)
        return;
      id<MTL4RenderCommandEncoder> encoder = scene_encoder ();
      begin_gpu_pass (encoder, GpuPass::Scene);
      Mat4 previous = model;
      bool have_previous = false;
      if (motion_id && m_camera_history_valid) {
        const auto found = m_previous_models.find (motion_id);
        if (found != m_previous_models.end ()) {
          previous = found->second;
          have_previous = true;
        }
      }
      if (motion_id)
        m_current_models[motion_id] = model;
      MetalScenePass::draw_mesh ({ m_device,
                                   m_residency,
                                   encoder,
                                   m_pipelines,
                                   m_terrain_resources,
                                   m_scene_resources,
                                   m_frame },
                                 mesh,
                                 model,
                                 previous,
                                 motion_id && !have_previous ? 1.0f : 0.0f);
    }

    void MetalWaterPass::draw_waterfalls (const MetalWaterPassInputs& inputs,
                                          const Mesh& mesh,
                                          const Mat4& model) {
      id<MTL4RenderCommandEncoder> enc = inputs.encoder;
      const MetalPipelines& pipelines = inputs.pipelines;
      const MetalTerrainResources& terrain = inputs.terrain;
      MetalFrameEncoding& frame = inputs.frame;
      const MetalMesh& m = (const MetalMesh&)mesh;

      [enc setRenderPipelineState:pipelines.river];
      [enc
        setDepthStencilState:frame.params.upscaling == UpscalingMode::Temporal
                               ? pipelines.depth[1][0]
                               : pipelines.river_depth];
      if (frame.params.upscaling != UpscalingMode::Temporal)
        [enc setStencilReferenceValue:1];
      [enc setCullMode:MTLCullModeNone];

      MoppeDrawUniforms du;
      std::memset (&du, 0, sizeof (du));
      du.model = m4 (model);
      du.previous_model = m4 (model);
      const NormalMat nm = NormalMat::from (model);
      du.nrm0 = f4 (nm.c0);
      du.nrm1 = f4 (nm.c1);
      du.nrm2 = f4 (nm.c2);
      bind_address (frame,
                    MTLRenderStageVertex,
                    MOPPE_BUF_DRAW,
                    frame.arena[frame.slot].write (du));
      bind_address (
        frame, MTLRenderStageVertex, MOPPE_BUF_FRAME, frame.frame_uniforms);
      bind_address (
        frame, MTLRenderStageFragment, MOPPE_BUF_FRAME, frame.frame_uniforms);
      bind_texture (frame,
                    MTLRenderStageFragment,
                    MOPPE_TEX_SHADOW,
                    terrain.shadow_map ? terrain.shadow_map
                                       : pipelines.shadow_fallback);
      bind_address (
        frame, MTLRenderStageVertex, MOPPE_BUF_VERTICES, m.vertices.gpuAddress);
      use_arguments (enc, frame, MTLRenderStageVertex | MTLRenderStageFragment);
      for (const DrawList::Run& run : m.runs)
        if (run.count > 0)
          [enc drawPrimitives:MTLPrimitiveTypeTriangle
                  vertexStart:run.first
                  vertexCount:run.count];
    }

    void MetalRenderer::draw_waterfalls (const Mesh& mesh, const Mat4& model) {
      const MetalMesh& metal_mesh = (const MetalMesh&)mesh;
      if (!m_pipelines.river || !metal_mesh.vertices)
        return;
      id<MTL4RenderCommandEncoder> encoder = scene_encoder ();
      begin_gpu_pass (encoder, GpuPass::Water);
      MetalWaterPass::draw_waterfalls ({ encoder,
                                         m_pipelines,
                                         m_terrain_resources,
                                         m_water_resources,
                                         m_frame },
                                       mesh,
                                       model);
    }

    void MetalRenderer::reconstruct_scene () {
      if (m_frame.reconstructed)
        return;
      end_scene_encoder ();
      m_frame.reconstructed = true;

      if (m_targets.resolved_upscaling == ResolvedUpscaling::Native)
        return;

      if (m_targets.temporal_scaler && m_targets.spatial_output &&
          m_targets.motion && m_targets.reactive && m_targets.exposure_tex) {
        // Materialize a neutral exposure in the exact 1x1 R16F contract
        // MetalFX consumes. Reconstruction owns linear HDR pixels; Moppe's
        // bloom and present passes apply the adapted exposure afterward, just
        // as they do for native, spatial, and linear reconstruction.
        MTL4RenderPassDescriptor* exposure_pass =
          [[MTL4RenderPassDescriptor alloc] init];
        exposure_pass.colorAttachments[0].texture = m_targets.exposure_tex;
        exposure_pass.colorAttachments[0].loadAction = MTLLoadActionDontCare;
        exposure_pass.colorAttachments[0].storeAction = MTLStoreActionStore;
        id<MTL4RenderCommandEncoder> exposure_encoder = [m_frame.command_buffer
          renderCommandEncoderWithDescriptor:exposure_pass];
        exposure_encoder.label = @"Temporal exposure";
        wait_for_render_or_blit_writes (exposure_encoder);
        [exposure_encoder setRenderPipelineState:m_pipelines.exposure];
        MoppeQuadUniforms exposure {};
        exposure.tint.x = 1.0f;
        const MTLGPUAddress exposure_uniforms =
          m_frame.arena[m_frame.slot].write (exposure);
        bind_address (
          m_frame, MTLRenderStageVertex, MOPPE_BUF_FRAME, exposure_uniforms);
        bind_address (
          m_frame, MTLRenderStageFragment, MOPPE_BUF_FRAME, exposure_uniforms);
        use_arguments (exposure_encoder,
                       m_frame,
                       MTLRenderStageVertex | MTLRenderStageFragment);
        [exposure_encoder drawPrimitives:MTLPrimitiveTypeTriangle
                             vertexStart:0
                             vertexCount:3];
        [exposure_encoder endEncoding];

        id<MTL4FXTemporalScaler> scaler = m_targets.temporal_scaler;
        scaler.colorTexture = m_frame.current_scene;
        scaler.depthTexture = m_targets.msaa_depth;
        scaler.motionTexture = m_targets.motion;
        scaler.reactiveMaskTexture = m_targets.reactive;
        scaler.exposureTexture = m_targets.exposure_tex;
        scaler.outputTexture = m_targets.spatial_output;
        scaler.inputContentWidth = m_targets.width;
        scaler.inputContentHeight = m_targets.height;
        scaler.preExposure = 1.0f;
        scaler.jitterOffsetX = m_frame.jitter_x;
        scaler.jitterOffsetY = m_frame.jitter_y;
        scaler.motionVectorScaleX = 1.0f;
        scaler.motionVectorScaleY = 1.0f;
        scaler.depthReversed = YES;
        scaler.reset = !m_targets.temporal_history_valid;
        [m_frame.command_buffer pushDebugGroup:@"Temporal MetalFX"];
#if !TARGET_OS_TV
        record_gpu_pass_start (m_frame, GpuPass::Reconstruction);
#endif
        [scaler encodeToCommandBuffer:m_frame.command_buffer];
#if !TARGET_OS_TV
        record_gpu_pass_end (m_frame);
#endif
        [m_frame.command_buffer popDebugGroup];
        m_targets.temporal_history_valid = true;
        m_frame.current_scene = m_targets.spatial_output;
        return;
      }

      if (m_targets.spatial_scaler && m_targets.spatial_output) {
        m_targets.spatial_scaler.colorTexture = m_frame.current_scene;
        m_targets.spatial_scaler.inputContentWidth = m_targets.width;
        m_targets.spatial_scaler.inputContentHeight = m_targets.height;
        m_targets.spatial_scaler.outputTexture = m_targets.spatial_output;
        [m_frame.command_buffer pushDebugGroup:@"Spatial MetalFX"];
#if !TARGET_OS_TV
        record_gpu_pass_start (m_frame, GpuPass::Reconstruction);
#endif
        [m_targets.spatial_scaler encodeToCommandBuffer:m_frame.command_buffer];
#if !TARGET_OS_TV
        record_gpu_pass_end (m_frame);
#endif
        [m_frame.command_buffer popDebugGroup];
        m_frame.current_scene = m_targets.spatial_output;
        return;
      }

      // Exact linear fallback, promoted to output resolution before post.
      MTL4RenderPassDescriptor* pass = [[MTL4RenderPassDescriptor alloc] init];
      pass.colorAttachments[0].texture = m_targets.post_a;
      pass.colorAttachments[0].loadAction = MTLLoadActionDontCare;
      pass.colorAttachments[0].storeAction = MTLStoreActionStore;
      id<MTL4RenderCommandEncoder> encoder =
        [m_frame.command_buffer renderCommandEncoderWithDescriptor:pass];
      encoder.label = @"Linear reconstruction";
      wait_for_render_or_blit_writes (encoder);
      record_gpu_pass_start (m_frame, encoder, GpuPass::Reconstruction);
      [encoder setRenderPipelineState:m_pipelines.copy];
      MoppeQuadUniforms q {};
      q.tint.x = q.tint.y = q.tint.z = q.tint.w = 1.0f;
      q.params.x = 1.0f;
      const MTLGPUAddress uniforms = m_frame.arena[m_frame.slot].write (q);
      bind_address (m_frame, MTLRenderStageVertex, MOPPE_BUF_FRAME, uniforms);
      bind_address (m_frame, MTLRenderStageFragment, MOPPE_BUF_FRAME, uniforms);
      bind_texture (m_frame,
                    MTLRenderStageFragment,
                    MOPPE_TEX_SCENE,
                    m_frame.current_scene);
      use_arguments (
        encoder, m_frame, MTLRenderStageVertex | MTLRenderStageFragment);
      [encoder drawPrimitives:MTLPrimitiveTypeTriangle
                  vertexStart:0
                  vertexCount:3];
      [encoder endEncoding];
      m_frame.current_scene = m_targets.post_a;
    }

    // -- post ----------------------------------------------------------

    void MetalPostPass::apply_gtao (const MetalPostPassInputs& inputs,
                                    const MoppeGtaoUniforms& gtao,
                                    id<MTLTexture> scene_depth) {
      const MetalPipelines& pipelines = inputs.pipelines;
      MetalFrameTargets& targets = inputs.targets;
      MetalFrameEncoding& frame = inputs.frame;

      MoppeQuadUniforms q;
      std::memset (&q, 0, sizeof (q));
      q.tint.x = q.tint.y = q.tint.z = q.tint.w = 1;
      q.params.x = 1; // no zoom
      const MTLGPUAddress quad_uniforms = frame.arena[frame.slot].write (q);

      const auto quad_pass = [&] (id<MTLRenderPipelineState> pipeline,
                                  id<MTLTexture> destination,
                                  NSString* label,
                                  const MoppeGtaoUniforms& uniforms,
                                  id<MTLTexture> scene,
                                  id<MTLTexture> occlusion,
                                  bool fence) {
        MTL4RenderPassDescriptor* rp = [[MTL4RenderPassDescriptor alloc] init];
        rp.colorAttachments[0].texture = destination;
        rp.colorAttachments[0].loadAction = MTLLoadActionDontCare;
        rp.colorAttachments[0].storeAction = MTLStoreActionStore;
        id<MTL4RenderCommandEncoder> enc =
          [frame.command_buffer renderCommandEncoderWithDescriptor:rp];
        enc.label = label;
        if (fence && targets.spatial_fence)
          [enc waitForFence:targets.spatial_fence
            beforeEncoderStages:MTLStageFragment];
        wait_for_render_or_blit_writes (enc);
        record_gpu_pass_start (frame, enc, GpuPass::Post);
        [enc setRenderPipelineState:pipeline];
        bind_address (
          frame, MTLRenderStageVertex, MOPPE_BUF_FRAME, quad_uniforms);
        bind_address (frame,
                      MTLRenderStageFragment,
                      MOPPE_BUF_FRAME,
                      frame.arena[frame.slot].write (uniforms));
        if (scene)
          bind_texture (frame, MTLRenderStageFragment, MOPPE_TEX_SCENE, scene);
        if (occlusion)
          bind_texture (
            frame, MTLRenderStageFragment, MOPPE_TEX_BLOOM, occlusion);
        bind_texture (
          frame, MTLRenderStageFragment, MOPPE_TEX_POST_DEPTH, scene_depth);
        use_arguments (
          enc, frame, MTLRenderStageVertex | MTLRenderStageFragment);
        [enc drawPrimitives:MTLPrimitiveTypeTriangle
                vertexStart:0
                vertexCount:3];
        if (fence && targets.spatial_fence)
          [enc updateFence:targets.spatial_fence
            afterEncoderStages:MTLStageFragment];
        [enc endEncoding];
      };

      MoppeGtaoUniforms horizontal = gtao;
      horizontal.blur.x = 1.0f / (float)targets.ao_a.width;
      horizontal.blur.y = 0.0f;
      MoppeGtaoUniforms vertical = gtao;
      vertical.blur.x = 0.0f;
      vertical.blur.y = 1.0f / (float)targets.ao_a.height;

      quad_pass (
        pipelines.gtao, targets.ao_a, @"GTAO gather", gtao, nil, nil, false);
      quad_pass (pipelines.gtao_blur,
                 targets.ao_b,
                 @"GTAO blur",
                 horizontal,
                 nil,
                 targets.ao_a,
                 false);
      quad_pass (pipelines.gtao_blur,
                 targets.ao_a,
                 @"GTAO blur",
                 vertical,
                 nil,
                 targets.ao_b,
                 false);

      id<MTLTexture> src = frame.current_scene;
      id<MTLTexture> dst =
        (src == targets.post_a) ? targets.post_b : targets.post_a;
      quad_pass (pipelines.gtao_apply,
                 dst,
                 @"GTAO apply",
                 gtao,
                 src,
                 targets.ao_a,
                 true);
      frame.current_scene = dst;
    }

    void MetalRenderer::apply_gtao (const GtaoParams& params) {
      if (!m_pipelines.gtao || !m_pipelines.gtao_blur ||
          !m_pipelines.gtao_apply || !m_targets.ao_a || !m_targets.ao_b ||
          !m_frame.command_buffer)
        return;
      // Occlusion needs the stored scene depth, which only the temporal
      // path keeps.
      if (!m_targets.temporal_scaler || !m_targets.msaa_depth)
        return;
      reconstruct_scene ();

      MoppeGtaoUniforms u;
      std::memset (&u, 0, sizeof (u));
      u.camera_pos = f4 (position_value (params.camera_pos));
      u.ray_forward = f4 (params.forward);
      u.ray_right = f4 (params.right_span);
      u.ray_up = f4 (params.up_span);
      u.params.x = params.radius.numerical_value_in (u::m);
      u.params.y = scalar_value (params.strength);
      u.params.z = params.near_plane.numerical_value_in (u::m);
      u.params.w = params.far_plane.numerical_value_in (u::m);
      MetalPostPass::apply_gtao (
        { m_pipelines, m_targets, m_frame }, u, m_targets.msaa_depth);
    }

    void MetalPostPass::apply_light_shafts (const MetalPostPassInputs& inputs,
                                            const MoppeShaftUniforms& shaft,
                                            id<MTLTexture> scene_depth,
                                            id<MTLTexture> shadow_map) {
      const MetalPipelines& pipelines = inputs.pipelines;
      MetalFrameTargets& targets = inputs.targets;
      MetalFrameEncoding& frame = inputs.frame;

      // The shared fullscreen vertex still wants quad uniforms; the march
      // parameters ride the fragment stage at the same slot.
      MoppeQuadUniforms q;
      std::memset (&q, 0, sizeof (q));
      q.tint.x = q.tint.y = q.tint.z = q.tint.w = 1;
      q.params.x = 1; // no zoom
      const MTLGPUAddress quad_uniforms = frame.arena[frame.slot].write (q);

      // Gather: march at half resolution into the scatter target.
      MTL4RenderPassDescriptor* rp = [[MTL4RenderPassDescriptor alloc] init];
      rp.colorAttachments[0].texture = targets.shafts;
      rp.colorAttachments[0].loadAction = MTLLoadActionDontCare;
      rp.colorAttachments[0].storeAction = MTLStoreActionStore;
      id<MTL4RenderCommandEncoder> enc =
        [frame.command_buffer renderCommandEncoderWithDescriptor:rp];
      enc.label = @"Sun shafts gather";
      wait_for_render_or_blit_writes (enc);
      record_gpu_pass_start (frame, enc, GpuPass::Post);
      [enc setRenderPipelineState:pipelines.shafts];
      bind_address (
        frame, MTLRenderStageVertex, MOPPE_BUF_FRAME, quad_uniforms);
      bind_address (frame,
                    MTLRenderStageFragment,
                    MOPPE_BUF_FRAME,
                    frame.arena[frame.slot].write (shaft));
      bind_texture (
        frame, MTLRenderStageFragment, MOPPE_TEX_POST_DEPTH, scene_depth);
      bind_texture (
        frame, MTLRenderStageFragment, MOPPE_TEX_SHADOW, shadow_map);
      use_arguments (enc, frame, MTLRenderStageVertex | MTLRenderStageFragment);
      [enc drawPrimitives:MTLPrimitiveTypeTriangle vertexStart:0 vertexCount:3];
      [enc endEncoding];

      // Add: bilinear-upsample the scatter onto the full-size scene.
      id<MTLTexture> src = frame.current_scene;
      id<MTLTexture> dst =
        (src == targets.post_a) ? targets.post_b : targets.post_a;
      MTL4RenderPassDescriptor* add = [[MTL4RenderPassDescriptor alloc] init];
      add.colorAttachments[0].texture = dst;
      add.colorAttachments[0].loadAction = MTLLoadActionDontCare;
      add.colorAttachments[0].storeAction = MTLStoreActionStore;
      enc = [frame.command_buffer renderCommandEncoderWithDescriptor:add];
      enc.label = @"Sun shafts add";
      if (targets.spatial_fence)
        [enc waitForFence:targets.spatial_fence
          beforeEncoderStages:MTLStageFragment];
      wait_for_render_or_blit_writes (enc);
      record_gpu_pass_start (frame, enc, GpuPass::Post);
      [enc setRenderPipelineState:pipelines.shafts_add];
      bind_address (
        frame, MTLRenderStageVertex, MOPPE_BUF_FRAME, quad_uniforms);
      bind_texture (frame, MTLRenderStageFragment, MOPPE_TEX_SCENE, src);
      bind_texture (
        frame, MTLRenderStageFragment, MOPPE_TEX_BLOOM, targets.shafts);
      use_arguments (enc, frame, MTLRenderStageVertex | MTLRenderStageFragment);
      [enc drawPrimitives:MTLPrimitiveTypeTriangle vertexStart:0 vertexCount:3];
      if (targets.spatial_fence)
        [enc updateFence:targets.spatial_fence
          afterEncoderStages:MTLStageFragment];
      [enc endEncoding];

      frame.current_scene = dst;
    }

    void MetalRenderer::apply_light_shafts (const LightShaftParams& params) {
      if (!m_pipelines.shafts || !m_pipelines.shafts_add || !m_targets.shafts ||
          !m_frame.command_buffer)
        return;
      // The march needs the stored scene depth, which only the temporal
      // path keeps, and the per-frame camera-local shadow map.
      if (!m_targets.temporal_scaler || !m_targets.msaa_depth)
        return;
      if (!m_terrain_resources.shadow_map || !m_terrain_resources.have_shadow)
        return;
      reconstruct_scene ();

      MoppeShaftUniforms u;
      std::memset (&u, 0, sizeof (u));
      u.view_proj = m4 (m_current_view_proj);
      u.light_matrix = m4 (m_terrain_resources.light_biased);
      u.camera_pos = f4 (position_value (params.camera_pos));
      u.ray_forward = f4 (params.forward);
      u.ray_right = f4 (params.right_span);
      u.ray_up = f4 (params.up_span);
      u.sun_dir = f4 (params.sun_dir);
      u.sun_color = f4lin (params.sun_color, scalar_value (params.strength));
      u.params.x = params.max_distance.numerical_value_in (u::m);
      u.params.y = 0.011f; // extinction per metre
      u.params.z = 24.0f;  // march steps
      MetalPostPass::apply_light_shafts ({ m_pipelines, m_targets, m_frame },
                                         u,
                                         m_targets.msaa_depth,
                                         m_terrain_resources.shadow_map);
    }

    void MetalPostPass::apply_underwater (const MetalPostPassInputs& inputs,
                                          float time) {
      const MetalPipelines& pipelines = inputs.pipelines;
      MetalFrameTargets& targets = inputs.targets;
      MetalFrameEncoding& frame = inputs.frame;

      id<MTLTexture> src = frame.current_scene;
      id<MTLTexture> dst =
        (src == targets.post_a) ? targets.post_b : targets.post_a;

      MTL4RenderPassDescriptor* rp = [[MTL4RenderPassDescriptor alloc] init];
      rp.colorAttachments[0].texture = dst;
      rp.colorAttachments[0].loadAction = MTLLoadActionDontCare;
      rp.colorAttachments[0].storeAction = MTLStoreActionStore;

      id<MTL4RenderCommandEncoder> enc =
        [frame.command_buffer renderCommandEncoderWithDescriptor:rp];
      enc.label = @"Underwater post-process";
      if (targets.spatial_fence)
        [enc waitForFence:targets.spatial_fence
          beforeEncoderStages:MTLStageFragment];
      wait_for_render_or_blit_writes (enc);
      record_gpu_pass_start (frame, enc, GpuPass::Post);
      MoppeQuadUniforms q;
      std::memset (&q, 0, sizeof (q));
      q.tint.x = q.tint.y = q.tint.z = q.tint.w = 1;
      q.params.x = 1; // no zoom
      q.params.y = time;
      [enc setRenderPipelineState:pipelines.underwater];
      const MTLGPUAddress uniforms = frame.arena[frame.slot].write (q);
      bind_address (frame, MTLRenderStageVertex, MOPPE_BUF_FRAME, uniforms);
      bind_address (frame, MTLRenderStageFragment, MOPPE_BUF_FRAME, uniforms);
      bind_texture (frame, MTLRenderStageFragment, MOPPE_TEX_SCENE, src);
      use_arguments (enc, frame, MTLRenderStageVertex | MTLRenderStageFragment);
      [enc drawPrimitives:MTLPrimitiveTypeTriangle vertexStart:0 vertexCount:3];
      if (targets.spatial_fence)
        [enc updateFence:targets.spatial_fence
          afterEncoderStages:MTLStageFragment];
      [enc endEncoding];

      frame.current_scene = dst;
    }

    void MetalRenderer::apply_underwater (float time) {
      if (!m_pipelines.underwater)
        return;
      reconstruct_scene ();
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
        id<MTL4ComputeCommandEncoder> prime =
          [frame.command_buffer computeCommandEncoder];
        if (targets.spatial_fence)
          [prime waitForFence:targets.spatial_fence
            beforeEncoderStages:MTLStageBlit];
        wait_for_render_writes (prime);
        [prime copyFromTexture:frame.current_scene
                     toTexture:targets.prev_frame];
        [prime endEncoding];
        targets.prev_valid = true;
        return;
      }

      MTL4RenderPassDescriptor* rp = [[MTL4RenderPassDescriptor alloc] init];
      rp.colorAttachments[0].texture = frame.current_scene;
      rp.colorAttachments[0].loadAction = MTLLoadActionLoad;
      rp.colorAttachments[0].storeAction = MTLStoreActionStore;

      id<MTL4RenderCommandEncoder> enc =
        [frame.command_buffer renderCommandEncoderWithDescriptor:rp];
      enc.label = @"Motion blur";
      if (targets.spatial_fence)
        [enc waitForFence:targets.spatial_fence
          beforeEncoderStages:MTLStageFragment];
      wait_for_render_or_blit_writes (enc);
      record_gpu_pass_start (frame, enc, GpuPass::Post);
      [enc setRenderPipelineState:pipelines.ghost];
      for (int i = 1; i <= 3; ++i) {
        MoppeQuadUniforms q;
        std::memset (&q, 0, sizeof (q));
        const float alpha = 0.5f * strength / i;
        q.tint.x = q.tint.y = q.tint.z = 1;
        q.tint.w = alpha;
        q.params.x = 1.0f + 0.012f * i * strength;
        const MTLGPUAddress uniforms = frame.arena[frame.slot].write (q);
        bind_address (frame, MTLRenderStageVertex, MOPPE_BUF_FRAME, uniforms);
        bind_address (frame, MTLRenderStageFragment, MOPPE_BUF_FRAME, uniforms);
        bind_texture (
          frame, MTLRenderStageFragment, MOPPE_TEX_SCENE, targets.prev_frame);
        use_arguments (
          enc, frame, MTLRenderStageVertex | MTLRenderStageFragment);
        [enc drawPrimitives:MTLPrimitiveTypeTriangle
                vertexStart:0
                vertexCount:3];
      }
      if (targets.spatial_fence)
        [enc updateFence:targets.spatial_fence
          afterEncoderStages:MTLStageFragment];
      [enc endEncoding];

      id<MTL4ComputeCommandEncoder> blit =
        [frame.command_buffer computeCommandEncoder];
      if (targets.spatial_fence)
        [blit waitForFence:targets.spatial_fence
          beforeEncoderStages:MTLStageBlit];
      wait_for_render_writes (blit);
      [blit copyFromTexture:frame.current_scene toTexture:targets.prev_frame];
      [blit endEncoding];
    }

    void MetalRenderer::apply_motion_blur (float strength) {
      if (!m_pipelines.ghost || strength <= 0.01f)
        return;
      reconstruct_scene ();
      MetalPostPass::apply_motion_blur ({ m_pipelines, m_targets, m_frame },
                                        strength);
    }

    void MetalPostPass::apply_scene_blur (const MetalPostPassInputs& inputs) {
      const MetalPipelines& pipelines = inputs.pipelines;
      MetalFrameTargets& targets = inputs.targets;
      MetalFrameEncoding& frame = inputs.frame;

      const auto pass = [&frame, &targets] (NSString* label,
                                            id<MTLRenderPipelineState> pipeline,
                                            id<MTLTexture> src,
                                            id<MTLTexture> dst,
                                            const MoppeQuadUniforms& q) {
        MTL4RenderPassDescriptor* rp = [[MTL4RenderPassDescriptor alloc] init];
        rp.colorAttachments[0].texture = dst;
        rp.colorAttachments[0].loadAction = MTLLoadActionDontCare;
        rp.colorAttachments[0].storeAction = MTLStoreActionStore;
        id<MTL4RenderCommandEncoder> enc =
          [frame.command_buffer renderCommandEncoderWithDescriptor:rp];
        enc.label = label;
        if (targets.spatial_fence)
          [enc waitForFence:targets.spatial_fence
            beforeEncoderStages:MTLStageFragment];
        wait_for_render_or_blit_writes (enc);
        record_gpu_pass_start (frame, enc, GpuPass::Post);
        [enc setRenderPipelineState:pipeline];
        const MTLGPUAddress uniforms = frame.arena[frame.slot].write (q);
        bind_address (frame, MTLRenderStageVertex, MOPPE_BUF_FRAME, uniforms);
        bind_address (frame, MTLRenderStageFragment, MOPPE_BUF_FRAME, uniforms);
        bind_texture (frame, MTLRenderStageFragment, MOPPE_TEX_SCENE, src);
        use_arguments (
          enc, frame, MTLRenderStageVertex | MTLRenderStageFragment);
        [enc drawPrimitives:MTLPrimitiveTypeTriangle
                vertexStart:0
                vertexCount:3];
        if (targets.spatial_fence &&
            (dst == targets.post_a || dst == targets.post_b))
          [enc updateFence:targets.spatial_fence
            afterEncoderStages:MTLStageFragment];
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
      id<MTLTexture> dst =
        frame.current_scene == targets.post_a ? targets.post_b : targets.post_a;
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
      reconstruct_scene ();
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
      const float presentation_exposure = targets.exposure;

      // One fullscreen pass into `dst` reading `src`.
      auto quad_pass = [&] (GpuPass pass,
                            NSString* label,
                            id<MTLRenderPipelineState> pso,
                            id<MTLTexture> src,
                            id<MTLTexture> dst,
                            const MoppeQuadUniforms& q) {
        MTL4RenderPassDescriptor* p = [[MTL4RenderPassDescriptor alloc] init];
        p.colorAttachments[0].texture = dst;
        p.colorAttachments[0].loadAction = MTLLoadActionDontCare;
        p.colorAttachments[0].storeAction = MTLStoreActionStore;
        const auto encode = [&] {
          id<MTL4RenderCommandEncoder> e =
            [frame.command_buffer renderCommandEncoderWithDescriptor:p];
          e.label = label;
          if (targets.spatial_fence)
            [e waitForFence:targets.spatial_fence
              beforeEncoderStages:MTLStageFragment];
          wait_for_render_or_blit_writes (e);
          record_gpu_pass_start (frame, e, pass);
          [e setRenderPipelineState:pso];
          const MTLGPUAddress uniforms = frame.arena[frame.slot].write (q);
          bind_address (frame, MTLRenderStageVertex, MOPPE_BUF_FRAME, uniforms);
          bind_address (
            frame, MTLRenderStageFragment, MOPPE_BUF_FRAME, uniforms);
          bind_texture (frame, MTLRenderStageFragment, MOPPE_TEX_SCENE, src);
          use_arguments (
            e, frame, MTLRenderStageVertex | MTLRenderStageFragment);
          [e drawPrimitives:MTLPrimitiveTypeTriangle
                vertexStart:0
                vertexCount:3];
          [e endEncoding];
        };
        encode ();
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
        q.tint.w = presentation_exposure * frame.params.exposure_bias;
        q.params.x = 1;
        quad_pass (GpuPass::Bloom,
                   @"Bloom bright pass",
                   pipelines.bloom_bright,
                   frame.current_scene,
                   targets.bloom_a,
                   q);

        q.params.z = 1.0f / (float)targets.bloom_a.width;
        q.params.w = 0;
        quad_pass (GpuPass::Bloom,
                   @"Bloom horizontal blur",
                   pipelines.bloom_blur,
                   targets.bloom_a,
                   targets.bloom_b,
                   q);
        q.params.z = 0;
        q.params.w = 1.0f / (float)targets.bloom_a.height;
        quad_pass (GpuPass::Bloom,
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
        quad_pass (GpuPass::Exposure,
                   @"Exposure probe",
                   pipelines.probe,
                   frame.current_scene,
                   targets.probe_tex,
                   q);

        id<MTL4ComputeCommandEncoder> blit =
          [frame.command_buffer computeCommandEncoder];
        wait_for_render_writes (blit);
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

      id<MTLTexture> present_scene = frame.current_scene;
      const bool interpolate = targets.frame_interpolator &&
                               targets.interpolator_color[frame.slot] &&
                               targets.interpolator_composite[frame.slot] &&
                               targets.interpolator_output[frame.slot];
      id<MTLTexture> rendered_color = interpolate
                                        ? targets.interpolator_color[frame.slot]
                                        : frame.drawable.texture;

      MTL4RenderPassDescriptor* rp = [[MTL4RenderPassDescriptor alloc] init];
      rp.colorAttachments[0].texture = rendered_color;
      rp.colorAttachments[0].loadAction = MTLLoadActionDontCare;
      rp.colorAttachments[0].storeAction = MTLStoreActionStore;

      id<MTL4RenderCommandEncoder> enc =
        [frame.command_buffer renderCommandEncoderWithDescriptor:rp];
      enc.label = @"Present and HUD";
      if (targets.spatial_fence)
        [enc waitForFence:targets.spatial_fence
          beforeEncoderStages:MTLStageFragment];
      wait_for_render_or_blit_writes (enc);
      record_gpu_pass_start (frame, enc, GpuPass::Present);

      // Scene quad with the tonemap, grade, bloom, and lens flare.
      if (pipelines.present) {
        MoppeQuadUniforms q;
        std::memset (&q, 0, sizeof (q));
        q.tint.x = q.tint.y = q.tint.z = 1;
        q.tint.w = presentation_exposure * frame.params.exposure_bias;
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
          const float cx = vp.element (0) * sp[0] + vp.element (4) * sp[1] +
                           vp.element (8) * sp[2] + vp.element (12);
          const float cy = vp.element (1) * sp[0] + vp.element (5) * sp[1] +
                           vp.element (9) * sp[2] + vp.element (13);
          const float cw = vp.element (3) * sp[0] + vp.element (7) * sp[1] +
                           vp.element (11) * sp[2] + vp.element (15);
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
                        (edge > 1.0f ? 1.0f : edge) * presentation_exposure *
                        frame.params.exposure_bias;
              q.sun.w = (float)targets.width / (float)targets.height;
            }
          }
        }

        [enc setRenderPipelineState:pipelines.present];
        const MTLGPUAddress uniforms = frame.arena[frame.slot].write (q);
        bind_address (frame, MTLRenderStageVertex, MOPPE_BUF_FRAME, uniforms);
        bind_address (frame, MTLRenderStageFragment, MOPPE_BUF_FRAME, uniforms);
        bind_texture (
          frame, MTLRenderStageFragment, MOPPE_TEX_SCENE, present_scene);
        MetalTexture* inert_bloom =
          static_cast<MetalTexture*> (pipelines.black.get ());
        bind_texture (frame,
                      MTLRenderStageFragment,
                      MOPPE_TEX_BLOOM,
                      bloom_ok ? targets.bloom_a : inert_bloom->texture);
        use_arguments (
          enc, frame, MTLRenderStageVertex | MTLRenderStageFragment);
        [enc drawPrimitives:MTLPrimitiveTypeTriangle
                vertexStart:0
                vertexCount:3];
      }

      const auto draw_hud_overlay = [&] (id<MTL4RenderCommandEncoder> target) {
        if (list.empty () || !pipelines.hud)
          return;
        MoppeHudUniforms hu;
        std::memset (&hu, 0, sizeof (hu));
        hu.proj = m4 (
          Mat4::hud_ortho ((float)frame.width_pts, (float)frame.height_pts));
#if !TARGET_OS_IPHONE
        hu.params.x = 1;
#endif
        const MTLGPUAddress uniforms = frame.arena[frame.slot].write (hu);
        bind_address (frame, MTLRenderStageVertex, MOPPE_BUF_FRAME, uniforms);
        bind_address (frame, MTLRenderStageFragment, MOPPE_BUF_FRAME, uniforms);
        MetalDrawListEncoder::play (
          { device, target, pipelines, terrain, frame },
          list.vertices (),
          list.runs (),
          true);
      };

      if (!interpolate) {
        // HUD overlay in point coordinates.
        draw_hud_overlay (enc);
        record_gpu_pass_end (frame, enc);
        [enc endEncoding];
        return;
      }

      record_gpu_pass_end (frame, enc);
      if (targets.spatial_fence)
        [enc updateFence:targets.spatial_fence
          afterEncoderStages:MTLStageFragment];
      [enc endEncoding];

      // Keep both versions Apple exposes in its composited-UI integration:
      // a HUD-free, tone-mapped color input and the exact current frame with
      // Moppe's HUD on top. The interpolator decomposites the latter so UI
      // stays crisp rather than being motion-warped with the world.
      id<MTL4ComputeCommandEncoder> composite_copy =
        [frame.command_buffer computeCommandEncoder];
      composite_copy.label = @"Frame interpolation color copy";
      if (targets.spatial_fence)
        [composite_copy waitForFence:targets.spatial_fence
                 beforeEncoderStages:MTLStageBlit];
      wait_for_render_writes (composite_copy);
      [composite_copy
        copyFromTexture:rendered_color
              toTexture:targets.interpolator_composite[frame.slot]];
      if (targets.spatial_fence)
        [composite_copy updateFence:targets.spatial_fence
                 afterEncoderStages:MTLStageBlit];
      [composite_copy endEncoding];

      if (!list.empty () && pipelines.hud) {
        MTL4RenderPassDescriptor* composite_pass =
          [[MTL4RenderPassDescriptor alloc] init];
        composite_pass.colorAttachments[0].texture =
          targets.interpolator_composite[frame.slot];
        composite_pass.colorAttachments[0].loadAction = MTLLoadActionLoad;
        composite_pass.colorAttachments[0].storeAction = MTLStoreActionStore;
        id<MTL4RenderCommandEncoder> composite = [frame.command_buffer
          renderCommandEncoderWithDescriptor:composite_pass];
        composite.label = @"Frame interpolation UI composite";
        if (targets.spatial_fence)
          [composite waitForFence:targets.spatial_fence
              beforeEncoderStages:MTLStageFragment];
        wait_for_render_or_blit_writes (composite);
        draw_hud_overlay (composite);
        if (targets.spatial_fence)
          [composite updateFence:targets.spatial_fence
              afterEncoderStages:MTLStageFragment];
        [composite endEncoding];
      }

      id<MTL4FXFrameInterpolator> interpolator = targets.frame_interpolator;
      const int previous_slot =
        (frame.slot + FRAMES_IN_FLIGHT - 1) % FRAMES_IN_FLIGHT;
      const bool have_history = targets.interpolation_history_valid;
      interpolator.colorTexture = rendered_color;
      interpolator.prevColorTexture =
        have_history ? targets.interpolator_color[previous_slot]
                     : rendered_color;
      interpolator.depthTexture = targets.msaa_depth;
      interpolator.motionTexture = targets.motion;
      interpolator.uiTexture = targets.interpolator_composite[frame.slot];
      interpolator.uiTextureComposited = YES;
      interpolator.outputTexture = targets.interpolator_output[frame.slot];
      // The attached temporal scaler defines how its input-pixel motion maps
      // to reconstructed color. MetalFX consumes Moppe's pixel vectors as-is.
      interpolator.motionVectorScaleX = 1.0f;
      interpolator.motionVectorScaleY = 1.0f;
      interpolator.deltaTime = frame.interpolation_delta_time;
      const float projection_a = frame.params.proj.element (10);
      const float projection_b = frame.params.proj.element (14);
      interpolator.nearPlane =
        projection_b / std::max (projection_a + 1.0f, 1e-6f);
      interpolator.farPlane =
        projection_a > 1e-6f ? projection_b / projection_a : 9000.0f;
      interpolator.fieldOfView =
        2.0f * std::atan (1.0f / frame.params.proj.element (5)) *
        (180.0f / 3.14159265358979323846f);
      interpolator.aspectRatio =
        frame.params.proj.element (0) != 0.0f
          ? frame.params.proj.element (5) / frame.params.proj.element (0)
          : (float)targets.output_width / targets.output_height;
      interpolator.jitterOffsetX = frame.jitter_x;
      interpolator.jitterOffsetY = frame.jitter_y;
      interpolator.depthReversed = YES;
      interpolator.shouldResetHistory = !have_history;
      [frame.command_buffer pushDebugGroup:@"MetalFX frame interpolation"];
#if !TARGET_OS_TV
      record_gpu_pass_start (frame, GpuPass::Interpolation);
#endif
      [interpolator encodeToCommandBuffer:frame.command_buffer];
#if !TARGET_OS_TV
      record_gpu_pass_end (frame);
#endif
      [frame.command_buffer popDebugGroup];
      targets.interpolation_history_valid = true;

      // The first encoded call only primes history. Afterwards this callback
      // presents the midpoint; the next display-link callback presents the
      // stored real frame without advancing simulation.
      id<MTLTexture> display_source =
        have_history ? targets.interpolator_output[frame.slot]
                     : targets.interpolator_composite[frame.slot];
      MTL4RenderPassDescriptor* display_pass =
        [[MTL4RenderPassDescriptor alloc] init];
      display_pass.colorAttachments[0].texture = frame.drawable.texture;
      display_pass.colorAttachments[0].loadAction = MTLLoadActionDontCare;
      display_pass.colorAttachments[0].storeAction = MTLStoreActionStore;
      id<MTL4RenderCommandEncoder> display =
        [frame.command_buffer renderCommandEncoderWithDescriptor:display_pass];
      display.label = @"Present interpolated frame";
      if (targets.spatial_fence)
        [display waitForFence:targets.spatial_fence
          beforeEncoderStages:MTLStageFragment];
      wait_for_render_or_blit_writes (display);
      record_gpu_pass_start (frame, display, GpuPass::Present);
      [display setRenderPipelineState:pipelines.copy];
      MoppeQuadUniforms copy {};
      copy.tint.x = copy.tint.y = copy.tint.z = copy.tint.w = 1.0f;
      copy.params.x = 1.0f;
      const MTLGPUAddress copy_uniforms = frame.arena[frame.slot].write (copy);
      bind_address (
        frame, MTLRenderStageVertex, MOPPE_BUF_FRAME, copy_uniforms);
      bind_address (
        frame, MTLRenderStageFragment, MOPPE_BUF_FRAME, copy_uniforms);
      bind_texture (
        frame, MTLRenderStageFragment, MOPPE_TEX_SCENE, display_source);
      use_arguments (
        display, frame, MTLRenderStageVertex | MTLRenderStageFragment);
      [display drawPrimitives:MTLPrimitiveTypeTriangle
                  vertexStart:0
                  vertexCount:3];
      record_gpu_pass_end (frame, display);
      [display endEncoding];
    }

    void MetalRenderer::draw_hud (const DrawList& list) {
      reconstruct_scene ();
      MetalHudPass::draw (
        { m_device, m_pipelines, m_terrain_resources, m_targets, m_frame },
        list);
    }

    bool MetalRenderer::present_rendered_frame (id<CAMetalDrawable> drawable) {
      if (!drawable || !frame_interpolation_active () || !m_frame.sequence)
        return false;
      const int slot =
        static_cast<int> ((m_frame.sequence - 1) % FRAMES_IN_FLIGHT);
      id<MTLTexture> source = m_targets.interpolator_composite[slot];
      if (!source || source.width != drawable.texture.width ||
          source.height != drawable.texture.height)
        return false;

      const uint64_t sequence = ++m_present_sequence;
      constexpr uint64_t wait_timeout_ms = 1000;
      if (sequence > FRAMES_IN_FLIGHT &&
          ![m_present_completion_event
            waitUntilSignaledValue:sequence - FRAMES_IN_FLIGHT
                         timeoutMS:wait_timeout_ms])
        throw std::runtime_error (
          "Timed out waiting for a Metal 4 presentation frame");
      const int present_slot =
        static_cast<int> ((sequence - 1) % FRAMES_IN_FLIGHT);
      id<MTL4CommandAllocator> allocator = m_present_allocators[present_slot];
      [allocator reset];
      id<MTL4CommandBuffer> command = [m_device newCommandBuffer];
      command.label = @"Moppe rendered-frame presentation";
      [command beginCommandBufferWithAllocator:allocator];
      MTL4RenderPassDescriptor* pass = [[MTL4RenderPassDescriptor alloc] init];
      pass.colorAttachments[0].texture = drawable.texture;
      pass.colorAttachments[0].loadAction = MTLLoadActionDontCare;
      pass.colorAttachments[0].storeAction = MTLStoreActionStore;
      id<MTL4RenderCommandEncoder> encoder =
        [command renderCommandEncoderWithDescriptor:pass];
      encoder.label = @"Present rendered frame";
      [encoder setRenderPipelineState:m_pipelines.copy];
      MoppeQuadUniforms copy {};
      copy.tint.x = copy.tint.y = copy.tint.z = copy.tint.w = 1.0f;
      copy.params.x = 1.0f;
      const MTLGPUAddress copy_uniforms = m_frame.arena[slot].write (copy);
      bind_address (
        m_frame, MTLRenderStageVertex, MOPPE_BUF_FRAME, copy_uniforms);
      bind_address (
        m_frame, MTLRenderStageFragment, MOPPE_BUF_FRAME, copy_uniforms);
      bind_texture (m_frame, MTLRenderStageFragment, MOPPE_TEX_SCENE, source);
      use_arguments (
        encoder, m_frame, MTLRenderStageVertex | MTLRenderStageFragment);
      [encoder drawPrimitives:MTLPrimitiveTypeTriangle
                  vertexStart:0
                  vertexCount:3];
      [encoder endEncoding];
      [command endCommandBuffer];
      [m_queue waitForDrawable:drawable];
      const id<MTL4CommandBuffer> commands[] = { command };
      [m_queue commit:commands count:1];
      [m_queue signalEvent:m_present_completion_event value:sequence];
      [m_queue signalDrawable:drawable];
      [drawable present];
      return true;
    }

    void MetalRenderer::request_screenshot (const std::string& path) {
      m_frame.screenshot_path = path;
#if !TARGET_OS_IPHONE
      build_reflection_geometry ();
#endif
    }

    void MetalRenderer::end_frame () {
      MOPPE_PROFILE_ZONE ("MetalRenderer::end_frame");
      {
        MOPPE_PROFILE_ZONE ("MetalRenderer::finish_scene_encoding");
        end_scene_encoder (); // in case nothing was drawn
      }
#if !TARGET_OS_IPHONE
      // Automated captures can request their path before begin_frame, when no
      // drawable exists. The forcing camera is final here, so this is the
      // authoritative point at which to materialize its bounded proxy.
      build_reflection_geometry ();
#endif

#if !TARGET_OS_IPHONE
      id<MTLBuffer> capture = nil;
      id<MTLBuffer> reflection_diagnostic = nil;
      id<MTLBuffer> water_reflection_diagnostic = nil;
      std::size_t capture_row_bytes = 0;
      std::size_t reflection_row_bytes = 0;
      std::size_t water_reflection_row_bytes = 0;
      int capture_width = 0;
      int capture_height = 0;
      int water_reflection_width = 0;
      int water_reflection_height = 0;
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
        make_resident (capture);
        id<MTL4ComputeCommandEncoder> blit =
          [m_frame.command_buffer computeCommandEncoder];
        wait_for_render_writes (blit);
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

      draw_water_reflection_signal (water_reflection_diagnostic,
                                    water_reflection_row_bytes,
                                    water_reflection_width,
                                    water_reflection_height);

      if (!capture_path.empty () && !m_reflection_geometry_written &&
          m_pipelines.reflection_geometry &&
          m_terrain_resources.reflection_structure && m_frame.drawable) {
        const int width = static_cast<int> (m_frame.drawable.texture.width);
        const int height = static_cast<int> (m_frame.drawable.texture.height);
        reflection_row_bytes = (static_cast<std::size_t> (width) * 8 + 255) &
                               ~static_cast<std::size_t> (255);
        reflection_diagnostic =
          [m_device newBufferWithLength:reflection_row_bytes * height
                                options:MTLResourceStorageModeShared];
        reflection_diagnostic.label = @"Moppe reflection geometry diagnostic";
        make_resident (reflection_diagnostic);

        MoppeReflectionGeometryUniforms uniforms {};
        uniforms.camera = f4 (m_frame.params.camera_pos);
        uniforms.camera.w = 8192.0f;
        const Mat4& view = m_frame.params.view;
        uniforms.camera_right = {
          view.element (0), view.element (4), view.element (8), 0.0f
        };
        uniforms.camera_up = {
          view.element (1), view.element (5), view.element (9), 0.0f
        };
        uniforms.camera_back = {
          view.element (2), view.element (6), view.element (10), 0.0f
        };
        uniforms.projection = { m_frame.params.proj.element (0),
                                m_frame.params.proj.element (5),
                                static_cast<float> (width),
                                static_cast<float> (height) };
        uniforms.output.x = static_cast<float> (reflection_row_bytes / 8);
        const MTLGPUAddress uniform_address =
          m_frame.arena[m_frame.slot].write (uniforms);
        [m_reflection_arguments
            setResource:m_terrain_resources.reflection_structure.gpuResourceID
          atBufferIndex:MOPPE_BUF_REFLECTION_AS];
        [m_reflection_arguments setAddress:uniform_address
                                   atIndex:MOPPE_BUF_REFLECTION_UNIFORMS];
        [m_reflection_arguments setAddress:reflection_diagnostic.gpuAddress
                                   atIndex:MOPPE_BUF_REFLECTION_OUTPUT];
        [m_reflection_arguments
          setAddress:m_terrain_resources.reflection_vertices.gpuAddress
             atIndex:MOPPE_BUF_REFLECTION_VERTICES];
        id<MTL4ComputeCommandEncoder> diagnostic =
          [m_frame.command_buffer computeCommandEncoder];
        [diagnostic setComputePipelineState:m_pipelines.reflection_geometry];
        [diagnostic setArgumentTable:m_reflection_arguments];
        [diagnostic dispatchThreads:MTLSizeMake (width, height, 1)
              threadsPerThreadgroup:MTLSizeMake (8, 8, 1)];
        [diagnostic endEncoding];
      }
#endif

      const int timestamp_count = m_frame.timestamp_count;
      const std::vector<GpuPass> sample_intervals = m_frame.sample_intervals;
      const double timestamp_ms_per_tick = m_frame.timestamp_ms_per_tick;
      id<MTL4CounterHeap> timestamp_heap =
        m_frame.timestamp_heaps[m_frame.slot];
      std::shared_ptr<FrameTiming> timing =
        m_frame.profile_this_frame ? m_frame_timing : nullptr;
      std::shared_ptr<BenchmarkOutput> benchmark =
        m_frame.params.benchmark_measured ? m_benchmark : nullptr;
      const uint32_t benchmark_mask = m_frame.params.benchmark_mask;
      const uint32_t benchmark_partition_mask =
        m_frame.params.benchmark_partition_mask;
      const uint32_t benchmark_epoch = m_frame.params.benchmark_epoch;
      const uint32_t benchmark_frame = m_frame.params.benchmark_frame;
      MTL4CommitOptions* commit_options = [[MTL4CommitOptions alloc] init];
      [commit_options addFeedbackHandler:^(id<MTL4CommitFeedback> feedback) {
        if (feedback.error)
          std::cerr << "moppe: Metal 4 submission failed: "
                    << feedback.error.localizedDescription.UTF8String
                    << std::endl;
        std::array<double, GPU_PASS_COUNT> pass_ms {};
        NSData* timestamp_results =
          timestamp_heap && timestamp_count > 1
            ? [timestamp_heap
                resolveCounterRange:NSMakeRange (0, timestamp_count)]
            : nil;
        if (timestamp_results) {
          const auto* samples = static_cast<const MTLCounterResultTimestamp*> (
            timestamp_results.bytes);
          if (sample_intervals.size () + 1 == (size_t)timestamp_count) {
            for (std::size_t i = 0; i < sample_intervals.size (); ++i) {
              const uint64_t a = samples[i].timestamp;
              const uint64_t b = samples[i + 1].timestamp;
              if (a != MTLCounterErrorValue && b != MTLCounterErrorValue &&
                  b >= a)
                pass_ms[static_cast<int> (sample_intervals[i])] +=
                  (b - a) * timestamp_ms_per_tick;
            }
          }
        }
        if (benchmark && feedback.GPUEndTime >= feedback.GPUStartTime) {
          const BenchmarkSample sample { benchmark_mask,
                                         benchmark_partition_mask,
                                         benchmark_epoch,
                                         benchmark_frame,
                                         1000.0 * (feedback.GPUEndTime -
                                                   feedback.GPUStartTime),
                                         pass_ms };
          {
            std::lock_guard<std::mutex> lock (benchmark->mutex);
            benchmark->samples.push_back (sample);
          }
          ++benchmark->completed;
        }
        if (timing && feedback.GPUEndTime >= feedback.GPUStartTime) {
          const double gpu_ms =
            1000.0 * (feedback.GPUEndTime - feedback.GPUStartTime);
          std::lock_guard<std::mutex> lock (timing->mutex);
          if (timing->interval_start == 0)
            timing->interval_start = feedback.GPUEndTime;
          timing->gpu_total_ms += gpu_ms;
          timing->gpu_min_ms = std::min (timing->gpu_min_ms, gpu_ms);
          timing->gpu_max_ms = std::max (timing->gpu_max_ms, gpu_ms);
          for (int i = 0; i < GPU_PASS_COUNT; ++i)
            timing->pass_total_ms[i] += pass_ms[i];
          ++timing->frames;
          const double elapsed = feedback.GPUEndTime - timing->interval_start;
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
            timing->interval_start = feedback.GPUEndTime;
            timing->gpu_total_ms = 0;
            timing->gpu_min_ms = std::numeric_limits<double>::max ();
            timing->gpu_max_ms = 0;
            timing->pass_total_ms.fill (0);
            timing->frames = 0;
          }
        }
      }];
      {
        MOPPE_PROFILE_ZONE ("MetalRenderer::commit_command_buffer");
        [m_frame.command_buffer endCommandBuffer];
        [m_queue waitForDrawable:m_frame.drawable];
        const id<MTL4CommandBuffer> commands[] = { m_frame.command_buffer };
        [m_queue commit:commands count:1 options:commit_options];
        [m_queue signalEvent:m_frame.completion_event value:m_frame.sequence];
#if !TARGET_OS_IPHONE
        if (capture_path.empty ()) {
#endif
          [m_queue signalDrawable:m_frame.drawable];
          [m_frame.drawable present];
#if !TARGET_OS_IPHONE
        }
#endif
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
        [m_frame.completion_event waitUntilSignaledValue:m_frame.sequence
                                               timeoutMS:5000];
        [[MTLCaptureManager sharedCaptureManager] stopCapture];
        m_frame.capture_active = false;
        std::cerr << "moppe: wrote Metal capture " << m_frame.capture_path
                  << std::endl;
      }
#endif

#if !TARGET_OS_IPHONE
      if (capture) {
        [m_frame.completion_event waitUntilSignaledValue:m_frame.sequence
                                               timeoutMS:5000];
        if (timestamp_heap && timestamp_count > 1) {
          NSData* results = [timestamp_heap
            resolveCounterRange:NSMakeRange (0, timestamp_count)];
          const auto* samples =
            static_cast<const MTLCounterResultTimestamp*> (results.bytes);
          if (results &&
              sample_intervals.size () + 1 == (std::size_t)timestamp_count) {
            double reflection_ms = 0.0;
            for (std::size_t i = 0; i < sample_intervals.size (); ++i) {
              if (sample_intervals[i] != GpuPass::Reflection)
                continue;
              const uint64_t a = samples[i].timestamp;
              const uint64_t b = samples[i + 1].timestamp;
              if (a != MTLCounterErrorValue && b != MTLCounterErrorValue &&
                  b >= a)
                reflection_ms += (b - a) / 1000000.0;
            }
            if (reflection_ms > 0.0)
              m_water_reflection_gpu_ms = reflection_ms;
          }
        }
        if (water_reflection_diagnostic)
          benchmark_water_reflection_query ();
        if (!write_capture_png (capture_path,
                                capture_width,
                                capture_height,
                                capture_row_bytes,
                                capture.contents))
          std::cerr << "moppe: failed to write screenshot " << capture_path
                    << std::endl;
        else
          std::cout << "moppe: wrote screenshot " << capture_path << std::endl;
        [m_residency removeAllocation:capture];
        [m_residency commit];
      }
      if (reflection_diagnostic) {
        if (!write_capture_png (m_reflection_geometry_path,
                                capture_width,
                                capture_height,
                                reflection_row_bytes,
                                reflection_diagnostic.contents))
          std::cerr << "moppe: failed to write reflection geometry "
                    << m_reflection_geometry_path << std::endl;
        else {
          write_reflection_geometry_report ();
          m_reflection_geometry_written = true;
          std::cout << "moppe: wrote reflection geometry "
                    << m_reflection_geometry_path << std::endl;
        }
        [m_residency removeAllocation:reflection_diagnostic];
        [m_residency commit];
      }
      if (water_reflection_diagnostic) {
        if (!write_capture_png (m_water_reflection_path,
                                water_reflection_width,
                                water_reflection_height,
                                water_reflection_row_bytes,
                                water_reflection_diagnostic.contents))
          std::cerr << "moppe: failed to write water reflection signal "
                    << m_water_reflection_path << std::endl;
        else {
          write_water_reflection_report (water_reflection_diagnostic.contents,
                                         water_reflection_row_bytes,
                                         water_reflection_width,
                                         water_reflection_height);
          m_water_reflection_written = true;
          std::cout << "moppe: wrote water reflection signal "
                    << m_water_reflection_path << std::endl;
        }
        [m_residency removeAllocation:water_reflection_diagnostic];
        [m_residency commit];
      }
      m_frame.screenshot_path.clear ();
#endif

      m_previous_view_proj = m_current_view_proj;
      m_previous_sky_view_proj = m_current_sky_view_proj;
      m_previous_time = m_frame.params.time;
      m_camera_history_valid = true;
      m_previous_models = std::move (m_current_models);
      m_previous_lists = std::move (m_current_lists);
      m_current_models.clear ();
      m_current_lists.clear ();
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
      if (m_frame.sequence &&
          ![m_frame.completion_event waitUntilSignaledValue:m_frame.sequence
                                                  timeoutMS:5000])
        throw std::runtime_error (
          "Timed out waiting to reset Metal temporal state");
      m_targets.prev_valid = false;
      m_targets.temporal_history_valid = false;
      m_targets.interpolation_history_valid = false;
      m_targets.exposure = 1.0f;
      m_camera_history_valid = false;
      m_previous_models.clear ();
      m_current_models.clear ();
      m_previous_lists.clear ();
      m_current_lists.clear ();
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
      output << "epoch,mask,partition_mask,logical_frame,gpu_ms,partition";
      if (m_benchmark->pass_timing)
        for (int i = 0; i < GPU_PASS_COUNT; ++i)
          output << ',' << GPU_PASS_NAMES[i] << "_ms";
      for (std::size_t bit = 0; bit < m_benchmark->feature_names.size (); ++bit)
        output << ",feature_" << bit << '_' << m_benchmark->feature_names[bit];
      for (std::size_t bit = 0; bit < m_benchmark->block_names.size (); ++bit)
        output << ",block_" << bit << '_' << m_benchmark->block_names[bit];
      output << '\n';
      for (const BenchmarkSample& sample : samples) {
        output << sample.epoch << ',' << sample.mask << ','
               << sample.partition_mask << ',' << sample.frame << ','
               << sample.gpu_ms << ',' << m_benchmark->partition;
        if (m_benchmark->pass_timing)
          for (double pass_ms : sample.pass_ms)
            output << ',' << pass_ms;
        for (std::size_t bit = 0; bit < m_benchmark->feature_names.size ();
             ++bit)
          output << ',' << ((sample.mask & (1u << bit)) ? 1 : 0);
        for (std::size_t bit = 0; bit < m_benchmark->block_names.size (); ++bit)
          output << ',' << ((sample.partition_mask & (1u << bit)) ? 1 : 0);
        output << '\n';
      }
      std::cerr << "moppe: wrote " << samples.size ()
                << " graphics benchmark samples to " << m_benchmark->path
                << std::endl;
    }

    // ------------------------------------------------------------------

    Renderer* create_metal_renderer (void* metal_layer,
                                     const std::string& lib_path,
                                     int msaa_samples,
                                     bool request_frame_interpolation) {
      return new MetalRenderer ((__bridge CAMetalLayer*)metal_layer,
                                lib_path,
                                msaa_samples,
                                request_frame_interpolation);
    }

    void set_metal_drawable (Renderer& renderer, void* drawable) {
      MetalRenderer& metal = static_cast<MetalRenderer&> (renderer);
      metal.set_next_drawable ((__bridge id<CAMetalDrawable>)drawable);
    }

    void set_metal_edr_headroom (Renderer& renderer, float headroom) {
      MetalRenderer& metal = static_cast<MetalRenderer&> (renderer);
      metal.set_edr_headroom (headroom);
    }

    bool metal_frame_interpolation_supported (Renderer& renderer) {
      MetalRenderer& metal = static_cast<MetalRenderer&> (renderer);
      return metal.frame_interpolation_supported ();
    }

    bool metal_frame_interpolation_active (Renderer& renderer) {
      MetalRenderer& metal = static_cast<MetalRenderer&> (renderer);
      return metal.frame_interpolation_active ();
    }

    void set_metal_frame_interpolation_enabled (Renderer& renderer,
                                                bool enabled) {
      MetalRenderer& metal = static_cast<MetalRenderer&> (renderer);
      metal.set_frame_interpolation_enabled (enabled);
    }

    void set_metal_frame_delta_time (Renderer& renderer, float delta_time) {
      MetalRenderer& metal = static_cast<MetalRenderer&> (renderer);
      metal.set_frame_delta_time (delta_time);
    }

    bool present_metal_rendered_frame (Renderer& renderer, void* drawable) {
      MetalRenderer& metal = static_cast<MetalRenderer&> (renderer);
      return metal.present_rendered_frame (
        (__bridge id<CAMetalDrawable>)drawable);
    }
  }
}
