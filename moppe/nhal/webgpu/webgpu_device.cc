// NHAL on WebGPU, in the browser: Dawn's webgpu.h as Emscripten binds it to
// the page's GPUDevice.
//
// luv-shaderc lowers each program to WGSL with its buffers in bind group 0,
// its textures in group 1 (storage textures from binding 16), and its
// samplers in group 2, each at its family's binding number, so a program's
// reflection becomes those three layouts. Bind groups are immutable; the
// device keeps the ones it has made, keyed by what they bind: the standard
// samplers once per program, a group per set of textures, and a group per
// set of buffers. Uniform blocks, and read-only storage buffers as far as
// the limits allow, bind at dynamic offsets, so the frame arena's slices
// share a group whatever their place in it. Slots a draw leaves unbound
// read zeroes, as Vulkan's null descriptors do.
//
// The frame arena is ordinary memory, written to its GPU buffer when the
// frame is submitted: WebGPU has no memory the CPU and the GPU share.
// Buffers bind only from multiples of 256 bytes.
//
// The browser presents the canvas when the animation frame's callback
// returns, and nothing here may wait for the GPU: wait_idle returns at
// once, and captures and pass timings arrive in a later frame. Frames are
// timed by the document's timeline, which the browser steps once per
// displayed frame.
#include <moppe/environment.hh>
#include <moppe/nhal/table.hh>
#include <moppe/nhal/webgpu/webgpu_device.hh>

#include <algorithm>
#include <array>
#include <cmath>
#include <cstring>
#include <iostream>
#include <memory>
#include <stdexcept>
#include <string>
#include <unordered_map>
#include <utility>
#include <vector>

#include <emscripten.h>
#include <webgpu/webgpu.h>

namespace moppe::nhal {
  namespace {
    constexpr std::uint64_t arena_capacity = 16u << 20;
    constexpr std::uint32_t max_bindings = 16;
    constexpr std::uint32_t standard_samplers = 4;
    constexpr std::uint64_t offset_alignment = 256;
    constexpr std::uint32_t max_timed_passes = 32;
    // Transient addresses are the arena's offsets above a tag, so a slice
    // finds its offset again when it is bound.
    constexpr std::uint64_t arena_tag = 1ull << 48;
    // Bind groups unused for this many frames are released.
    constexpr std::uint64_t group_lifetime = 240;

    // The time of the animation frame being built, in milliseconds.
    EM_JS (double, frame_milliseconds, (), {
      const time = document.timeline.currentTime;
      return time === null ? performance.now () : time;
    });

    EM_JS (int, canvas_prefers_bgra, (), {
      return navigator.gpu.getPreferredCanvasFormat () === 'bgra8unorm';
    });

    WGPUStringView view (const char* text) {
      return { text, WGPU_STRLEN };
    }

    WGPUStringView view (std::string_view text) {
      return { text.data (), text.size () };
    }

    std::string string (WGPUStringView text) {
      if (!text.data)
        return {};
      return text.length == WGPU_STRLEN
               ? std::string (text.data)
               : std::string (text.data, text.length);
    }

    WGPUTextureFormat wgpu_format (Format format) {
      switch (format) {
      case Format::undefined: return WGPUTextureFormat_Undefined;
      case Format::rgba8_unorm: return WGPUTextureFormat_RGBA8Unorm;
      case Format::rgba8_unorm_srgb: return WGPUTextureFormat_RGBA8UnormSrgb;
      case Format::bgra8_unorm: return WGPUTextureFormat_BGRA8Unorm;
      case Format::bgra8_unorm_srgb: return WGPUTextureFormat_BGRA8UnormSrgb;
      case Format::rgb10a2_unorm: return WGPUTextureFormat_RGB10A2Unorm;
      case Format::rgba16_float: return WGPUTextureFormat_RGBA16Float;
      case Format::rg16_float: return WGPUTextureFormat_RG16Float;
      // WebGPU's 16-bit normalized formats are optional and, in Dawn, not
      // filtered; such textures are kept as half floats (write_texture).
      case Format::rg16_snorm: return WGPUTextureFormat_RG16Float;
      case Format::r16_float: return WGPUTextureFormat_R16Float;
      case Format::r8_unorm: return WGPUTextureFormat_R8Unorm;
      case Format::r32_float: return WGPUTextureFormat_R32Float;
      case Format::rg32_float: return WGPUTextureFormat_RG32Float;
      case Format::rgba32_float: return WGPUTextureFormat_RGBA32Float;
      case Format::r32_uint: return WGPUTextureFormat_R32Uint;
      case Format::d32_float: return WGPUTextureFormat_Depth32Float;
      }
      return WGPUTextureFormat_Undefined;
    }

    WGPUCompareFunction wgpu_compare (CompareOp op) {
      switch (op) {
      case CompareOp::never: return WGPUCompareFunction_Never;
      case CompareOp::less: return WGPUCompareFunction_Less;
      case CompareOp::equal: return WGPUCompareFunction_Equal;
      case CompareOp::less_equal: return WGPUCompareFunction_LessEqual;
      case CompareOp::greater: return WGPUCompareFunction_Greater;
      case CompareOp::not_equal: return WGPUCompareFunction_NotEqual;
      case CompareOp::greater_equal: return WGPUCompareFunction_GreaterEqual;
      case CompareOp::always: return WGPUCompareFunction_Always;
      }
      return WGPUCompareFunction_Always;
    }

    WGPUShaderStage wgpu_stages (StageMask stages) {
      WGPUShaderStage visible = WGPUShaderStage_None;
      if (stages & stage_vertex)
        visible |= WGPUShaderStage_Vertex;
      if (stages & stage_fragment)
        visible |= WGPUShaderStage_Fragment;
      if (stages & stage_compute)
        visible |= WGPUShaderStage_Compute;
      return visible;
    }

    WGPUIndexFormat wgpu_index (IndexType type) {
      return type == IndexType::uint16 ? WGPUIndexFormat_Uint16
                                       : WGPUIndexFormat_Uint32;
    }

    // A signed normalized 16-bit value as a half float.
    std::uint16_t half_from_snorm (std::int16_t value) {
      const float f = std::max (float (value) / 32767.0f, -1.0f);
      std::uint32_t bits;
      std::memcpy (&bits, &f, sizeof bits);
      const std::uint32_t sign = (bits >> 16) & 0x8000u;
      const std::int32_t exponent = std::int32_t ((bits >> 23) & 0xff) - 112;
      const std::uint32_t mantissa = bits & 0x7fffffu;
      if (exponent <= 0) {
        // Too small for a normal half: a subnormal, or zero.
        if (exponent < -10)
          return std::uint16_t (sign);
        const std::uint32_t whole = mantissa | 0x800000u;
        return std::uint16_t (sign | (whole >> (14 - exponent)));
      }
      return std::uint16_t (sign | (std::uint32_t (exponent) << 10)
                            | (mantissa >> 13));
    }

    struct BufferRow {
      WGPUBuffer buffer = nullptr;
      std::uint64_t size = 0;
      // Names the buffer in bind group keys; never reused.
      std::uint32_t id = 0;
      // Upload memory: the CPU's copy, written to the buffer each frame.
      std::unique_ptr<std::byte[]> contents;
    };

    struct TextureRow {
      WGPUTexture texture = nullptr;
      WGPUTextureView view = nullptr;
      TextureDesc desc {};
      std::uint32_t id = 0;
      bool warned = false;
    };

    struct BufferSlot {
      std::uint32_t binding = 0;
      ResourceKind kind = ResourceKind::uniform_block;
      std::uint64_t size = 0;
      bool dynamic = false;
    };

    struct TextureSlot {
      std::uint32_t binding = 0;
      ResourceKind kind = ResourceKind::texture_2d;
    };

    // A program's three bind group layouts, made once from its reflection.
    struct Layout {
      std::uint32_t id = 0;
      std::array<WGPUBindGroupLayout, 3> groups {};
      WGPUPipelineLayout pipeline = nullptr;
      WGPUBindGroup samplers = nullptr;
      // In binding order, which is the order of their dynamic offsets.
      std::vector<BufferSlot> buffers;
      std::vector<TextureSlot> textures;
    };

    struct PipelineRow {
      // A strip's pipeline states the index format whose all-ones value
      // restarts it, so a strip has a pipeline per index type it is drawn
      // with, made on first use; other pipelines have only the first.
      std::array<WGPURenderPipeline, 2> render {};
      WGPUComputePipeline compute = nullptr;
      const Layout* layout = nullptr;
      const char* name = "";
      bool is_compute = false;
      // What a strip's pipelines are made from.
      bool strip = false;
      WGPUShaderModule vertex = nullptr, fragment = nullptr;
      RenderPipelineDesc desc {};
    };

    // What a destroyed object leaves for the device to release once the
    // frame that may still encode it has been submitted.
    struct Retired {
      WGPUBuffer buffer = nullptr;
      WGPUTexture texture = nullptr;
      WGPUTextureView view = nullptr;
      std::array<WGPURenderPipeline, 2> render {};
      WGPUComputePipeline compute = nullptr;
      WGPUShaderModule vertex = nullptr, fragment = nullptr;
    };

    // A binding: a device buffer or an arena slice, as buffer and range.
    struct BoundBuffer {
      WGPUBuffer buffer = nullptr;
      std::uint32_t id = 0;
      std::uint64_t offset = 0;
      std::uint64_t size = 0;
    };

    // What a bind group binds: its layout, and each entry's identity.
    struct GroupKey {
      std::uint32_t layout = 0;
      std::uint32_t group = 0;
      std::uint32_t count = 0;
      std::array<std::uint64_t, 3 * max_bindings> words {};

      void add (std::uint64_t word) { words[count++] = word; }

      bool operator== (const GroupKey& other) const {
        return layout == other.layout && group == other.group
               && count == other.count
               && std::equal (words.begin (), words.begin () + count,
                              other.words.begin ());
      }
    };

    struct GroupKeyHash {
      std::size_t operator() (const GroupKey& key) const {
        std::uint64_t hash = 14695981039346656037ull;
        auto mix = [&] (std::uint64_t word) {
          hash = (hash ^ word) * 1099511628211ull;
        };
        mix (key.layout);
        mix (key.group);
        for (std::uint32_t i = 0; i < key.count; ++i)
          mix (key.words[i]);
        return std::size_t (hash ^ (hash >> 32));
      }
    };

    struct CachedGroup {
      WGPUBindGroup group = nullptr;
      std::uint64_t used = 0;
    };

    struct PendingCapture {
      WGPUBuffer readback = nullptr;
      Capture capture;
      // The readback's rows are padded to 256 bytes.
      std::uint32_t stride = 0;
      std::function<void (const Capture&)> done;
      bool requested = false, ready = false, failed = false;
    };

    class WebGpuDevice final : public Device {
    public:
      WebGpuDevice (const char* selector, std::uint32_t width,
                    std::uint32_t height)
        : m_arena_memory (arena_capacity) {
        m_device = emscripten_webgpu_get_device ();
        if (!m_device)
          throw std::runtime_error ("NHAL: the page has no WebGPU device");
        m_queue = wgpuDeviceGetQueue (m_device);
        WGPULimits limits = WGPU_LIMITS_INIT;
        wgpuDeviceGetLimits (m_device, &limits);
        m_limits = limits;
        WGPUAdapterInfo adapter = WGPU_ADAPTER_INFO_INIT;
        if (wgpuDeviceGetAdapterInfo (m_device, &adapter)
            == WGPUStatus_Success) {
          m_adapter = string (adapter.description);
          if (m_adapter.empty ())
            m_adapter = string (adapter.vendor) + " "
                        + string (adapter.architecture);
          wgpuAdapterInfoFreeMembers (adapter);
        }
        if (!wgpuDeviceHasFeature (m_device,
                                   WGPUFeatureName_Float32Filterable))
          throw std::runtime_error (
            "NHAL: this browser's WebGPU cannot filter 32-bit float "
            "textures (float32-filterable), which the terrain needs");
        // MOPPE_NHAL_TIMINGS asks for each pass's GPU time; unasked, the
        // device spares the queries and their readback.
        const char* timings = moppe::environment ("MOPPE_NHAL_TIMINGS");
        m_timestamps =
          timings && *timings && *timings != '0'
          && wgpuDeviceHasFeature (m_device, WGPUFeatureName_TimestampQuery);

        m_instance = wgpuCreateInstance (nullptr);
        WGPUEmscriptenSurfaceSourceCanvasHTMLSelector canvas =
          WGPU_EMSCRIPTEN_SURFACE_SOURCE_CANVAS_HTML_SELECTOR_INIT;
        canvas.selector = view (selector);
        WGPUSurfaceDescriptor surface = WGPU_SURFACE_DESCRIPTOR_INIT;
        surface.nextInChain = &canvas.chain;
        m_surface = wgpuInstanceCreateSurface (m_instance, &surface);
        if (!m_surface)
          throw std::runtime_error (std::string ("NHAL: no canvas at ")
                                    + selector);
        m_surface_format = canvas_prefers_bgra () ? Format::bgra8_unorm
                                                  : Format::rgba8_unorm;
        m_width = width;
        m_height = height;
        configure_surface ();

        create_samplers ();
        create_arena ();
        create_nulls ();
        if (m_timestamps)
          create_timing ();
        TextureRow back;
        back.desc = { width, height, m_surface_format, usage_render_target,
                      1, "drawable" };
        m_backbuffer = m_textures.insert<Texture> (back);
      }

      ~WebGpuDevice () override {
        // The page keeps the device; this releases what was made on it.
        for (auto& [key, cached] : m_groups)
          wgpuBindGroupRelease (cached.group);
        m_pipelines.each ([&] (PipelineRow& p) { release (retired (p)); });
        m_buffers.each ([&] (BufferRow& b) {
          if (b.buffer)
            wgpuBufferRelease (b.buffer);
        });
        m_textures.each ([&] (TextureRow& t) {
          if (t.view)
            wgpuTextureViewRelease (t.view);
          if (t.texture)
            wgpuTextureRelease (t.texture);
        });
        m_retired.collect (~0ull, [&] (Retired& r) { release (r); });
        for (auto& [program, layout] : m_layouts) {
          wgpuBindGroupRelease (layout->samplers);
          wgpuPipelineLayoutRelease (layout->pipeline);
          for (WGPUBindGroupLayout group : layout->groups)
            wgpuBindGroupLayoutRelease (group);
        }
        for (WGPUSampler sampler : m_samplers)
          wgpuSamplerRelease (sampler);
        wgpuBufferRelease (m_arena_buffer);
        wgpuBufferRelease (m_null_buffer);
        if (m_query_set) {
          wgpuQuerySetRelease (m_query_set);
          wgpuBufferRelease (m_query_resolve);
          wgpuBufferRelease (m_query_readback);
        }
        // Captures still being mapped keep their records: the browser may
        // yet call back with them.
        for (auto& capture : m_captures)
          (void)capture.release ();
        wgpuSurfaceRelease (m_surface);
        wgpuInstanceRelease (m_instance);
        wgpuQueueRelease (m_queue);
        wgpuDeviceRelease (m_device);
      }

      DeviceInfo info () const override {
        return { "WebGPU", m_adapter, 1, true };
      }

      // -- resources --------------------------------------------------

      Buffer create_buffer (const BufferDesc& desc,
                            std::span<const std::byte> initial) override {
        BufferRow row;
        // Buffer sizes and the ranges bound from them are whole words.
        row.size = (std::max<std::uint64_t> (desc.size, 16) + 3) & ~3ull;
        if (row.size > m_limits.maxBufferSize)
          throw std::runtime_error (
            std::string ("NHAL: buffer ") + (desc.label ? desc.label : "")
            + " of " + std::to_string (row.size)
            + " bytes exceeds WebGPU's limit");
        row.id = ++m_next_id;
        WGPUBufferDescriptor info = WGPU_BUFFER_DESCRIPTOR_INIT;
        info.label = view (desc.label);
        info.size = row.size;
        info.usage = WGPUBufferUsage_Storage | WGPUBufferUsage_Uniform
                     | WGPUBufferUsage_Index | WGPUBufferUsage_Indirect
                     | WGPUBufferUsage_CopySrc | WGPUBufferUsage_CopyDst;
        row.buffer = wgpuDeviceCreateBuffer (m_device, &info);
        if (desc.memory == Memory::upload) {
          row.contents = std::make_unique<std::byte[]> (row.size);
          std::memset (row.contents.get (), 0, row.size);
          if (!initial.empty ())
            std::memcpy (row.contents.get (), initial.data (),
                         std::min<std::uint64_t> (initial.size (), row.size));
          ++m_upload_buffers;
        } else if (!initial.empty ()) {
          write_buffer (row.buffer, initial);
        }
        return m_buffers.insert<Buffer> (std::move (row));
      }

      void* contents (Buffer buffer) override {
        return m_buffers[buffer].contents.get ();
      }

      Texture create_texture (const TextureDesc& desc) override {
        TextureRow row;
        row.desc = desc;
        row.id = ++m_next_id;
        WGPUTextureDescriptor info = WGPU_TEXTURE_DESCRIPTOR_INIT;
        info.label = view (desc.label);
        info.size = { desc.width, desc.height, 1 };
        info.format = wgpu_format (desc.format);
        info.sampleCount = std::max (desc.samples, 1u);
        // Multisampled textures are attachments only.
        if (info.sampleCount == 1)
          info.usage = WGPUTextureUsage_CopySrc | WGPUTextureUsage_CopyDst;
        if (desc.usage & usage_sampled)
          info.usage |= WGPUTextureUsage_TextureBinding;
        if (desc.usage & (usage_render_target | usage_depth))
          info.usage |= WGPUTextureUsage_RenderAttachment;
        if (desc.usage & usage_storage)
          info.usage |= WGPUTextureUsage_StorageBinding;
        row.texture = wgpuDeviceCreateTexture (m_device, &info);
        row.view = wgpuTextureCreateView (row.texture, nullptr);
        return m_textures.insert<Texture> (row);
      }

      void write_texture (Texture handle, std::span<const std::byte> pixels,
                          std::uint32_t row_bytes) override {
        const TextureRow& t = m_textures[handle];
        const std::uint32_t texel = bytes_per_pixel (t.desc.format);
        if (!row_bytes)
          row_bytes = t.desc.width * texel;
        std::vector<std::uint16_t> halves;
        if (t.desc.format == Format::rg16_snorm) {
          // Each row's signed pairs as half floats, tightly packed.
          halves.resize (std::size_t (t.desc.width) * t.desc.height * 2);
          for (std::uint32_t y = 0; y < t.desc.height; ++y) {
            const std::byte* from = pixels.data ()
                                    + std::size_t (y) * row_bytes;
            std::uint16_t* to = halves.data ()
                                + std::size_t (y) * t.desc.width * 2;
            for (std::uint32_t i = 0; i < t.desc.width * 2; ++i) {
              std::int16_t value;
              std::memcpy (&value, from + i * 2, sizeof value);
              to[i] = half_from_snorm (value);
            }
          }
          pixels = std::as_bytes (std::span (halves));
          row_bytes = t.desc.width * texel;
        }
        WGPUTexelCopyTextureInfo target = WGPU_TEXEL_COPY_TEXTURE_INFO_INIT;
        target.texture = t.texture;
        const WGPUTexelCopyBufferLayout layout { 0, row_bytes,
                                                 t.desc.height };
        const WGPUExtent3D extent { t.desc.width, t.desc.height, 1 };
        wgpuQueueWriteTexture (
          m_queue, &target, pixels.data (),
          std::min<std::size_t> (pixels.size (),
                                 std::size_t (row_bytes) * t.desc.height),
          &layout, &extent);
      }

      Pipeline create_render_pipeline (const RenderPipelineDesc& desc)
        override {
        PipelineRow row;
        row.layout = &program_layout (*desc.program);
        row.name = desc.program->name;
        row.strip = desc.topology == Topology::triangle_strip;
        row.vertex = shader_module (desc.vertex, desc.program, "vertex");
        if (!desc.fragment.wgsl.empty ())
          row.fragment = shader_module (desc.fragment, desc.program,
                                        "fragment");
        row.desc = desc;
        row.desc.vertex = {};
        row.desc.fragment = {};
        row.desc.label = desc.label ? desc.label : desc.program->name;
        if (!row.strip) {
          row.render[0] = render_pipeline (row, WGPUIndexFormat_Undefined);
          wgpuShaderModuleRelease (row.vertex);
          if (row.fragment)
            wgpuShaderModuleRelease (row.fragment);
          row.vertex = row.fragment = nullptr;
        }
        return m_pipelines.insert<Pipeline> (row);
      }

      Pipeline create_compute_pipeline (const ComputePipelineDesc& desc)
        override {
        PipelineRow row;
        row.layout = &program_layout (*desc.program);
        row.name = desc.program->name;
        row.is_compute = true;
        WGPUShaderModule module = shader_module (desc.compute, desc.program,
                                                 "compute");
        WGPUComputePipelineDescriptor info =
          WGPU_COMPUTE_PIPELINE_DESCRIPTOR_INIT;
        info.label = view (desc.label ? desc.label : desc.program->name);
        info.layout = row.layout->pipeline;
        info.compute.module = module;
        info.compute.entryPoint = view (desc.program->compute_entry);
        row.compute = wgpuDeviceCreateComputePipeline (m_device, &info);
        wgpuShaderModuleRelease (module);
        return m_pipelines.insert<Pipeline> (row);
      }

      void destroy (Buffer buffer) override {
        BufferRow row = m_buffers.take (buffer);
        if (row.contents)
          --m_upload_buffers;
        Retired r;
        r.buffer = row.buffer;
        m_retired.retire (m_serial, r);
      }

      void destroy (Texture texture) override {
        TextureRow row = m_textures.take (texture);
        Retired r;
        r.texture = row.texture;
        r.view = row.view;
        m_retired.retire (m_serial, r);
      }

      void destroy (Pipeline pipeline) override {
        PipelineRow row = m_pipelines.take (pipeline);
        m_retired.retire (m_serial, retired (row));
      }

      // -- the drawable -----------------------------------------------

      Format surface_format () const override { return m_surface_format; }
      std::uint32_t surface_width () const override { return m_width; }
      std::uint32_t surface_height () const override { return m_height; }

      void resize_surface (std::uint32_t width, std::uint32_t height)
        override {
        if (width == m_width && height == m_height)
          return;
        m_width = width;
        m_height = height;
        configure_surface ();
      }

      // -- a frame ----------------------------------------------------

      bool begin_frame () override {
        deliver_captures ();
        deliver_timings ();
        if (!m_width || !m_height)
          return false;
        WGPUSurfaceTexture drawable = WGPU_SURFACE_TEXTURE_INIT;
        wgpuSurfaceGetCurrentTexture (m_surface, &drawable);
        if (drawable.status
              != WGPUSurfaceGetCurrentTextureStatus_SuccessOptimal
            && drawable.status
                 != WGPUSurfaceGetCurrentTextureStatus_SuccessSuboptimal) {
          if (drawable.texture)
            wgpuTextureRelease (drawable.texture);
          return false;
        }
        ++m_serial;
        note_frame_time ();
        TextureRow& back = m_textures[m_backbuffer];
        back.texture = drawable.texture;
        back.view = wgpuTextureCreateView (back.texture, nullptr);
        back.desc.width = wgpuTextureGetWidth (back.texture);
        back.desc.height = wgpuTextureGetHeight (back.texture);
        back.id = ++m_next_id;
        m_encoder = wgpuDeviceCreateCommandEncoder (m_device, nullptr);
        m_arena = { m_arena_memory.data (), arena_tag, arena_capacity, 0 };
        m_timing_frame = m_timestamps && !m_timing_pending;
        m_frame_labels.clear ();
        reset_bindings ();
        return true;
      }

      Texture backbuffer () override { return m_backbuffer; }

      Transient allocate (std::uint64_t size, std::uint64_t alignment)
        override {
        alignment = std::max (alignment, offset_alignment);
        if (!m_arena.fits (size, alignment))
          throw std::runtime_error ("NHAL: frame arena exhausted");
        return m_arena.allocate (size, alignment);
      }

      void begin_render_pass (const RenderPassDesc& desc) override {
        std::array<WGPURenderPassColorAttachment, 8> colors;
        for (std::uint32_t i = 0; i < desc.color_count; ++i) {
          const ColorAttachment& c = desc.colors[i];
          WGPURenderPassColorAttachment& a = colors[i];
          a = WGPU_RENDER_PASS_COLOR_ATTACHMENT_INIT;
          a.view = m_textures[c.texture].view;
          // WebGPU has no "don't care": a clear costs the least.
          a.loadOp = c.load == Load::load ? WGPULoadOp_Load
                                          : WGPULoadOp_Clear;
          a.storeOp = c.store == Store::store ? WGPUStoreOp_Store
                                              : WGPUStoreOp_Discard;
          a.clearValue = { c.clear[0], c.clear[1], c.clear[2], c.clear[3] };
          if (c.resolve)
            a.resolveTarget = m_textures[c.resolve].view;
        }
        WGPURenderPassDepthStencilAttachment depth =
          WGPU_RENDER_PASS_DEPTH_STENCIL_ATTACHMENT_INIT;
        if (desc.depth.texture) {
          depth.view = m_textures[desc.depth.texture].view;
          depth.depthLoadOp = desc.depth.load == Load::load
                                ? WGPULoadOp_Load
                                : WGPULoadOp_Clear;
          depth.depthStoreOp = desc.depth.store == Store::store
                                 ? WGPUStoreOp_Store
                                 : WGPUStoreOp_Discard;
          depth.depthClearValue = desc.depth.clear;
        }
        WGPURenderPassDescriptor info = WGPU_RENDER_PASS_DESCRIPTOR_INIT;
        info.label = view (desc.label);
        info.colorAttachmentCount = desc.color_count;
        info.colorAttachments = colors.data ();
        if (desc.depth.texture)
          info.depthStencilAttachment = &depth;
        WGPUPassTimestampWrites timing = WGPU_PASS_TIMESTAMP_WRITES_INIT;
        if (time_pass (timing, desc.label ? desc.label : "render"))
          info.timestampWrites = &timing;
        m_render = wgpuCommandEncoderBeginRenderPass (m_encoder, &info);
        m_pass = desc;
        m_pass_width = m_pass_height = 0;
        const Texture sized = desc.color_count ? desc.colors[0].texture
                                               : desc.depth.texture;
        if (sized) {
          m_pass_width = m_textures[sized].desc.width;
          m_pass_height = m_textures[sized].desc.height;
        }
        reset_bindings ();
      }

      void end_render_pass () override {
        wgpuRenderPassEncoderEnd (m_render);
        wgpuRenderPassEncoderRelease (m_render);
        m_render = nullptr;
        m_pass = {};
        reset_bindings ();
      }

      void begin_compute_pass (const char* label) override {
        WGPUComputePassDescriptor info = WGPU_COMPUTE_PASS_DESCRIPTOR_INIT;
        info.label = view (label);
        WGPUPassTimestampWrites timing = WGPU_PASS_TIMESTAMP_WRITES_INIT;
        if (time_pass (timing, label ? label : "compute"))
          info.timestampWrites = &timing;
        m_compute = wgpuCommandEncoderBeginComputePass (m_encoder, &info);
        reset_bindings ();
      }

      void end_compute_pass () override {
        wgpuComputePassEncoderEnd (m_compute);
        wgpuComputePassEncoderRelease (m_compute);
        m_compute = nullptr;
        reset_bindings ();
      }

      void copy_to_buffer (Buffer target, std::uint64_t offset,
                           const Transient& source) override {
        // The arena's bytes reach its buffer before the frame's commands
        // run, so this copies what the slice holds by then.
        wgpuCommandEncoderCopyBufferToBuffer (
          m_encoder, m_arena_buffer, source.gpu_address - arena_tag,
          m_buffers[target].buffer, offset, (source.size + 3) & ~3ull);
      }

      void set_pipeline (Pipeline handle) override { m_pipeline = handle; }

      void set_buffer (std::uint32_t binding, Buffer buffer,
                       std::uint64_t offset) override {
        const BufferRow& row = m_buffers[buffer];
        m_bound_buffers.at (binding) = { row.buffer, row.id, offset,
                                         row.size - offset };
        m_buffers_dirty = true;
      }

      void set_buffer (std::uint32_t binding, const Transient& slice)
        override {
        // A slice binds a power of two of the arena from its start, so
        // slices of like size share a bind group.
        const std::uint64_t offset = slice.gpu_address - arena_tag;
        std::uint64_t reach = offset_alignment;
        while (reach < slice.size)
          reach <<= 1;
        m_bound_buffers.at (binding) = {
          m_arena_buffer, m_arena_id, offset,
          std::min (reach, arena_capacity - offset) };
        m_buffers_dirty = true;
      }

      void set_texture (std::uint32_t binding, Texture texture) override {
        m_bound_textures.at (binding) = texture;
        m_textures_dirty = true;
      }

      void set_storage_texture (std::uint32_t binding, Texture texture)
        override {
        m_bound_storage.at (binding) = texture;
        m_textures_dirty = true;
      }

      void set_viewport (float x, float y, float width, float height)
        override {
        if (!m_render)
          return;
        wgpuRenderPassEncoderSetViewport (m_render, x, y, width, height, 0,
                                          1);
        const auto left = std::uint32_t (std::max (x, 0.0f));
        const auto top = std::uint32_t (std::max (y, 0.0f));
        wgpuRenderPassEncoderSetScissorRect (
          m_render, std::min (left, m_pass_width),
          std::min (top, m_pass_height),
          std::min (std::uint32_t (width),
                    m_pass_width - std::min (left, m_pass_width)),
          std::min (std::uint32_t (height),
                    m_pass_height - std::min (top, m_pass_height)));
      }

      void draw (std::uint32_t vertex_count, std::uint32_t instance_count,
                 std::uint32_t first_vertex, std::uint32_t first_instance)
        override {
        bind_arguments (nullptr);
        wgpuRenderPassEncoderDraw (m_render, vertex_count, instance_count,
                                   first_vertex, first_instance);
      }

      void draw_indexed (Buffer indices, IndexType type,
                         std::uint32_t index_count,
                         std::uint32_t instance_count,
                         std::uint32_t first_index, std::int32_t base_vertex,
                         std::uint32_t first_instance) override {
        bind_arguments (&type);
        set_index_buffer (indices, type);
        wgpuRenderPassEncoderDrawIndexed (m_render, index_count,
                                          instance_count, first_index,
                                          base_vertex, first_instance);
      }

      void draw_indirect (Buffer arguments, std::uint64_t offset) override {
        bind_arguments (nullptr);
        wgpuRenderPassEncoderDrawIndirect (
          m_render, m_buffers[arguments].buffer, offset);
      }

      void draw_indexed_indirect (Buffer indices, IndexType type,
                                  Buffer arguments, std::uint64_t offset)
        override {
        bind_arguments (&type);
        set_index_buffer (indices, type);
        wgpuRenderPassEncoderDrawIndexedIndirect (
          m_render, m_buffers[arguments].buffer, offset);
      }

      void dispatch (std::uint32_t x, std::uint32_t y, std::uint32_t z)
        override {
        bind_arguments (nullptr);
        wgpuComputePassEncoderDispatchWorkgroups (m_compute, x, y, z);
      }

      void dispatch_indirect (Buffer arguments, std::uint64_t offset)
        override {
        bind_arguments (nullptr);
        wgpuComputePassEncoderDispatchWorkgroupsIndirect (
          m_compute, m_buffers[arguments].buffer, offset);
      }

      void capture_frame (std::function<void (const Capture&)> done)
        override {
        m_capture_request = std::move (done);
      }

      std::span<const PassTiming> pass_timings () const override {
        return m_pass_timings;
      }

      // The animation frame's own time: the browser steps the document's
      // timeline once per frame it displays.
      FrameTiming next_frame_timing () const override {
        return { frame_milliseconds () * 1e-3, m_refresh, true };
      }

      void end_frame () override {
        if (m_capture_request)
          encode_capture ();
        const bool timed = m_timing_frame && !m_frame_labels.empty ();
        if (timed)
          encode_timings ();
        WGPUCommandBuffer commands = wgpuCommandEncoderFinish (m_encoder,
                                                               nullptr);
        wgpuCommandEncoderRelease (m_encoder);
        m_encoder = nullptr;
        // Queue writes precede the commands submitted after them.
        if (m_arena.used)
          wgpuQueueWriteBuffer (m_queue, m_arena_buffer, 0,
                                m_arena_memory.data (),
                                (m_arena.used + 3) & ~3ull);
        if (m_upload_buffers)
          m_buffers.each ([&] (BufferRow& b) {
            if (b.contents)
              wgpuQueueWriteBuffer (m_queue, b.buffer, 0, b.contents.get (),
                                    b.size);
          });
        wgpuQueueSubmit (m_queue, 1, &commands);
        wgpuCommandBufferRelease (commands);

        for (auto& capture : m_captures)
          if (!capture->requested) {
            capture->requested = true;
            WGPUBufferMapCallbackInfo mapped =
              WGPU_BUFFER_MAP_CALLBACK_INFO_INIT;
            mapped.mode = WGPUCallbackMode_AllowSpontaneous;
            mapped.callback = &WebGpuDevice::capture_mapped;
            mapped.userdata1 = capture.get ();
            wgpuBufferMapAsync (capture->readback, WGPUMapMode_Read, 0,
                                wgpuBufferGetSize (capture->readback),
                                mapped);
          }
        if (timed) {
          m_timing_pending = true;
          m_timing_ready = false;
          m_pending_labels = m_frame_labels;
          WGPUBufferMapCallbackInfo mapped =
            WGPU_BUFFER_MAP_CALLBACK_INFO_INIT;
          mapped.mode = WGPUCallbackMode_AllowSpontaneous;
          mapped.callback = &WebGpuDevice::timings_mapped;
          mapped.userdata1 = this;
          wgpuBufferMapAsync (m_query_readback, WGPUMapMode_Read, 0,
                              m_pending_labels.size () * 16, mapped);
        }

        // The browser presents the drawable when this callback returns.
        TextureRow& back = m_textures[m_backbuffer];
        wgpuTextureViewRelease (back.view);
        wgpuTextureRelease (back.texture);
        back.view = nullptr;
        back.texture = nullptr;
        m_retired.collect (m_serial, [&] (Retired& r) { release (r); });
        if (m_serial % 64 == 0)
          sweep_groups ();
      }

      void wait_idle () override {}

    private:
      // -- setup ------------------------------------------------------

      void configure_surface () {
        if (!m_width || !m_height)
          return;
        WGPUSurfaceConfiguration info = WGPU_SURFACE_CONFIGURATION_INIT;
        info.device = m_device;
        info.format = wgpu_format (m_surface_format);
        // Captures copy the drawable out.
        info.usage = WGPUTextureUsage_RenderAttachment
                     | WGPUTextureUsage_CopySrc;
        info.width = m_width;
        info.height = m_height;
        info.alphaMode = WGPUCompositeAlphaMode_Opaque;
        info.presentMode = WGPUPresentMode_Fifo;
        wgpuSurfaceConfigure (m_surface, &info);
      }

      void create_samplers () {
        for (std::uint32_t i = 0; i < standard_samplers; ++i) {
          WGPUSamplerDescriptor s = WGPU_SAMPLER_DESCRIPTOR_INIT;
          const bool nearest = i == 2;
          s.magFilter = s.minFilter = nearest ? WGPUFilterMode_Nearest
                                              : WGPUFilterMode_Linear;
          s.mipmapFilter = nearest ? WGPUMipmapFilterMode_Nearest
                                   : WGPUMipmapFilterMode_Linear;
          s.addressModeU = s.addressModeV = s.addressModeW =
            i == 1 ? WGPUAddressMode_Repeat : WGPUAddressMode_ClampToEdge;
          if (i == 3)
            s.compare = WGPUCompareFunction_LessEqual;
          m_samplers[i] = wgpuDeviceCreateSampler (m_device, &s);
        }
      }

      void create_arena () {
        WGPUBufferDescriptor info = WGPU_BUFFER_DESCRIPTOR_INIT;
        info.label = view ("frame arena");
        info.size = arena_capacity;
        info.usage = WGPUBufferUsage_Storage | WGPUBufferUsage_Uniform
                     | WGPUBufferUsage_CopySrc | WGPUBufferUsage_CopyDst;
        m_arena_buffer = wgpuDeviceCreateBuffer (m_device, &info);
        m_arena_id = ++m_next_id;
      }

      // What unbound slots read: zeroes.
      void create_nulls () {
        WGPUBufferDescriptor info = WGPU_BUFFER_DESCRIPTOR_INIT;
        info.label = view ("unbound");
        info.size = 64 << 10;
        info.usage = WGPUBufferUsage_Storage | WGPUBufferUsage_Uniform;
        m_null_buffer = wgpuDeviceCreateBuffer (m_device, &info);
        m_null_buffer_id = ++m_next_id;
        m_null_float = create_texture (
          { 1, 1, Format::rgba8_unorm, usage_sampled, 1, "unbound" });
        m_null_depth = create_texture (
          { 1, 1, Format::d32_float, usage_sampled, 1, "unbound depth" });
        m_null_uint = create_texture (
          { 1, 1, Format::r32_uint, usage_sampled, 1, "unbound uint" });
      }

      void create_timing () {
        WGPUQuerySetDescriptor queries = WGPU_QUERY_SET_DESCRIPTOR_INIT;
        queries.type = WGPUQueryType_Timestamp;
        queries.count = 2 * max_timed_passes;
        m_query_set = wgpuDeviceCreateQuerySet (m_device, &queries);
        WGPUBufferDescriptor info = WGPU_BUFFER_DESCRIPTOR_INIT;
        info.size = 16 * max_timed_passes;
        info.usage = WGPUBufferUsage_QueryResolve | WGPUBufferUsage_CopySrc;
        m_query_resolve = wgpuDeviceCreateBuffer (m_device, &info);
        info.usage = WGPUBufferUsage_MapRead | WGPUBufferUsage_CopyDst;
        m_query_readback = wgpuDeviceCreateBuffer (m_device, &info);
      }

      void write_buffer (WGPUBuffer buffer,
                         std::span<const std::byte> bytes) {
        if (bytes.size () % 4 == 0) {
          wgpuQueueWriteBuffer (m_queue, buffer, 0, bytes.data (),
                                bytes.size ());
          return;
        }
        std::vector<std::byte> padded ((bytes.size () + 3) & ~std::size_t (3));
        std::memcpy (padded.data (), bytes.data (), bytes.size ());
        wgpuQueueWriteBuffer (m_queue, buffer, 0, padded.data (),
                              padded.size ());
      }

      // -- programs and pipelines -------------------------------------

      const Layout& program_layout (const Program& program) {
        std::unique_ptr<Layout>& cached = m_layouts[&program];
        if (cached)
          return *cached;
        auto layout = std::make_unique<Layout> ();
        layout->id = ++m_next_id;
        std::vector<WGPUBindGroupLayoutEntry> buffers, textures, samplers;
        std::vector<WGPUBindGroupEntry> sampler_entries;
        for (const Resource& r : program.resources) {
          WGPUBindGroupLayoutEntry e = WGPU_BIND_GROUP_LAYOUT_ENTRY_INIT;
          e.binding = r.binding;
          e.visibility = wgpu_stages (r.stages);
          if (is_buffer (r.kind)) {
            layout->buffers.push_back ({ r.binding, r.kind, r.size, false });
            buffers.push_back (e);
          } else if (is_sampler (r.kind)) {
            // The nearest sampler is the one that may read depth and other
            // unfilterable textures.
            e.sampler.type = r.kind == ResourceKind::comparison_sampler
                               ? WGPUSamplerBindingType_Comparison
                             : r.binding == 2
                               ? WGPUSamplerBindingType_NonFiltering
                               : WGPUSamplerBindingType_Filtering;
            samplers.push_back (e);
            WGPUBindGroupEntry bound = WGPU_BIND_GROUP_ENTRY_INIT;
            bound.binding = r.binding;
            bound.sampler = m_samplers.at (r.binding);
            sampler_entries.push_back (bound);
          } else if (r.kind == ResourceKind::texture_2d
                     || r.kind == ResourceKind::depth_texture_2d
                     || r.kind == ResourceKind::uint_texture_2d) {
            e.texture.sampleType =
              r.kind == ResourceKind::depth_texture_2d
                ? WGPUTextureSampleType_Depth
              : r.kind == ResourceKind::uint_texture_2d
                ? WGPUTextureSampleType_Uint
                : WGPUTextureSampleType_Float;
            e.texture.viewDimension = WGPUTextureViewDimension_2D;
            layout->textures.push_back ({ r.binding, r.kind });
            textures.push_back (e);
          } else {
            // The reflection does not yet say a storage texture's format,
            // and no program binds an array, a cube, or a volume.
            throw std::runtime_error (
              std::string ("NHAL: ") + program.name + " binds " + r.name
              + ", a kind of texture the WebGPU device does not bind yet");
          }
        }
        auto by_binding = [] (const auto& a, const auto& b) {
          return a.binding < b.binding;
        };
        std::sort (layout->buffers.begin (), layout->buffers.end (),
                   by_binding);
        std::sort (buffers.begin (), buffers.end (), by_binding);
        std::sort (layout->textures.begin (), layout->textures.end (),
                   by_binding);

        // Dynamic offsets go to uniform blocks, then to read-only storage
        // buffers, which the arena's slices bind; what shaders write is a
        // device buffer bound whole.
        std::uint32_t uniforms =
          m_limits.maxDynamicUniformBuffersPerPipelineLayout;
        std::uint32_t storages =
          m_limits.maxDynamicStorageBuffersPerPipelineLayout;
        for (std::size_t i = 0; i < buffers.size (); ++i) {
          BufferSlot& slot = layout->buffers[i];
          WGPUBufferBindingLayout& b = buffers[i].buffer;
          switch (slot.kind) {
          case ResourceKind::uniform_block:
            b.type = WGPUBufferBindingType_Uniform;
            b.minBindingSize = slot.size;
            if (uniforms) {
              slot.dynamic = true;
              --uniforms;
            }
            break;
          case ResourceKind::storage_buffer:
            b.type = WGPUBufferBindingType_ReadOnlyStorage;
            if (storages) {
              slot.dynamic = true;
              --storages;
            }
            break;
          default: b.type = WGPUBufferBindingType_Storage; break;
          }
          b.hasDynamicOffset = slot.dynamic;
        }

        auto group = [&] (const std::vector<WGPUBindGroupLayoutEntry>& all) {
          WGPUBindGroupLayoutDescriptor info =
            WGPU_BIND_GROUP_LAYOUT_DESCRIPTOR_INIT;
          info.label = view (program.name);
          info.entryCount = all.size ();
          info.entries = all.data ();
          return wgpuDeviceCreateBindGroupLayout (m_device, &info);
        };
        layout->groups = { group (buffers), group (textures),
                           group (samplers) };
        WGPUPipelineLayoutDescriptor pipeline =
          WGPU_PIPELINE_LAYOUT_DESCRIPTOR_INIT;
        pipeline.label = view (program.name);
        pipeline.bindGroupLayoutCount = layout->groups.size ();
        pipeline.bindGroupLayouts = layout->groups.data ();
        layout->pipeline = wgpuDeviceCreatePipelineLayout (m_device,
                                                           &pipeline);
        WGPUBindGroupDescriptor bound = WGPU_BIND_GROUP_DESCRIPTOR_INIT;
        bound.label = view (program.name);
        bound.layout = layout->groups[2];
        bound.entryCount = sampler_entries.size ();
        bound.entries = sampler_entries.data ();
        layout->samplers = wgpuDeviceCreateBindGroup (m_device, &bound);
        cached = std::move (layout);
        return *cached;
      }

      WGPUShaderModule shader_module (const StageCode& code,
                                      const Program* program,
                                      const char* stage) {
        if (code.wgsl.empty ())
          throw std::runtime_error (std::string ("NHAL: ") + program->name
                                    + " has no WGSL " + stage + " stage");
        WGPUShaderSourceWGSL source = WGPU_SHADER_SOURCE_WGSL_INIT;
        source.code = view (code.wgsl);
        const std::string label = std::string (program->name) + " " + stage;
        WGPUShaderModuleDescriptor info = WGPU_SHADER_MODULE_DESCRIPTOR_INIT;
        info.nextInChain = &source.chain;
        info.label = view (std::string_view (label));
        return wgpuDeviceCreateShaderModule (m_device, &info);
      }

      WGPURenderPipeline render_pipeline (const PipelineRow& row,
                                          WGPUIndexFormat strip_index) {
        const RenderPipelineDesc& desc = row.desc;
        WGPURenderPipelineDescriptor info =
          WGPU_RENDER_PIPELINE_DESCRIPTOR_INIT;
        info.label = view (desc.label);
        info.layout = row.layout->pipeline;
        info.vertex.module = row.vertex;
        info.vertex.entryPoint = view (desc.program->vertex_entry);
        info.primitive.topology =
          desc.topology == Topology::triangle_strip
            ? WGPUPrimitiveTopology_TriangleStrip
          : desc.topology == Topology::line_list
            ? WGPUPrimitiveTopology_LineList
            : WGPUPrimitiveTopology_TriangleList;
        info.primitive.stripIndexFormat = strip_index;
        info.primitive.frontFace = desc.front_counter_clockwise
                                     ? WGPUFrontFace_CCW
                                     : WGPUFrontFace_CW;
        info.primitive.cullMode = desc.cull == Cull::back
                                    ? WGPUCullMode_Back
                                  : desc.cull == Cull::front
                                    ? WGPUCullMode_Front
                                    : WGPUCullMode_None;
        WGPUDepthStencilState depth = WGPU_DEPTH_STENCIL_STATE_INIT;
        if (desc.depth_format != Format::undefined) {
          depth.format = wgpu_format (desc.depth_format);
          depth.depthWriteEnabled = desc.depth_write ? WGPUOptionalBool_True
                                                     : WGPUOptionalBool_False;
          depth.depthCompare = wgpu_compare (desc.depth_compare);
          if (desc.topology != Topology::line_list) {
            depth.depthBias = std::int32_t (desc.depth_bias);
            depth.depthBiasSlopeScale = desc.slope_scaled_depth_bias;
          }
          info.depthStencil = &depth;
        }
        info.multisample.count = std::max (desc.samples, 1u);

        std::array<WGPUBlendState, 8> blends;
        std::array<WGPUColorTargetState, 8> targets;
        for (std::uint32_t i = 0; i < desc.color_count; ++i) {
          WGPUColorTargetState& target = targets[i];
          target = WGPU_COLOR_TARGET_STATE_INIT;
          target.format = wgpu_format (desc.color_formats[i]);
          if (desc.blend[i] == Blend::none)
            continue;
          const bool additive = desc.blend[i] == Blend::additive;
          WGPUBlendState& b = blends[i];
          b.color.operation = b.alpha.operation = WGPUBlendOperation_Add;
          b.color.srcFactor = desc.blend[i] == Blend::premultiplied
                                ? WGPUBlendFactor_One
                                : WGPUBlendFactor_SrcAlpha;
          b.color.dstFactor = b.alpha.dstFactor =
            additive ? WGPUBlendFactor_One
                     : WGPUBlendFactor_OneMinusSrcAlpha;
          b.alpha.srcFactor = WGPUBlendFactor_One;
          target.blend = &b;
        }
        WGPUFragmentState fragment = WGPU_FRAGMENT_STATE_INIT;
        if (row.fragment) {
          fragment.module = row.fragment;
          fragment.entryPoint = view (desc.program->fragment_entry);
          fragment.targetCount = desc.color_count;
          fragment.targets = targets.data ();
          info.fragment = &fragment;
        }
        return wgpuDeviceCreateRenderPipeline (m_device, &info);
      }

      // The pipeline that draws with `type` indices, or with none.
      WGPURenderPipeline render_variant (PipelineRow& row,
                                         const IndexType* type) {
        if (!row.strip)
          return row.render[0];
        // A draw without indices takes whichever pipeline the strip has.
        const std::size_t which = type ? std::size_t (*type)
                                  : row.render[0] ? 0 : 1;
        if (!row.render[which])
          row.render[which] =
            render_pipeline (row, wgpu_index (IndexType (which)));
        return row.render[which];
      }

      static Retired retired (const PipelineRow& row) {
        Retired r;
        r.render = row.render;
        r.compute = row.compute;
        r.vertex = row.vertex;
        r.fragment = row.fragment;
        return r;
      }

      void release (const Retired& r) {
        if (r.buffer) {
          wgpuBufferDestroy (r.buffer);
          wgpuBufferRelease (r.buffer);
        }
        if (r.view)
          wgpuTextureViewRelease (r.view);
        if (r.texture) {
          wgpuTextureDestroy (r.texture);
          wgpuTextureRelease (r.texture);
        }
        for (WGPURenderPipeline pipeline : r.render)
          if (pipeline)
            wgpuRenderPipelineRelease (pipeline);
        if (r.compute)
          wgpuComputePipelineRelease (r.compute);
        if (r.vertex)
          wgpuShaderModuleRelease (r.vertex);
        if (r.fragment)
          wgpuShaderModuleRelease (r.fragment);
      }

      // -- bindings ---------------------------------------------------

      void reset_bindings () {
        m_set_pipeline = nullptr;
        m_set_compute = nullptr;
        m_set_layout = nullptr;
        m_set_indices = nullptr;
        m_buffers_dirty = m_textures_dirty = true;
      }

      bool attached (Texture handle) const {
        for (std::uint32_t i = 0; i < m_pass.color_count; ++i)
          if (m_pass.colors[i].texture == handle
              || m_pass.colors[i].resolve == handle)
            return true;
        return m_pass.depth.texture == handle;
      }

      void set_group (std::uint32_t index, WGPUBindGroup group,
                      std::size_t count = 0,
                      const std::uint32_t* offsets = nullptr) {
        if (m_render)
          wgpuRenderPassEncoderSetBindGroup (m_render, index, group, count,
                                             offsets);
        else
          wgpuComputePassEncoderSetBindGroup (m_compute, index, group, count,
                                              offsets);
      }

      // The group binding `entries`, made when none like it is kept.
      WGPUBindGroup group_for (const GroupKey& key, const Layout& layout,
                               std::span<const WGPUBindGroupEntry> entries) {
        CachedGroup& cached = m_groups[key];
        if (!cached.group) {
          WGPUBindGroupDescriptor info = WGPU_BIND_GROUP_DESCRIPTOR_INIT;
          info.layout = layout.groups[key.group];
          info.entryCount = entries.size ();
          info.entries = entries.data ();
          cached.group = wgpuDeviceCreateBindGroup (m_device, &info);
        }
        cached.used = m_serial;
        return cached.group;
      }

      void sweep_groups () {
        for (auto it = m_groups.begin (); it != m_groups.end ();)
          if (it->second.used + group_lifetime < m_serial) {
            wgpuBindGroupRelease (it->second.group);
            it = m_groups.erase (it);
          } else {
            ++it;
          }
      }

      // Sets the pipeline and the bind groups for the next draw (with
      // indices of `type`, if any) or dispatch.
      void bind_arguments (const IndexType* type) {
        if (!m_pipeline)
          throw std::runtime_error ("NHAL: draw without a pipeline");
        PipelineRow& row = m_pipelines[m_pipeline];
        if (row.is_compute != (m_compute != nullptr))
          throw std::runtime_error (std::string ("NHAL: ") + row.name
                                    + " is used in the wrong kind of pass");
        if (row.is_compute) {
          if (m_set_compute != row.compute) {
            wgpuComputePassEncoderSetPipeline (m_compute, row.compute);
            m_set_compute = row.compute;
          }
        } else {
          const WGPURenderPipeline variant = render_variant (row, type);
          if (m_set_pipeline != variant) {
            wgpuRenderPassEncoderSetPipeline (m_render, variant);
            m_set_pipeline = variant;
          }
        }
        const Layout& layout = *row.layout;
        if (m_set_layout != &layout) {
          m_set_layout = &layout;
          m_buffers_dirty = m_textures_dirty = true;
          set_group (2, layout.samplers);
        }
        if (m_buffers_dirty) {
          m_buffers_dirty = false;
          bind_buffers (row, layout);
        }
        if (m_textures_dirty) {
          m_textures_dirty = false;
          bind_textures (row, layout);
        }
      }

      void bind_buffers (const PipelineRow& row, const Layout& layout) {
        GroupKey key { layout.id, 0 };
        std::array<WGPUBindGroupEntry, max_bindings> entries;
        std::array<std::uint32_t, max_bindings> offsets;
        std::size_t count = 0, dynamic = 0;
        for (const BufferSlot& slot : layout.buffers) {
          BoundBuffer bound = m_bound_buffers[slot.binding];
          if (!bound.buffer) {
            if (slot.kind == ResourceKind::read_write_storage_buffer)
              throw std::runtime_error (
                std::string ("NHAL: ") + row.name
                + " writes a buffer that is not bound, at binding "
                + std::to_string (slot.binding));
            bound = { m_null_buffer, m_null_buffer_id, 0, 64 << 10 };
          }
          if (bound.offset % offset_alignment)
            throw std::runtime_error (
              std::string ("NHAL: ") + row.name + " binds a buffer from "
              + std::to_string (bound.offset)
              + " bytes; WebGPU binds from multiples of 256");
          WGPUBindGroupEntry& e = entries[count++];
          e = WGPU_BIND_GROUP_ENTRY_INIT;
          e.binding = slot.binding;
          e.buffer = bound.buffer;
          e.size = slot.kind == ResourceKind::uniform_block ? slot.size
                                                            : bound.size;
          key.add (bound.id);
          key.add (e.size);
          if (slot.dynamic) {
            offsets[dynamic++] = std::uint32_t (bound.offset);
          } else {
            e.offset = bound.offset;
            key.add (bound.offset);
          }
        }
        set_group (0, group_for (key, layout, { entries.data (), count }),
                   dynamic, offsets.data ());
      }

      void bind_textures (const PipelineRow& row, const Layout& layout) {
        GroupKey key { layout.id, 1 };
        std::array<WGPUBindGroupEntry, 2 * max_bindings> entries;
        std::size_t count = 0;
        for (const TextureSlot& slot : layout.textures) {
          Texture handle = m_bound_textures[slot.binding];
          // A texture the pass renders into reads as unbound, as on
          // Direct3D 12 and Vulkan.
          if (handle && m_render && attached (handle)) {
            TextureRow& t = m_textures[handle];
            if (!t.warned) {
              t.warned = true;
              std::cerr << "NHAL: " << row.name << " binds attachment "
                        << (t.desc.label ? t.desc.label : "(unnamed)")
                        << std::endl;
            }
            handle = {};
          }
          if (!handle)
            handle = slot.kind == ResourceKind::depth_texture_2d
                       ? m_null_depth
                     : slot.kind == ResourceKind::uint_texture_2d
                       ? m_null_uint
                       : m_null_float;
          const TextureRow& t = m_textures[handle];
          WGPUBindGroupEntry& e = entries[count++];
          e = WGPU_BIND_GROUP_ENTRY_INIT;
          e.binding = slot.binding;
          e.textureView = t.view;
          key.add (t.id);
        }
        set_group (1, group_for (key, layout, { entries.data (), count }));
      }

      void set_index_buffer (Buffer indices, IndexType type) {
        const BufferRow& row = m_buffers[indices];
        if (m_set_indices == row.buffer && m_set_index_type == type)
          return;
        m_set_indices = row.buffer;
        m_set_index_type = type;
        wgpuRenderPassEncoderSetIndexBuffer (m_render, row.buffer,
                                             wgpu_index (type), 0, row.size);
      }

      // -- timings and captures ---------------------------------------

      // The shortest interval between recent frames is the display's.
      void note_frame_time () {
        const double now = frame_milliseconds () * 1e-3;
        if (m_frame_time > 0 && now > m_frame_time) {
          m_intervals[m_interval++ % m_intervals.size ()] = now - m_frame_time;
          const std::size_t seen = std::min (m_interval, m_intervals.size ());
          m_refresh = *std::min_element (m_intervals.begin (),
                                         m_intervals.begin () + seen);
        }
        m_frame_time = now;
      }

      // Whether the pass about to begin is timed, and by which queries.
      bool time_pass (WGPUPassTimestampWrites& writes, const char* label) {
        if (!m_timing_frame || m_frame_labels.size () == max_timed_passes)
          return false;
        writes.querySet = m_query_set;
        writes.beginningOfPassWriteIndex =
          std::uint32_t (2 * m_frame_labels.size ());
        writes.endOfPassWriteIndex = writes.beginningOfPassWriteIndex + 1;
        m_frame_labels.emplace_back (label);
        return true;
      }

      void encode_timings () {
        const std::uint32_t queries =
          std::uint32_t (2 * m_frame_labels.size ());
        wgpuCommandEncoderResolveQuerySet (m_encoder, m_query_set, 0,
                                           queries, m_query_resolve, 0);
        wgpuCommandEncoderCopyBufferToBuffer (m_encoder, m_query_resolve, 0,
                                              m_query_readback, 0,
                                              queries * 8ull);
      }

      static void timings_mapped (WGPUMapAsyncStatus status, WGPUStringView,
                                  void* device, void*) {
        auto& self = *static_cast<WebGpuDevice*> (device);
        if (status == WGPUMapAsyncStatus_Success)
          self.m_timing_ready = true;
        else
          self.m_timing_pending = false;
      }

      // The timed frame's passes, once its queries have been read back.
      void deliver_timings () {
        if (!m_timing_ready)
          return;
        m_timing_ready = false;
        const std::size_t bytes = m_pending_labels.size () * 16;
        const auto* stamps = static_cast<const std::uint64_t*> (
          wgpuBufferGetConstMappedRange (m_query_readback, 0, bytes));
        m_pass_timings.clear ();
        for (std::size_t i = 0; stamps && i < m_pending_labels.size (); ++i) {
          // Nanoseconds; a pass the GPU reordered may read backwards.
          const std::uint64_t begin = stamps[2 * i], end = stamps[2 * i + 1];
          m_pass_timings.push_back (
            { m_pending_labels[i],
              end > begin ? double (end - begin) * 1e-6 : 0.0 });
        }
        wgpuBufferUnmap (m_query_readback);
        m_timing_pending = false;
      }

      void encode_capture () {
        const TextureRow& back = m_textures[m_backbuffer];
        auto pending = std::make_unique<PendingCapture> ();
        const std::uint32_t row_bytes =
          back.desc.width * bytes_per_pixel (m_surface_format);
        pending->stride = (row_bytes + 255) & ~255u;
        WGPUBufferDescriptor info = WGPU_BUFFER_DESCRIPTOR_INIT;
        info.label = view ("capture");
        info.size = std::uint64_t (pending->stride) * back.desc.height;
        info.usage = WGPUBufferUsage_MapRead | WGPUBufferUsage_CopyDst;
        pending->readback = wgpuDeviceCreateBuffer (m_device, &info);
        WGPUTexelCopyTextureInfo source = WGPU_TEXEL_COPY_TEXTURE_INFO_INIT;
        source.texture = back.texture;
        WGPUTexelCopyBufferInfo target = WGPU_TEXEL_COPY_BUFFER_INFO_INIT;
        target.buffer = pending->readback;
        target.layout = { 0, pending->stride, back.desc.height };
        const WGPUExtent3D extent { back.desc.width, back.desc.height, 1 };
        wgpuCommandEncoderCopyTextureToBuffer (m_encoder, &source, &target,
                                               &extent);
        pending->capture = { back.desc.width, back.desc.height,
                             m_surface_format, row_bytes, {} };
        pending->done = std::move (m_capture_request);
        m_capture_request = nullptr;
        m_captures.push_back (std::move (pending));
      }

      static void capture_mapped (WGPUMapAsyncStatus status, WGPUStringView,
                                  void* capture, void*) {
        auto& pending = *static_cast<PendingCapture*> (capture);
        pending.failed = status != WGPUMapAsyncStatus_Success;
        pending.ready = true;
      }

      // Hands over the captures whose readbacks have been mapped.
      void deliver_captures () {
        for (auto it = m_captures.begin (); it != m_captures.end ();) {
          PendingCapture& pending = **it;
          if (!pending.ready) {
            ++it;
            continue;
          }
          const Capture& c = pending.capture;
          const auto* mapped = pending.failed
                                 ? nullptr
                                 : static_cast<const std::byte*> (
                                     wgpuBufferGetConstMappedRange (
                                       pending.readback, 0,
                                       std::size_t (pending.stride)
                                         * c.height));
          if (mapped) {
            std::vector<std::byte> pixels (std::size_t (c.row_bytes)
                                           * c.height);
            for (std::uint32_t y = 0; y < c.height; ++y)
              std::memcpy (pixels.data () + std::size_t (y) * c.row_bytes,
                           mapped + std::size_t (y) * pending.stride,
                           c.row_bytes);
            pending.capture.pixels = pixels;
            pending.done (pending.capture);
            wgpuBufferUnmap (pending.readback);
          } else {
            std::cerr << "NHAL: a capture's readback failed" << std::endl;
          }
          wgpuBufferDestroy (pending.readback);
          wgpuBufferRelease (pending.readback);
          it = m_captures.erase (it);
        }
      }

      WGPUInstance m_instance = nullptr;
      WGPUDevice m_device = nullptr;
      WGPUQueue m_queue = nullptr;
      WGPUSurface m_surface = nullptr;
      WGPULimits m_limits {};
      std::string m_adapter;
      Format m_surface_format = Format::bgra8_unorm;
      std::uint32_t m_width = 0, m_height = 0;

      Table<BufferRow> m_buffers;
      Table<TextureRow> m_textures;
      Table<PipelineRow> m_pipelines;
      Retirement<Retired> m_retired;
      std::unordered_map<const Program*, std::unique_ptr<Layout>> m_layouts;
      std::unordered_map<GroupKey, CachedGroup, GroupKeyHash> m_groups;
      std::array<WGPUSampler, standard_samplers> m_samplers {};
      std::uint32_t m_next_id = 0;
      std::uint32_t m_upload_buffers = 0;

      // What unbound slots read.
      WGPUBuffer m_null_buffer = nullptr;
      std::uint32_t m_null_buffer_id = 0;
      Texture m_null_float, m_null_depth, m_null_uint;

      // The frame.
      std::uint64_t m_serial = 0;
      Texture m_backbuffer;
      WGPUCommandEncoder m_encoder = nullptr;
      WGPURenderPassEncoder m_render = nullptr;
      WGPUComputePassEncoder m_compute = nullptr;
      RenderPassDesc m_pass {};
      std::uint32_t m_pass_width = 0, m_pass_height = 0;
      std::vector<std::byte> m_arena_memory;
      WGPUBuffer m_arena_buffer = nullptr;
      std::uint32_t m_arena_id = 0;
      Arena m_arena;

      // What the host has bound, and what the pass encoder has set.
      Pipeline m_pipeline;
      std::array<BoundBuffer, max_bindings> m_bound_buffers {};
      std::array<Texture, max_bindings> m_bound_textures {};
      std::array<Texture, max_bindings> m_bound_storage {};
      bool m_buffers_dirty = true, m_textures_dirty = true;
      WGPURenderPipeline m_set_pipeline = nullptr;
      WGPUComputePipeline m_set_compute = nullptr;
      const Layout* m_set_layout = nullptr;
      WGPUBuffer m_set_indices = nullptr;
      IndexType m_set_index_type = IndexType::uint16;

      // Frame times, for the host's steps.
      double m_frame_time = 0, m_refresh = 1.0 / 60.0;
      std::array<double, 32> m_intervals {};
      std::size_t m_interval = 0;

      // Pass timings: one frame's queries are read back at a time.
      bool m_timestamps = false;
      WGPUQuerySet m_query_set = nullptr;
      WGPUBuffer m_query_resolve = nullptr, m_query_readback = nullptr;
      bool m_timing_frame = false, m_timing_pending = false,
           m_timing_ready = false;
      std::vector<std::string> m_frame_labels, m_pending_labels;
      std::vector<PassTiming> m_pass_timings;

      std::function<void (const Capture&)> m_capture_request;
      std::vector<std::unique_ptr<PendingCapture>> m_captures;
    };
  }

  std::unique_ptr<Device> create_webgpu_device (const char* selector,
                                                std::uint32_t width,
                                                std::uint32_t height) {
    return std::make_unique<WebGpuDevice> (selector, width, height);
  }
}
