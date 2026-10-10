// NHAL on Direct3D 12 at feature level 11_0, shader model 6.x, resource
// binding tier 3: what Xbox Series consoles give a Developer Mode UWP app.
//
// Each program's reflection becomes a root signature: a root CBV or SRV per
// buffer binding (so buffers are bound by GPU address, as on Metal), one
// descriptor table for its textures (t0-tN in space 1), and the standard
// samplers as static samplers. Texture bindings are copied into a ring in
// the shader-visible heap at each draw. Textures carry their resource state,
// and the device transitions them where passes and bindings need them;
// buffers rely on implicit promotion and decay.
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>

#include <d3d12.h>
#include <dxgi1_4.h>
#include <wrl/client.h>

#include <moppe/nhal/d3d12/d3d12_device.hh>
#include <moppe/nhal/presentation.hh>
#include <moppe/nhal/table.hh>

#include <algorithm>
#include <array>
#include <atomic>
#include <cstdio>
#include <iostream>
#include <stdexcept>
#include <string>
#include <unordered_map>
#include <vector>

using Microsoft::WRL::ComPtr;

namespace moppe::nhal {
  namespace {
    std::atomic<const char*> current_step { "idle" };
    // The live device's fence and device, for a watchdog's report.
    std::atomic<ID3D12Fence*> watched_fence { nullptr };
    std::atomic<ID3D12Device*> watched_device { nullptr };
    std::atomic<std::uint64_t> awaited_value { 0 };

    void step (const char* name) {
      current_step.store (name, std::memory_order_relaxed);
    }

    constexpr std::uint32_t frames_in_flight = 3;
    constexpr std::uint64_t arena_capacity = 16u << 20;
    constexpr std::uint32_t ring_descriptors = 3 * 16384;
    constexpr std::uint32_t max_bindings = 16;
    constexpr std::uint32_t max_timestamps = 64;

    void check (HRESULT hr, const char* what) {
      if (FAILED (hr)) {
        char message[128];
        std::snprintf (message, sizeof message, "NHAL: %s failed: 0x%08lx",
                       what, static_cast<unsigned long> (hr));
        throw std::runtime_error (message);
      }
    }

    DXGI_FORMAT dxgi_format (Format format) {
      switch (format) {
      case Format::undefined: return DXGI_FORMAT_UNKNOWN;
      case Format::rgba8_unorm: return DXGI_FORMAT_R8G8B8A8_UNORM;
      case Format::rgba8_unorm_srgb: return DXGI_FORMAT_R8G8B8A8_UNORM_SRGB;
      case Format::bgra8_unorm: return DXGI_FORMAT_B8G8R8A8_UNORM;
      case Format::bgra8_unorm_srgb: return DXGI_FORMAT_B8G8R8A8_UNORM_SRGB;
      case Format::rgb10a2_unorm: return DXGI_FORMAT_R10G10B10A2_UNORM;
      case Format::rgba16_float: return DXGI_FORMAT_R16G16B16A16_FLOAT;
      case Format::rg16_float: return DXGI_FORMAT_R16G16_FLOAT;
      case Format::rg16_snorm: return DXGI_FORMAT_R16G16_SNORM;
      case Format::r16_float: return DXGI_FORMAT_R16_FLOAT;
      case Format::r8_unorm: return DXGI_FORMAT_R8_UNORM;
      case Format::r32_float: return DXGI_FORMAT_R32_FLOAT;
      case Format::rg32_float: return DXGI_FORMAT_R32G32_FLOAT;
      case Format::rgba32_float: return DXGI_FORMAT_R32G32B32A32_FLOAT;
      case Format::r32_uint: return DXGI_FORMAT_R32_UINT;
      case Format::d32_float: return DXGI_FORMAT_D32_FLOAT;
      }
      return DXGI_FORMAT_UNKNOWN;
    }

    D3D12_COMPARISON_FUNC comparison (CompareOp op) {
      return static_cast<D3D12_COMPARISON_FUNC> (
        D3D12_COMPARISON_FUNC_NEVER + static_cast<int> (op));
    }

    D3D12_HEAP_PROPERTIES heap (D3D12_HEAP_TYPE type) {
      D3D12_HEAP_PROPERTIES properties {};
      properties.Type = type;
      return properties;
    }

    D3D12_RESOURCE_DESC buffer_desc (std::uint64_t size) {
      D3D12_RESOURCE_DESC desc {};
      desc.Dimension = D3D12_RESOURCE_DIMENSION_BUFFER;
      desc.Width = std::max<std::uint64_t> (size, 16);
      desc.Height = 1;
      desc.DepthOrArraySize = 1;
      desc.MipLevels = 1;
      desc.SampleDesc.Count = 1;
      desc.Layout = D3D12_TEXTURE_LAYOUT_ROW_MAJOR;
      return desc;
    }

    // A free list over one CPU descriptor heap.
    struct DescriptorPool {
      ComPtr<ID3D12DescriptorHeap> heap;
      UINT step = 0;
      std::vector<std::uint32_t> free;
      std::uint32_t next = 0;
      std::uint32_t capacity = 0;

      void create (ID3D12Device* device, D3D12_DESCRIPTOR_HEAP_TYPE type,
                   std::uint32_t count) {
        D3D12_DESCRIPTOR_HEAP_DESC desc { type, count };
        check (device->CreateDescriptorHeap (&desc, IID_PPV_ARGS (&heap)),
               "descriptor heap");
        step = device->GetDescriptorHandleIncrementSize (type);
        capacity = count;
      }

      std::uint32_t allocate () {
        if (!free.empty ()) {
          const std::uint32_t slot = free.back ();
          free.pop_back ();
          return slot;
        }
        if (next == capacity)
          throw std::runtime_error ("NHAL: descriptor pool exhausted");
        return next++;
      }

      D3D12_CPU_DESCRIPTOR_HANDLE cpu (std::uint32_t slot) const {
        D3D12_CPU_DESCRIPTOR_HANDLE h =
          heap->GetCPUDescriptorHandleForHeapStart ();
        h.ptr += SIZE_T (slot) * step;
        return h;
      }
    };

    constexpr std::uint32_t no_slot = ~0u;

    // Default-heap buffers decay to COMMON whenever a command list
    // finishes, so a state is only meaningful in the frame that set it.
    struct D3DBuffer {
      ComPtr<ID3D12Resource> resource;
      void* mapped = nullptr;
      D3D12_RESOURCE_STATES state = D3D12_RESOURCE_STATE_COMMON;
      std::uint64_t state_serial = 0;
    };

    struct D3DTexture {
      ComPtr<ID3D12Resource> resource;
      TextureDesc desc {};
      D3D12_RESOURCE_STATES state = D3D12_RESOURCE_STATE_COMMON;
      std::uint32_t srv = no_slot, uav = no_slot, rtv = no_slot,
                    dsv = no_slot;
      bool warned = false;
    };

    enum class RootBuffer : std::uint8_t { none, constants, read, write };

    // How a program's bindings land in its root signature.
    struct RootLayout {
      ComPtr<ID3D12RootSignature> signature;
      std::array<int, max_bindings> buffer_parameter;
      std::array<RootBuffer, max_bindings> buffer_kind {};
      int texture_parameter = -1;
      std::uint32_t texture_count = 0;
      int storage_parameter = -1;
      std::uint32_t storage_count = 0;
    };

    struct D3DPipeline {
      ComPtr<ID3D12PipelineState> state;
      const RootLayout* layout = nullptr;
      D3D_PRIMITIVE_TOPOLOGY topology = D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST;
      bool compute = false;
      const char* name = "";
    };

    struct Retired {
      ComPtr<ID3D12Object> object;
      std::uint32_t srv = no_slot, uav = no_slot, rtv = no_slot,
                    dsv = no_slot;
    };

    // A binding: the buffer, when it is one of the device's (arena slices
    // are upload memory and need no barriers), and the address.
    struct BoundBuffer {
      Buffer buffer;
      std::uint64_t address = 0;
    };

    D3D12_STATIC_SAMPLER_DESC standard_sampler (std::uint32_t binding,
                                                bool comparison_sampler) {
      D3D12_STATIC_SAMPLER_DESC s {};
      const bool nearest = binding == 2;
      s.Filter = comparison_sampler
                   ? D3D12_FILTER_COMPARISON_MIN_MAG_MIP_LINEAR
                 : nearest ? D3D12_FILTER_MIN_MAG_MIP_POINT
                           : D3D12_FILTER_MIN_MAG_MIP_LINEAR;
      const D3D12_TEXTURE_ADDRESS_MODE mode =
        binding == 1 ? D3D12_TEXTURE_ADDRESS_MODE_WRAP
                     : D3D12_TEXTURE_ADDRESS_MODE_CLAMP;
      s.AddressU = s.AddressV = s.AddressW = mode;
      s.ComparisonFunc = comparison_sampler
                           ? D3D12_COMPARISON_FUNC_LESS_EQUAL
                           : D3D12_COMPARISON_FUNC_NEVER;
      s.MaxLOD = D3D12_FLOAT32_MAX;
      s.ShaderRegister = binding;
      s.ShaderVisibility = D3D12_SHADER_VISIBILITY_ALL;
      return s;
    }

    class D3D12Device final : public Device {
    public:
      D3D12Device (IUnknown* window, std::uint32_t width,
                   std::uint32_t height, Format surface_format)
        : m_surface_format (surface_format), m_width (width),
          m_height (height) {
        check (D3D12CreateDevice (nullptr, D3D_FEATURE_LEVEL_11_0,
                                  IID_PPV_ARGS (&m_device)),
               "D3D12CreateDevice");
        D3D12_COMMAND_QUEUE_DESC queue { D3D12_COMMAND_LIST_TYPE_DIRECT };
        check (m_device->CreateCommandQueue (&queue, IID_PPV_ARGS (&m_queue)),
               "command queue");
        check (m_device->CreateFence (0, D3D12_FENCE_FLAG_NONE,
                                      IID_PPV_ARGS (&m_fence)),
               "fence");
        check (m_device->CreateFence (0, D3D12_FENCE_FLAG_NONE,
                                      IID_PPV_ARGS (&m_setup_fence)),
               "setup fence");
        watched_fence = m_fence.Get ();
        watched_device = m_device.Get ();
        m_event = CreateEventExW (nullptr, nullptr, 0, EVENT_ALL_ACCESS);

        for (std::uint32_t i = 0; i < frames_in_flight; ++i) {
          check (m_device->CreateCommandAllocator (
                   D3D12_COMMAND_LIST_TYPE_DIRECT,
                   IID_PPV_ARGS (&m_allocators[i])),
                 "command allocator");
          const auto upload = heap (D3D12_HEAP_TYPE_UPLOAD);
          const auto desc = buffer_desc (arena_capacity);
          check (m_device->CreateCommittedResource (
                   &upload, D3D12_HEAP_FLAG_NONE, &desc,
                   D3D12_RESOURCE_STATE_GENERIC_READ, nullptr,
                   IID_PPV_ARGS (&m_arena_buffers[i])),
                 "frame arena");
          check (m_arena_buffers[i]->Map (0, nullptr, &m_arena_data[i]),
                 "map frame arena");
        }
        check (m_device->CreateCommandList (
                 0, D3D12_COMMAND_LIST_TYPE_DIRECT, m_allocators[0].Get (),
                 nullptr, IID_PPV_ARGS (&m_list)),
               "command list");
        m_list->Close ();
        check (m_device->CreateCommandAllocator (
                 D3D12_COMMAND_LIST_TYPE_DIRECT,
                 IID_PPV_ARGS (&m_setup_allocator)),
               "setup allocator");
        check (m_device->CreateCommandList (
                 0, D3D12_COMMAND_LIST_TYPE_DIRECT,
                 m_setup_allocator.Get (), nullptr,
                 IID_PPV_ARGS (&m_setup)),
               "setup list");
        m_setup->Close ();

        m_srv_pool.create (m_device.Get (),
                           D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV, 4096);
        m_rtv_pool.create (m_device.Get (), D3D12_DESCRIPTOR_HEAP_TYPE_RTV,
                           256);
        m_dsv_pool.create (m_device.Get (), D3D12_DESCRIPTOR_HEAP_TYPE_DSV,
                           64);
        D3D12_DESCRIPTOR_HEAP_DESC ring {
          D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV, ring_descriptors,
          D3D12_DESCRIPTOR_HEAP_FLAG_SHADER_VISIBLE };
        check (m_device->CreateDescriptorHeap (&ring, IID_PPV_ARGS (&m_ring)),
               "shader-visible heap");
        m_ring_step = m_srv_pool.step;

        // A null descriptor for texture slots a draw leaves unbound.
        m_null_srv = m_srv_pool.allocate ();
        D3D12_SHADER_RESOURCE_VIEW_DESC null {};
        null.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
        null.ViewDimension = D3D12_SRV_DIMENSION_TEXTURE2D;
        null.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
        null.Texture2D.MipLevels = 1;
        m_device->CreateShaderResourceView (nullptr, &null,
                                            m_srv_pool.cpu (m_null_srv));
        m_null_uav = m_srv_pool.allocate ();
        D3D12_UNORDERED_ACCESS_VIEW_DESC null_uav {};
        null_uav.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
        null_uav.ViewDimension = D3D12_UAV_DIMENSION_TEXTURE2D;
        m_device->CreateUnorderedAccessView (nullptr, nullptr, &null_uav,
                                             m_srv_pool.cpu (m_null_uav));

        // Timestamps: a range of the query heap per frame slot, resolved
        // into one readback buffer.
        UINT64 frequency = 0;
        m_queue->GetTimestampFrequency (&frequency);
        m_ticks_per_ms = double (frequency) / 1000.0;
        D3D12_QUERY_HEAP_DESC queries { D3D12_QUERY_HEAP_TYPE_TIMESTAMP,
                                        max_timestamps * frames_in_flight };
        check (m_device->CreateQueryHeap (&queries,
                                          IID_PPV_ARGS (&m_timestamps)),
               "timestamp heap");
        {
          const auto readback = heap (D3D12_HEAP_TYPE_READBACK);
          const auto desc =
            buffer_desc (8ull * max_timestamps * frames_in_flight);
          check (m_device->CreateCommittedResource (
                   &readback, D3D12_HEAP_FLAG_NONE, &desc,
                   D3D12_RESOURCE_STATE_COPY_DEST, nullptr,
                   IID_PPV_ARGS (&m_timestamp_readback)),
                 "timestamp readback");
        }

        // Indirect commands that carry only their arguments.
        auto signature = [&] (D3D12_INDIRECT_ARGUMENT_TYPE type, UINT stride,
                              ComPtr<ID3D12CommandSignature>& out) {
          D3D12_INDIRECT_ARGUMENT_DESC argument {};
          argument.Type = type;
          const D3D12_COMMAND_SIGNATURE_DESC desc { stride, 1, &argument, 0 };
          check (m_device->CreateCommandSignature (&desc, nullptr,
                                                   IID_PPV_ARGS (&out)),
                 "command signature");
        };
        signature (D3D12_INDIRECT_ARGUMENT_TYPE_DRAW,
                   sizeof (DrawIndirectArgs), m_draw_signature);
        signature (D3D12_INDIRECT_ARGUMENT_TYPE_DRAW_INDEXED,
                   sizeof (DrawIndexedIndirectArgs),
                   m_draw_indexed_signature);
        signature (D3D12_INDIRECT_ARGUMENT_TYPE_DISPATCH,
                   sizeof (DispatchIndirectArgs), m_dispatch_signature);

        ComPtr<IDXGIFactory2> factory;
        check (CreateDXGIFactory2 (0, IID_PPV_ARGS (&factory)), "DXGI");
        ComPtr<IDXGIAdapter1> adapter;
        if (SUCCEEDED (static_cast<IDXGIFactory1*> (factory.Get ())
                         ->EnumAdapters1 (0, &adapter))) {
          DXGI_ADAPTER_DESC1 desc {};
          adapter->GetDesc1 (&desc);
          char name[128] {};
          WideCharToMultiByte (CP_UTF8, 0, desc.Description, -1, name,
                               sizeof name, nullptr, nullptr);
          m_adapter = name;
        }
        DXGI_SWAP_CHAIN_DESC1 desc {};
        desc.Width = width;
        desc.Height = height;
        desc.Format = dxgi_format (surface_format);
        desc.SampleDesc.Count = 1;
        desc.BufferUsage = DXGI_USAGE_RENDER_TARGET_OUTPUT;
        desc.BufferCount = frames_in_flight;
        desc.Scaling = DXGI_SCALING_STRETCH;
        desc.SwapEffect = DXGI_SWAP_EFFECT_FLIP_DISCARD;
        ComPtr<IDXGISwapChain1> swapchain;
        check (factory->CreateSwapChainForCoreWindow (
                 m_queue.Get (), window, &desc, nullptr, &swapchain),
               "swapchain");
        check (swapchain.As (&m_swapchain), "IDXGISwapChain3");
        adopt_backbuffers ();
      }

      ~D3D12Device () override {
        wait_idle ();
        watched_fence = nullptr;
        watched_device = nullptr;
        if (m_event)
          CloseHandle (m_event);
      }

      DeviceInfo info () const override {
        return { "Direct3D 12", m_adapter, frames_in_flight, false };
      }

      // -- resources --------------------------------------------------

      Buffer create_buffer (const BufferDesc& desc,
                            std::span<const std::byte> initial) override {
        D3DBuffer buffer;
        auto resource_desc = buffer_desc (desc.size);
        if (desc.usage & buffer_storage_write)
          resource_desc.Flags |= D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS;
        if (desc.memory == Memory::upload) {
          const auto upload = heap (D3D12_HEAP_TYPE_UPLOAD);
          check (m_device->CreateCommittedResource (
                   &upload, D3D12_HEAP_FLAG_NONE, &resource_desc,
                   D3D12_RESOURCE_STATE_GENERIC_READ, nullptr,
                   IID_PPV_ARGS (&buffer.resource)),
                 "upload buffer");
          check (buffer.resource->Map (0, nullptr, &buffer.mapped),
                 "map buffer");
          if (!initial.empty ())
            std::memcpy (buffer.mapped, initial.data (), initial.size ());
        } else {
          const auto device_heap = heap (D3D12_HEAP_TYPE_DEFAULT);
          check (m_device->CreateCommittedResource (
                   &device_heap, D3D12_HEAP_FLAG_NONE, &resource_desc,
                   D3D12_RESOURCE_STATE_COMMON, nullptr,
                   IID_PPV_ARGS (&buffer.resource)),
                 "buffer");
          if (!initial.empty ()) {
            ComPtr<ID3D12Resource> staging = staging_buffer (initial);
            begin_setup ();
            m_setup->CopyBufferRegion (buffer.resource.Get (), 0,
                                       staging.Get (), 0, initial.size ());
            finish_setup ();
          }
        }
        name (buffer.resource.Get (), desc.label);
        return m_buffers.insert<Buffer> (std::move (buffer));
      }

      void* contents (Buffer buffer) override {
        return m_buffers[buffer].mapped;
      }

      Texture create_texture (const TextureDesc& desc) override {
        D3DTexture texture;
        texture.desc = desc;
        const DXGI_FORMAT format = dxgi_format (desc.format);
        const bool depth = (desc.usage & usage_depth) != 0;
        const bool sampled_depth = depth && (desc.usage & usage_sampled);
        D3D12_RESOURCE_DESC rd {};
        rd.Dimension = D3D12_RESOURCE_DIMENSION_TEXTURE2D;
        rd.Width = desc.width;
        rd.Height = desc.height;
        rd.DepthOrArraySize = 1;
        rd.MipLevels = 1;
        rd.Format = sampled_depth ? DXGI_FORMAT_R32_TYPELESS : format;
        rd.SampleDesc.Count = desc.samples;
        if (desc.usage & usage_render_target)
          rd.Flags |= D3D12_RESOURCE_FLAG_ALLOW_RENDER_TARGET;
        if (depth)
          rd.Flags |= D3D12_RESOURCE_FLAG_ALLOW_DEPTH_STENCIL;
        if (depth && !(desc.usage & usage_sampled))
          rd.Flags |= D3D12_RESOURCE_FLAG_DENY_SHADER_RESOURCE;
        if (desc.usage & usage_storage)
          rd.Flags |= D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS;

        D3D12_CLEAR_VALUE clear {};
        clear.Format = format;
        const bool optimized = depth || (desc.usage & usage_render_target);
        if (depth)
          clear.DepthStencil.Depth = 0.0f;
        else
          clear.Color[3] = 1.0f;
        texture.state = depth ? D3D12_RESOURCE_STATE_DEPTH_WRITE
                        : (desc.usage & usage_render_target)
                          ? D3D12_RESOURCE_STATE_RENDER_TARGET
                          : D3D12_RESOURCE_STATE_COPY_DEST;
        const auto device_heap = heap (D3D12_HEAP_TYPE_DEFAULT);
        check (m_device->CreateCommittedResource (
                 &device_heap, D3D12_HEAP_FLAG_NONE, &rd, texture.state,
                 optimized ? &clear : nullptr,
                 IID_PPV_ARGS (&texture.resource)),
               "texture");
        name (texture.resource.Get (), desc.label);
        make_views (texture);
        return m_textures.insert<Texture> (std::move (texture));
      }

      void write_texture (Texture handle, std::span<const std::byte> pixels,
                          std::uint32_t row_bytes) override {
        D3DTexture& t = m_textures[handle];
        const auto rd = t.resource->GetDesc ();
        D3D12_PLACED_SUBRESOURCE_FOOTPRINT footprint {};
        UINT64 total = 0;
        m_device->GetCopyableFootprints (&rd, 0, 1, 0, &footprint, nullptr,
                                         nullptr, &total);
        if (!row_bytes)
          row_bytes = t.desc.width * bytes_per_pixel (t.desc.format);
        std::vector<std::byte> staged (total);
        for (std::uint32_t y = 0; y < t.desc.height; ++y)
          std::memcpy (staged.data () + footprint.Footprint.RowPitch * y,
                       pixels.data () + std::size_t (row_bytes) * y,
                       std::min<std::size_t> (row_bytes,
                                              footprint.Footprint.RowPitch));
        ComPtr<ID3D12Resource> staging = staging_buffer (staged);
        begin_setup ();
        transition (m_setup.Get (), t, D3D12_RESOURCE_STATE_COPY_DEST);
        D3D12_TEXTURE_COPY_LOCATION to {};
        to.pResource = t.resource.Get ();
        to.Type = D3D12_TEXTURE_COPY_TYPE_SUBRESOURCE_INDEX;
        D3D12_TEXTURE_COPY_LOCATION from {};
        from.pResource = staging.Get ();
        from.Type = D3D12_TEXTURE_COPY_TYPE_PLACED_FOOTPRINT;
        from.PlacedFootprint = footprint;
        m_setup->CopyTextureRegion (&to, 0, 0, 0, &from, nullptr);
        transition (m_setup.Get (), t,
                    D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE
                      | D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE);
        finish_setup ();
      }

      Pipeline create_render_pipeline (const RenderPipelineDesc& desc)
        override {
        const RootLayout& layout = root_layout (*desc.program);
        D3D12_GRAPHICS_PIPELINE_STATE_DESC pd {};
        pd.pRootSignature = layout.signature.Get ();
        pd.VS = { desc.vertex.dxil.data (), desc.vertex.dxil.size () };
        pd.PS = { desc.fragment.dxil.data (), desc.fragment.dxil.size () };
        for (std::uint32_t i = 0; i < desc.color_count; ++i) {
          pd.RTVFormats[i] = dxgi_format (desc.color_formats[i]);
          auto& blend = pd.BlendState.RenderTarget[i];
          blend.RenderTargetWriteMask = D3D12_COLOR_WRITE_ENABLE_ALL;
          blend.SrcBlend = blend.SrcBlendAlpha = D3D12_BLEND_ONE;
          blend.DestBlend = blend.DestBlendAlpha = D3D12_BLEND_ZERO;
          blend.BlendOp = blend.BlendOpAlpha = D3D12_BLEND_OP_ADD;
          blend.LogicOp = D3D12_LOGIC_OP_NOOP;
          if (desc.blend[i] == Blend::none)
            continue;
          const bool additive = desc.blend[i] == Blend::additive;
          blend.BlendEnable = TRUE;
          blend.SrcBlend = desc.blend[i] == Blend::premultiplied
                             ? D3D12_BLEND_ONE
                             : D3D12_BLEND_SRC_ALPHA;
          blend.DestBlend = additive ? D3D12_BLEND_ONE
                                     : D3D12_BLEND_INV_SRC_ALPHA;
          blend.DestBlendAlpha = additive ? D3D12_BLEND_ONE
                                          : D3D12_BLEND_INV_SRC_ALPHA;
        }
        pd.NumRenderTargets = desc.color_count;
        pd.BlendState.IndependentBlendEnable = TRUE;
        pd.SampleMask = UINT_MAX;
        pd.RasterizerState.FillMode = D3D12_FILL_MODE_SOLID;
        pd.RasterizerState.CullMode =
          desc.cull == Cull::back    ? D3D12_CULL_MODE_BACK
          : desc.cull == Cull::front ? D3D12_CULL_MODE_FRONT
                                     : D3D12_CULL_MODE_NONE;
        pd.RasterizerState.FrontCounterClockwise =
          desc.front_counter_clockwise;
        pd.RasterizerState.DepthClipEnable = TRUE;
        pd.RasterizerState.DepthBias = INT (desc.depth_bias);
        pd.RasterizerState.SlopeScaledDepthBias = desc.slope_scaled_depth_bias;
        pd.RasterizerState.MultisampleEnable = desc.samples > 1;
        if (desc.depth_format != Format::undefined) {
          pd.DSVFormat = dxgi_format (desc.depth_format);
          pd.DepthStencilState.DepthEnable = TRUE;
          pd.DepthStencilState.DepthFunc = comparison (desc.depth_compare);
          pd.DepthStencilState.DepthWriteMask =
            desc.depth_write ? D3D12_DEPTH_WRITE_MASK_ALL
                             : D3D12_DEPTH_WRITE_MASK_ZERO;
        }
        pd.PrimitiveTopologyType = desc.topology == Topology::line_list
                                     ? D3D12_PRIMITIVE_TOPOLOGY_TYPE_LINE
                                     : D3D12_PRIMITIVE_TOPOLOGY_TYPE_TRIANGLE;
        pd.SampleDesc.Count = desc.samples;
        // Strips restart at the all-ones index, as Metal's always do.
        if (desc.topology == Topology::triangle_strip)
          pd.IBStripCutValue = D3D12_INDEX_BUFFER_STRIP_CUT_VALUE_0xFFFFFFFF;

        D3DPipeline pipeline;
        pipeline.layout = &layout;
        pipeline.name = desc.program->name;
        pipeline.topology =
          desc.topology == Topology::triangle_strip
            ? D3D_PRIMITIVE_TOPOLOGY_TRIANGLESTRIP
          : desc.topology == Topology::line_list
            ? D3D_PRIMITIVE_TOPOLOGY_LINELIST
            : D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST;
        const HRESULT hr = m_device->CreateGraphicsPipelineState (
          &pd, IID_PPV_ARGS (&pipeline.state));
        if (FAILED (hr)) {
          char message[160];
          std::snprintf (message, sizeof message,
                         "NHAL: pipeline %s failed: 0x%08lx",
                         desc.program->name, static_cast<unsigned long> (hr));
          throw std::runtime_error (message);
        }
        name (pipeline.state.Get (),
              desc.label ? desc.label : desc.program->name);
        return m_pipelines.insert<Pipeline> (std::move (pipeline));
      }

      Pipeline create_compute_pipeline (const ComputePipelineDesc& desc)
        override {
        const RootLayout& layout = root_layout (*desc.program);
        D3D12_COMPUTE_PIPELINE_STATE_DESC pd {};
        pd.pRootSignature = layout.signature.Get ();
        pd.CS = { desc.compute.dxil.data (), desc.compute.dxil.size () };
        D3DPipeline pipeline;
        pipeline.layout = &layout;
        pipeline.name = desc.program->name;
        pipeline.compute = true;
        const HRESULT hr = m_device->CreateComputePipelineState (
          &pd, IID_PPV_ARGS (&pipeline.state));
        if (FAILED (hr)) {
          char message[160];
          std::snprintf (message, sizeof message,
                         "NHAL: compute pipeline %s failed: 0x%08lx",
                         desc.program->name, static_cast<unsigned long> (hr));
          throw std::runtime_error (message);
        }
        name (pipeline.state.Get (),
              desc.label ? desc.label : desc.program->name);
        return m_pipelines.insert<Pipeline> (std::move (pipeline));
      }

      void destroy (Buffer buffer) override {
        m_retired.retire (m_serial, { m_buffers.take (buffer).resource });
      }

      void destroy (Texture texture) override {
        D3DTexture t = m_textures.take (texture);
        m_retired.retire (m_serial,
                          { t.resource, t.srv, t.uav, t.rtv, t.dsv });
      }

      void destroy (Pipeline pipeline) override {
        m_retired.retire (m_serial, { m_pipelines.take (pipeline).state });
      }

      // -- the drawable -----------------------------------------------

      Format surface_format () const override { return m_surface_format; }
      std::uint32_t surface_width () const override { return m_width; }
      std::uint32_t surface_height () const override { return m_height; }

      void resize_surface (std::uint32_t width, std::uint32_t height)
        override {
        if (width == m_width && height == m_height)
          return;
        wait_idle ();
        for (Texture t : m_backbuffers) {
          D3DTexture old = m_textures.take (t);
          m_rtv_pool.free.push_back (old.rtv);
        }
        m_width = width;
        m_height = height;
        check (m_swapchain->ResizeBuffers (frames_in_flight, width, height,
                                           dxgi_format (m_surface_format),
                                           0),
               "ResizeBuffers");
        adopt_backbuffers ();
      }

      // -- a frame ----------------------------------------------------

      bool begin_frame () override {
        const std::uint64_t next = m_serial + 1;
        const std::uint32_t slot = next % frames_in_flight;
        step ("begin_frame: waiting for the slot's frame");
        wait_for (m_slot_fence[slot]);
        step ("begin_frame: collecting");
        collect ();
        m_serial = next;
        m_slot = slot;
        check (m_allocators[slot]->Reset (), "reset allocator");
        check (m_list->Reset (m_allocators[slot].Get (), nullptr),
               "reset list");
        m_arena = { static_cast<std::byte*> (m_arena_data[slot]),
                    m_arena_buffers[slot]->GetGPUVirtualAddress (),
                    arena_capacity, 0 };
        m_ring_next = slot * (ring_descriptors / frames_in_flight);
        m_ring_end = m_ring_next + ring_descriptors / frames_in_flight;
        ID3D12DescriptorHeap* heaps[] = { m_ring.Get () };
        m_list->SetDescriptorHeaps (1, heaps);
        m_current_backbuffer =
          m_backbuffers[m_swapchain->GetCurrentBackBufferIndex ()];
        step ("recording");
        FrameTiming& timing = m_timing[slot];
        timing.serial = m_serial;
        timing.labels.clear ();
        timing.count = 0;
        timing.resolved = false;
        return true;
      }

      Texture backbuffer () override { return m_current_backbuffer; }

      Transient allocate (std::uint64_t size, std::uint64_t alignment)
        override {
        if (!m_arena.fits (size, alignment))
          throw std::runtime_error ("NHAL: frame arena exhausted");
        return m_arena.allocate (size, std::max<std::uint64_t> (alignment,
                                                                256));
      }

      void begin_render_pass (const RenderPassDesc& desc) override {
        timestamp_pass (desc.label ? desc.label : "render");
        m_pass = desc;
        std::array<D3D12_CPU_DESCRIPTOR_HANDLE, 8> rtvs {};
        std::uint32_t width = 0, height = 0;
        for (std::uint32_t i = 0; i < desc.color_count; ++i) {
          D3DTexture& t = m_textures[desc.colors[i].texture];
          transition (m_list.Get (), t, D3D12_RESOURCE_STATE_RENDER_TARGET);
          rtvs[i] = m_rtv_pool.cpu (t.rtv);
          width = t.desc.width;
          height = t.desc.height;
        }
        D3D12_CPU_DESCRIPTOR_HANDLE dsv {};
        if (desc.depth.texture) {
          D3DTexture& t = m_textures[desc.depth.texture];
          transition (m_list.Get (), t, D3D12_RESOURCE_STATE_DEPTH_WRITE);
          dsv = m_dsv_pool.cpu (t.dsv);
          width = t.desc.width;
          height = t.desc.height;
        }
        m_list->OMSetRenderTargets (desc.color_count, rtvs.data (), FALSE,
                                    desc.depth.texture ? &dsv : nullptr);
        for (std::uint32_t i = 0; i < desc.color_count; ++i)
          if (desc.colors[i].load == Load::clear)
            m_list->ClearRenderTargetView (rtvs[i],
                                           desc.colors[i].clear.data (), 0,
                                           nullptr);
        if (desc.depth.texture && desc.depth.load == Load::clear)
          m_list->ClearDepthStencilView (dsv, D3D12_CLEAR_FLAG_DEPTH,
                                         desc.depth.clear, 0, 0, nullptr);
        set_viewport (0, 0, float (width), float (height));
      }

      void end_render_pass () override {
        for (std::uint32_t i = 0; i < m_pass.color_count; ++i) {
          const ColorAttachment& color = m_pass.colors[i];
          if (!color.resolve)
            continue;
          D3DTexture& source = m_textures[color.texture];
          D3DTexture& target = m_textures[color.resolve];
          transition (m_list.Get (), source,
                      D3D12_RESOURCE_STATE_RESOLVE_SOURCE);
          transition (m_list.Get (), target,
                      D3D12_RESOURCE_STATE_RESOLVE_DEST);
          m_list->ResolveSubresource (target.resource.Get (), 0,
                                      source.resource.Get (), 0,
                                      dxgi_format (target.desc.format));
        }
        m_pass = {};
        timestamp ();
      }

      void set_pipeline (Pipeline handle) override {
        const D3DPipeline& pipeline = m_pipelines[handle];
        m_pipeline_name = pipeline.name;
        m_list->SetPipelineState (pipeline.state.Get ());
        if (m_layout != pipeline.layout
            || m_compute_bound != pipeline.compute) {
          m_layout = pipeline.layout;
          m_compute_bound = pipeline.compute;
          if (pipeline.compute)
            m_list->SetComputeRootSignature (m_layout->signature.Get ());
          else
            m_list->SetGraphicsRootSignature (m_layout->signature.Get ());
        }
        if (!pipeline.compute)
          m_list->IASetPrimitiveTopology (pipeline.topology);
      }

      void set_buffer (std::uint32_t binding, Buffer buffer,
                       std::uint64_t offset) override {
        m_bound_buffers.at (binding) = {
          buffer, m_buffers[buffer].resource->GetGPUVirtualAddress ()
                    + offset };
      }

      void set_buffer (std::uint32_t binding, const Transient& slice)
        override {
        m_bound_buffers.at (binding) = { Buffer {}, slice.gpu_address };
      }

      void set_texture (std::uint32_t binding, Texture texture) override {
        m_bound_textures.at (binding) = texture;
      }

      void set_storage_texture (std::uint32_t binding, Texture texture)
        override {
        m_bound_storage.at (binding) = texture;
      }

      void set_viewport (float x, float y, float width, float height)
        override {
        const D3D12_VIEWPORT viewport { x, y, width, height, 0, 1 };
        const D3D12_RECT scissor { LONG (x), LONG (y), LONG (x + width),
                                   LONG (y + height) };
        m_list->RSSetViewports (1, &viewport);
        m_list->RSSetScissorRects (1, &scissor);
      }

      void draw (std::uint32_t vertex_count, std::uint32_t instance_count,
                 std::uint32_t first_vertex, std::uint32_t first_instance)
        override {
        bind_arguments ();
        m_list->DrawInstanced (vertex_count, instance_count, first_vertex,
                               first_instance);
      }

      void draw_indexed (Buffer indices, IndexType type,
                         std::uint32_t index_count,
                         std::uint32_t instance_count,
                         std::uint32_t first_index, std::int32_t base_vertex,
                         std::uint32_t first_instance) override {
        bind_arguments ();
        set_index_buffer (indices, type);
        m_list->DrawIndexedInstanced (index_count, instance_count,
                                      first_index, base_vertex,
                                      first_instance);
      }

      void draw_indirect (Buffer arguments, std::uint64_t offset) override {
        bind_arguments ();
        D3DBuffer& args = m_buffers[arguments];
        buffer_state (args, D3D12_RESOURCE_STATE_INDIRECT_ARGUMENT);
        m_list->ExecuteIndirect (m_draw_signature.Get (), 1,
                                 args.resource.Get (), offset, nullptr, 0);
      }

      void draw_indexed_indirect (Buffer indices, IndexType type,
                                  Buffer arguments, std::uint64_t offset)
        override {
        bind_arguments ();
        set_index_buffer (indices, type);
        D3DBuffer& args = m_buffers[arguments];
        buffer_state (args, D3D12_RESOURCE_STATE_INDIRECT_ARGUMENT);
        m_list->ExecuteIndirect (m_draw_indexed_signature.Get (), 1,
                                 args.resource.Get (), offset, nullptr, 0);
      }

      void begin_compute_pass (const char* label) override {
        timestamp_pass (label ? label : "compute");
      }

      void end_compute_pass () override { timestamp (); }

      std::span<const PassTiming> pass_timings () const override {
        return m_pass_timings;
      }

      void dispatch (std::uint32_t x, std::uint32_t y, std::uint32_t z)
        override {
        bind_arguments ();
        m_list->Dispatch (x, y, z);
      }

      void dispatch_indirect (Buffer arguments, std::uint64_t offset)
        override {
        bind_arguments ();
        D3DBuffer& args = m_buffers[arguments];
        buffer_state (args, D3D12_RESOURCE_STATE_INDIRECT_ARGUMENT);
        m_list->ExecuteIndirect (m_dispatch_signature.Get (), 1,
                                 args.resource.Get (), offset, nullptr, 0);
      }

      void copy_to_buffer (Buffer target, std::uint64_t offset,
                           const Transient& source) override {
        D3DBuffer& b = m_buffers[target];
        buffer_state (b, D3D12_RESOURCE_STATE_COPY_DEST);
        m_list->CopyBufferRegion (
          b.resource.Get (), offset, m_arena_buffers[m_slot].Get (),
          source.gpu_address - m_arena_buffers[m_slot]->GetGPUVirtualAddress (),
          source.size);
      }

      void capture_frame (std::function<void (const Capture&)> done)
        override {
        m_capture_request = std::move (done);
      }

      void set_index_buffer (Buffer indices, IndexType type) {
        D3DBuffer& buffer = m_buffers[indices];
        buffer_state (buffer, D3D12_RESOURCE_STATE_INDEX_BUFFER);
        const D3D12_INDEX_BUFFER_VIEW view {
          buffer.resource->GetGPUVirtualAddress (),
          UINT (buffer.resource->GetDesc ().Width),
          type == IndexType::uint16 ? DXGI_FORMAT_R16_UINT
                                    : DXGI_FORMAT_R32_UINT };
        m_list->IASetIndexBuffer (&view);
      }

      void end_frame () override {
        D3DTexture& back = m_textures[m_current_backbuffer];
        if (m_capture_request)
          encode_capture (back);
        transition (m_list.Get (), back, D3D12_RESOURCE_STATE_PRESENT);
        if (const std::uint32_t count = m_timing[m_slot].count)
          m_list->ResolveQueryData (
            m_timestamps.Get (), D3D12_QUERY_TYPE_TIMESTAMP,
            m_slot * max_timestamps, count, m_timestamp_readback.Get (),
            8ull * m_slot * max_timestamps);
        step ("end_frame: closing");
        check (m_list->Close (), "close list");
        ID3D12CommandList* lists[] = { m_list.Get () };
        step ("end_frame: executing");
        m_queue->ExecuteCommandLists (1, lists);
        step ("end_frame: presenting");
        check (m_swapchain->Present (1, 0), "Present");
        observe_presentation ();
        step ("end_frame: signalling");
        check (m_queue->Signal (m_fence.Get (), m_serial), "Signal");
        step ("between frames");
        m_slot_fence[m_slot] = m_serial;
        m_layout = nullptr;
        m_compute_bound = false;
        m_bound_buffers.fill (BoundBuffer {});
        m_bound_textures.fill (Texture {});
        m_bound_storage.fill (Texture {});
      }

      void wait_idle () override {
        if (m_serial)
          wait_for (m_serial);
        collect ();
      }

      nhal::FrameTiming next_frame_timing () const override {
        return m_presentation.predict (m_serial + 1, steady_seconds ());
      }

    private:
      struct FrameTiming {
        std::uint64_t serial = 0;
        std::vector<std::string> labels;
        std::uint32_t count = 0;
        bool resolved = true;
      };

      // Which of our frames DXGI showed last, and at which vsync: each
      // Present's count names its frame, the statistics name the latest
      // count shown and its vsync's QPC time, and consecutive vsyncs give
      // the refresh period.
      void observe_presentation () {
        UINT count = 0;
        if (SUCCEEDED (m_swapchain->GetLastPresentCount (&count)))
          m_present_serials[count % m_present_serials.size ()] = {
            count, m_serial };
        DXGI_FRAME_STATISTICS stats {};
        if (FAILED (m_swapchain->GetFrameStatistics (&stats))
            || !stats.SyncQPCTime.QuadPart)
          return;
        LARGE_INTEGER now {};
        QueryPerformanceCounter (&now);
        const double frequency = double (m_qpc_frequency.QuadPart);
        const double offset = steady_seconds ()
                              - double (now.QuadPart) / frequency;
        const auto& known =
          m_present_serials[stats.PresentCount % m_present_serials.size ()];
        if (known.first == stats.PresentCount && known.second)
          m_presentation.presented (
            known.second,
            double (stats.SyncQPCTime.QuadPart) / frequency + offset);
        if (m_last_sync_refresh && stats.SyncRefreshCount > m_last_sync_refresh
            && stats.SyncQPCTime.QuadPart > m_last_sync_qpc)
          m_presentation.set_refresh (
            double (stats.SyncQPCTime.QuadPart - m_last_sync_qpc) / frequency
            / double (stats.SyncRefreshCount - m_last_sync_refresh));
        m_last_sync_refresh = stats.SyncRefreshCount;
        m_last_sync_qpc = stats.SyncQPCTime.QuadPart;
      }

      void timestamp_pass (const char* label) {
        FrameTiming& timing = m_timing[m_slot];
        if (timing.count + 2 > max_timestamps)
          return;
        timing.labels.emplace_back (label);
        timestamp ();
      }

      void timestamp () {
        FrameTiming& timing = m_timing[m_slot];
        if (timing.count >= max_timestamps
            || timing.count >= 2 * timing.labels.size ())
          return;
        m_list->EndQuery (m_timestamps.Get (), D3D12_QUERY_TYPE_TIMESTAMP,
                          m_slot * max_timestamps + timing.count);
        ++timing.count;
      }

      void resolve_timings (std::uint64_t completed) {
        for (std::uint32_t slot = 0; slot < frames_in_flight; ++slot) {
          FrameTiming& timing = m_timing[slot];
          if (timing.resolved || timing.serial > completed
              || timing.count < 2)
            continue;
          timing.resolved = true;
          const D3D12_RANGE range { 8ull * slot * max_timestamps,
                                    8ull * (slot * max_timestamps
                                            + timing.count) };
          void* mapped = nullptr;
          if (FAILED (m_timestamp_readback->Map (0, &range, &mapped)))
            continue;
          const auto* ticks = static_cast<const std::uint64_t*> (mapped)
                              + slot * max_timestamps;
          m_pass_timings.clear ();
          for (std::uint32_t i = 0; i + 1 < timing.count; i += 2)
            m_pass_timings.push_back (
              { timing.labels[i / 2],
                double (ticks[i + 1] - ticks[i]) / m_ticks_per_ms });
          const D3D12_RANGE none { 0, 0 };
          m_timestamp_readback->Unmap (0, &none);
        }
      }

      struct PendingCapture {
        std::uint64_t serial = 0;
        ComPtr<ID3D12Resource> readback;
        Capture capture;
        std::function<void (const Capture&)> done;
      };

      void adopt_backbuffers () {
        for (std::uint32_t i = 0; i < frames_in_flight; ++i) {
          D3DTexture texture;
          check (m_swapchain->GetBuffer (i, IID_PPV_ARGS (&texture.resource)),
                 "swapchain buffer");
          texture.desc = { m_width, m_height, m_surface_format,
                           usage_render_target, 1, "backbuffer" };
          texture.state = D3D12_RESOURCE_STATE_PRESENT;
          texture.rtv = m_rtv_pool.allocate ();
          m_device->CreateRenderTargetView (texture.resource.Get (), nullptr,
                                            m_rtv_pool.cpu (texture.rtv));
          m_backbuffers[i] = m_textures.insert<Texture> (std::move (texture));
        }
      }

      void make_views (D3DTexture& t) {
        const DXGI_FORMAT format = dxgi_format (t.desc.format);
        const bool multisampled = t.desc.samples > 1;
        if (t.desc.usage & usage_sampled) {
          t.srv = m_srv_pool.allocate ();
          D3D12_SHADER_RESOURCE_VIEW_DESC view {};
          view.Format = t.desc.format == Format::d32_float
                          ? DXGI_FORMAT_R32_FLOAT
                          : format;
          view.Shader4ComponentMapping =
            D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
          view.ViewDimension = multisampled
                                 ? D3D12_SRV_DIMENSION_TEXTURE2DMS
                                 : D3D12_SRV_DIMENSION_TEXTURE2D;
          view.Texture2D.MipLevels = 1;
          m_device->CreateShaderResourceView (t.resource.Get (), &view,
                                              m_srv_pool.cpu (t.srv));
        }
        if (t.desc.usage & usage_storage) {
          t.uav = m_srv_pool.allocate ();
          D3D12_UNORDERED_ACCESS_VIEW_DESC view {};
          view.Format = format;
          view.ViewDimension = D3D12_UAV_DIMENSION_TEXTURE2D;
          m_device->CreateUnorderedAccessView (t.resource.Get (), nullptr,
                                               &view, m_srv_pool.cpu (t.uav));
        }
        if (t.desc.usage & usage_render_target) {
          t.rtv = m_rtv_pool.allocate ();
          m_device->CreateRenderTargetView (t.resource.Get (), nullptr,
                                            m_rtv_pool.cpu (t.rtv));
        }
        if (t.desc.usage & usage_depth) {
          t.dsv = m_dsv_pool.allocate ();
          D3D12_DEPTH_STENCIL_VIEW_DESC view {};
          view.Format = format;
          view.ViewDimension = multisampled
                                 ? D3D12_DSV_DIMENSION_TEXTURE2DMS
                                 : D3D12_DSV_DIMENSION_TEXTURE2D;
          m_device->CreateDepthStencilView (t.resource.Get (), &view,
                                            m_dsv_pool.cpu (t.dsv));
        }
      }

      // The root signature for a program, made once from its reflection.
      const RootLayout& root_layout (const Program& program) {
        std::unique_ptr<RootLayout>& cached = m_layouts[&program];
        if (cached)
          return *cached;
        auto layout = std::make_unique<RootLayout> ();
        layout->buffer_parameter.fill (-1);
        std::vector<D3D12_ROOT_PARAMETER> parameters;
        std::vector<D3D12_STATIC_SAMPLER_DESC> samplers;
        for (const Resource& r : program.resources) {
          if (is_buffer (r.kind)) {
            D3D12_ROOT_PARAMETER p {};
            RootBuffer kind = RootBuffer::read;
            p.ParameterType = D3D12_ROOT_PARAMETER_TYPE_SRV;
            if (r.kind == ResourceKind::uniform_block) {
              kind = RootBuffer::constants;
              p.ParameterType = D3D12_ROOT_PARAMETER_TYPE_CBV;
            } else if (r.kind == ResourceKind::read_write_storage_buffer) {
              kind = RootBuffer::write;
              p.ParameterType = D3D12_ROOT_PARAMETER_TYPE_UAV;
            }
            p.Descriptor.ShaderRegister = r.binding;
            p.ShaderVisibility = D3D12_SHADER_VISIBILITY_ALL;
            layout->buffer_parameter.at (r.binding) = int (parameters.size ());
            layout->buffer_kind.at (r.binding) = kind;
            parameters.push_back (p);
          } else if (is_storage_texture (r.kind)) {
            layout->storage_count =
              std::max (layout->storage_count, r.binding + 1);
          } else if (is_texture (r.kind)) {
            layout->texture_count =
              std::max (layout->texture_count, r.binding + 1);
          } else if (is_sampler (r.kind)) {
            samplers.push_back (standard_sampler (
              r.binding, r.kind == ResourceKind::comparison_sampler));
          }
        }
        D3D12_DESCRIPTOR_RANGE storage_range {};
        if (layout->storage_count) {
          storage_range.RangeType = D3D12_DESCRIPTOR_RANGE_TYPE_UAV;
          storage_range.NumDescriptors = layout->storage_count;
          storage_range.RegisterSpace = 1;
          D3D12_ROOT_PARAMETER p {};
          p.ParameterType = D3D12_ROOT_PARAMETER_TYPE_DESCRIPTOR_TABLE;
          p.DescriptorTable = { 1, &storage_range };
          p.ShaderVisibility = D3D12_SHADER_VISIBILITY_ALL;
          layout->storage_parameter = int (parameters.size ());
          parameters.push_back (p);
        }
        D3D12_DESCRIPTOR_RANGE range {};
        if (layout->texture_count) {
          range.RangeType = D3D12_DESCRIPTOR_RANGE_TYPE_SRV;
          range.NumDescriptors = layout->texture_count;
          range.RegisterSpace = 1;
          D3D12_ROOT_PARAMETER p {};
          p.ParameterType = D3D12_ROOT_PARAMETER_TYPE_DESCRIPTOR_TABLE;
          p.DescriptorTable = { 1, &range };
          p.ShaderVisibility = D3D12_SHADER_VISIBILITY_ALL;
          layout->texture_parameter = int (parameters.size ());
          parameters.push_back (p);
        }
        D3D12_ROOT_SIGNATURE_DESC desc {
          UINT (parameters.size ()), parameters.data (),
          UINT (samplers.size ()), samplers.data (),
          D3D12_ROOT_SIGNATURE_FLAG_NONE };
        ComPtr<ID3DBlob> blob, error;
        if (FAILED (D3D12SerializeRootSignature (
              &desc, D3D_ROOT_SIGNATURE_VERSION_1, &blob, &error)))
          throw std::runtime_error (
            std::string ("NHAL: root signature for ") + program.name + ": "
            + (error ? static_cast<const char*> (error->GetBufferPointer ())
                     : "?"));
        check (m_device->CreateRootSignature (0, blob->GetBufferPointer (),
                                              blob->GetBufferSize (),
                                              IID_PPV_ARGS (
                                                &layout->signature)),
               "root signature");
        cached = std::move (layout);
        return *cached;
      }

      // Root arguments for the next draw or dispatch, with the barriers
      // its buffers and textures need first.
      void bind_arguments () {
        const bool compute = m_compute_bound;
        for (std::uint32_t b = 0; b < max_bindings; ++b) {
          const int parameter = m_layout->buffer_parameter[b];
          if (parameter < 0)
            continue;
          const BoundBuffer& bound = m_bound_buffers[b];
          const RootBuffer kind = m_layout->buffer_kind[b];
          if (bound.buffer && kind != RootBuffer::constants)
            buffer_state (m_buffers[bound.buffer],
                          kind == RootBuffer::write ? state_write
                                                    : state_read);
          switch (kind) {
          case RootBuffer::constants:
            compute ? m_list->SetComputeRootConstantBufferView (parameter,
                                                                bound.address)
                    : m_list->SetGraphicsRootConstantBufferView (
                        parameter, bound.address);
            break;
          case RootBuffer::read:
            compute ? m_list->SetComputeRootShaderResourceView (parameter,
                                                                bound.address)
                    : m_list->SetGraphicsRootShaderResourceView (
                        parameter, bound.address);
            break;
          case RootBuffer::write:
            compute ? m_list->SetComputeRootUnorderedAccessView (parameter,
                                                                 bound.address)
                    : m_list->SetGraphicsRootUnorderedAccessView (
                        parameter, bound.address);
            break;
          case RootBuffer::none: break;
          }
        }
        if (m_layout->texture_parameter >= 0)
          bind_table (m_layout->texture_parameter, m_layout->texture_count,
                      m_bound_textures, false);
        if (m_layout->storage_parameter >= 0)
          bind_table (m_layout->storage_parameter, m_layout->storage_count,
                      m_bound_storage, true);
      }

      // Copies a table's descriptors into the frame's ring and binds it.
      void bind_table (int parameter, std::uint32_t count,
                       const std::array<Texture, max_bindings>& bound,
                       bool storage) {
        if (m_ring_next + count > m_ring_end)
          throw std::runtime_error ("NHAL: descriptor ring exhausted");
        const std::uint32_t first = m_ring_next;
        m_ring_next += count;
        D3D12_CPU_DESCRIPTOR_HANDLE to =
          m_ring->GetCPUDescriptorHandleForHeapStart ();
        to.ptr += SIZE_T (first) * m_ring_step;
        for (std::uint32_t i = 0; i < count; ++i) {
          std::uint32_t source = storage ? m_null_uav : m_null_srv;
          if (const Texture handle = bound[i]) {
            D3DTexture& t = m_textures[handle];
            if (storage) {
              if (t.state == D3D12_RESOURCE_STATE_UNORDERED_ACCESS)
                uav_barrier (t.resource.Get ());
              transition (m_list.Get (), t,
                          D3D12_RESOURCE_STATE_UNORDERED_ACCESS);
              source = t.uav;
            } else if (attached (handle)) {
              // A texture the open pass renders into cannot be read: the
              // transition would pull it out of its attachment state
              // mid-pass. The slot reads as null instead.
              if (!t.warned) {
                t.warned = true;
                std::cerr << "NHAL: " << m_pipeline_name
                          << " binds attachment "
                          << (t.desc.label ? t.desc.label : "(unnamed)")
                          << " at texture " << i << std::endl;
              }
            } else {
              transition (m_list.Get (), t, state_read);
              source = t.srv;
            }
          }
          D3D12_CPU_DESCRIPTOR_HANDLE slot = to;
          slot.ptr += SIZE_T (i) * m_ring_step;
          m_device->CopyDescriptorsSimple (
            1, slot, m_srv_pool.cpu (source),
            D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV);
        }
        D3D12_GPU_DESCRIPTOR_HANDLE table =
          m_ring->GetGPUDescriptorHandleForHeapStart ();
        table.ptr += UINT64 (first) * m_ring_step;
        if (m_compute_bound)
          m_list->SetComputeRootDescriptorTable (parameter, table);
        else
          m_list->SetGraphicsRootDescriptorTable (parameter, table);
      }

      bool attached (Texture handle) const {
        for (std::uint32_t i = 0; i < m_pass.color_count; ++i)
          if (m_pass.colors[i].texture == handle)
            return true;
        return m_pass.depth.texture == handle;
      }

      static constexpr D3D12_RESOURCE_STATES state_read =
        D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE
        | D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE;
      static constexpr D3D12_RESOURCE_STATES state_write =
        D3D12_RESOURCE_STATE_UNORDERED_ACCESS;

      // Moves a device buffer into `state` for the next command. Upload
      // buffers stay GENERIC_READ. Writes after writes get a UAV barrier
      // so successive dispatches see each other's results.
      void buffer_state (D3DBuffer& b, D3D12_RESOURCE_STATES state) {
        if (b.mapped)
          return;
        const D3D12_RESOURCE_STATES current =
          b.state_serial == m_serial ? b.state : D3D12_RESOURCE_STATE_COMMON;
        if (current == state) {
          if (state == state_write)
            uav_barrier (b.resource.Get ());
        } else {
          D3D12_RESOURCE_BARRIER barrier {};
          barrier.Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
          barrier.Transition.pResource = b.resource.Get ();
          barrier.Transition.Subresource =
            D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;
          barrier.Transition.StateBefore = current;
          barrier.Transition.StateAfter = state;
          m_list->ResourceBarrier (1, &barrier);
        }
        b.state = state;
        b.state_serial = m_serial;
      }

      void uav_barrier (ID3D12Resource* resource) {
        D3D12_RESOURCE_BARRIER barrier {};
        barrier.Type = D3D12_RESOURCE_BARRIER_TYPE_UAV;
        barrier.UAV.pResource = resource;
        m_list->ResourceBarrier (1, &barrier);
      }

      void transition (ID3D12GraphicsCommandList* list, D3DTexture& t,
                       D3D12_RESOURCE_STATES state) {
        if (t.state == state)
          return;
        D3D12_RESOURCE_BARRIER barrier {};
        barrier.Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
        barrier.Transition.pResource = t.resource.Get ();
        barrier.Transition.Subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;
        barrier.Transition.StateBefore = t.state;
        barrier.Transition.StateAfter = state;
        list->ResourceBarrier (1, &barrier);
        t.state = state;
      }

      void encode_capture (D3DTexture& back) {
        const auto rd = back.resource->GetDesc ();
        PendingCapture pending;
        D3D12_PLACED_SUBRESOURCE_FOOTPRINT footprint {};
        UINT64 total = 0;
        m_device->GetCopyableFootprints (&rd, 0, 1, 0, &footprint, nullptr,
                                         nullptr, &total);
        const auto readback = heap (D3D12_HEAP_TYPE_READBACK);
        const auto desc = buffer_desc (total);
        check (m_device->CreateCommittedResource (
                 &readback, D3D12_HEAP_FLAG_NONE, &desc,
                 D3D12_RESOURCE_STATE_COPY_DEST, nullptr,
                 IID_PPV_ARGS (&pending.readback)),
               "capture buffer");
        transition (m_list.Get (), back, D3D12_RESOURCE_STATE_COPY_SOURCE);
        D3D12_TEXTURE_COPY_LOCATION to {};
        to.pResource = pending.readback.Get ();
        to.Type = D3D12_TEXTURE_COPY_TYPE_PLACED_FOOTPRINT;
        to.PlacedFootprint = footprint;
        D3D12_TEXTURE_COPY_LOCATION from {};
        from.pResource = back.resource.Get ();
        from.Type = D3D12_TEXTURE_COPY_TYPE_SUBRESOURCE_INDEX;
        m_list->CopyTextureRegion (&to, 0, 0, 0, &from, nullptr);
        pending.serial = m_serial;
        pending.capture = { back.desc.width, back.desc.height,
                            back.desc.format, footprint.Footprint.RowPitch,
                            {} };
        pending.done = std::move (m_capture_request);
        m_capture_request = nullptr;
        m_captures.push_back (std::move (pending));
      }

      ComPtr<ID3D12Resource> staging_buffer (std::span<const std::byte> data) {
        ComPtr<ID3D12Resource> staging;
        const auto upload = heap (D3D12_HEAP_TYPE_UPLOAD);
        const auto desc = buffer_desc (data.size ());
        check (m_device->CreateCommittedResource (
                 &upload, D3D12_HEAP_FLAG_NONE, &desc,
                 D3D12_RESOURCE_STATE_GENERIC_READ, nullptr,
                 IID_PPV_ARGS (&staging)),
               "staging buffer");
        void* mapped = nullptr;
        check (staging->Map (0, nullptr, &mapped), "map staging");
        std::memcpy (mapped, data.data (), data.size ());
        staging->Unmap (0, nullptr);
        return staging;
      }

      // Setup copies run on their own list and finish before returning.
      void begin_setup () {
        check (m_setup_allocator->Reset (), "reset setup allocator");
        check (m_setup->Reset (m_setup_allocator.Get (), nullptr),
               "reset setup list");
      }

      void finish_setup () {
        check (m_setup->Close (), "close setup list");
        ID3D12CommandList* lists[] = { m_setup.Get () };
        m_queue->ExecuteCommandLists (1, lists);
        const std::uint64_t value = ++m_setup_serial;
        check (m_queue->Signal (m_setup_fence.Get (), value), "setup signal");
        if (m_setup_fence->GetCompletedValue () < value) {
          m_setup_fence->SetEventOnCompletion (value, m_event);
          WaitForSingleObjectEx (m_event, INFINITE, FALSE);
        }
      }

      // A frame that never completes is a hung GPU; say which, and why.
      void wait_for (std::uint64_t value) {
        if (m_fence->GetCompletedValue () >= value)
          return;
        awaited_value = value;
        check (m_fence->SetEventOnCompletion (value, m_event),
               "SetEventOnCompletion");
        if (WaitForSingleObjectEx (m_event, 5000, FALSE) == WAIT_OBJECT_0)
          return;
        char message[160];
        std::snprintf (message, sizeof message,
                       "NHAL: frame %llu did not complete (fence at %llu, "
                       "device removed reason 0x%08lx)",
                       static_cast<unsigned long long> (value),
                       static_cast<unsigned long long> (
                         m_fence->GetCompletedValue ()),
                       static_cast<unsigned long> (
                         m_device->GetDeviceRemovedReason ()));
        throw std::runtime_error (message);
      }

      void collect () {
        const std::uint64_t completed = m_fence->GetCompletedValue ();
        m_retired.collect (completed, [&] (Retired& r) {
          if (r.srv != no_slot)
            m_srv_pool.free.push_back (r.srv);
          if (r.uav != no_slot)
            m_srv_pool.free.push_back (r.uav);
          if (r.rtv != no_slot)
            m_rtv_pool.free.push_back (r.rtv);
          if (r.dsv != no_slot)
            m_dsv_pool.free.push_back (r.dsv);
        });
        resolve_timings (completed);
        for (auto it = m_captures.begin (); it != m_captures.end ();) {
          if (it->serial > completed) {
            ++it;
            continue;
          }
          void* mapped = nullptr;
          const std::size_t size = std::size_t (it->capture.row_bytes)
                                   * it->capture.height;
          if (SUCCEEDED (it->readback->Map (0, nullptr, &mapped))) {
            it->capture.pixels = { static_cast<const std::byte*> (mapped),
                                   size };
            it->done (it->capture);
            it->readback->Unmap (0, nullptr);
          }
          it = m_captures.erase (it);
        }
      }

      static void name (ID3D12Object* object, const char* label) {
        if (!object || !label)
          return;
        wchar_t wide[128] {};
        MultiByteToWideChar (CP_UTF8, 0, label, -1, wide, 127);
        object->SetName (wide);
      }

      Format m_surface_format;
      std::uint32_t m_width, m_height;
      std::string m_adapter;
      ComPtr<ID3D12Device> m_device;
      ComPtr<ID3D12CommandQueue> m_queue;
      ComPtr<ID3D12Fence> m_fence;
      ComPtr<ID3D12Fence> m_setup_fence;
      std::uint64_t m_setup_serial = 0;
      HANDLE m_event = nullptr;
      std::array<ComPtr<ID3D12CommandAllocator>, frames_in_flight>
        m_allocators;
      std::array<ComPtr<ID3D12Resource>, frames_in_flight> m_arena_buffers;
      std::array<void*, frames_in_flight> m_arena_data {};
      std::array<std::uint64_t, frames_in_flight> m_slot_fence {};
      ComPtr<ID3D12GraphicsCommandList> m_list;
      ComPtr<ID3D12CommandAllocator> m_setup_allocator;
      ComPtr<ID3D12GraphicsCommandList> m_setup;
      ComPtr<IDXGISwapChain3> m_swapchain;
      std::array<Texture, frames_in_flight> m_backbuffers;

      DescriptorPool m_srv_pool, m_rtv_pool, m_dsv_pool;
      ComPtr<ID3D12DescriptorHeap> m_ring;
      UINT m_ring_step = 0;
      std::uint32_t m_ring_next = 0, m_ring_end = 0;
      std::uint32_t m_null_srv = 0;

      Table<D3DBuffer> m_buffers;
      Table<D3DTexture> m_textures;
      Table<D3DPipeline> m_pipelines;
      Retirement<Retired> m_retired;
      std::unordered_map<const Program*, std::unique_ptr<RootLayout>>
        m_layouts;

      std::uint64_t m_serial = 0;
      std::uint32_t m_slot = 0;
      Arena m_arena;
      Texture m_current_backbuffer;
      RenderPassDesc m_pass;
      const RootLayout* m_layout = nullptr;
      const char* m_pipeline_name = "";
      std::array<BoundBuffer, max_bindings> m_bound_buffers {};
      std::array<Texture, max_bindings> m_bound_textures {};
      std::array<Texture, max_bindings> m_bound_storage {};
      bool m_compute_bound = false;
      std::uint32_t m_null_uav = 0;
      ComPtr<ID3D12CommandSignature> m_draw_signature;
      ComPtr<ID3D12CommandSignature> m_draw_indexed_signature;
      ComPtr<ID3D12CommandSignature> m_dispatch_signature;
      ComPtr<ID3D12QueryHeap> m_timestamps;
      ComPtr<ID3D12Resource> m_timestamp_readback;
      std::array<FrameTiming, frames_in_flight> m_timing;
      std::vector<PassTiming> m_pass_timings;
      double m_ticks_per_ms = 1e6;
      PresentationClock m_presentation;
      std::array<std::pair<UINT, std::uint64_t>, 16> m_present_serials {};
      LARGE_INTEGER m_qpc_frequency = [] {
        LARGE_INTEGER f {};
        QueryPerformanceFrequency (&f);
        return f;
      }();
      UINT m_last_sync_refresh = 0;
      LONGLONG m_last_sync_qpc = 0;
      std::function<void (const Capture&)> m_capture_request;
      std::vector<PendingCapture> m_captures;
    };
  }

  std::unique_ptr<Device> create_d3d12_device (IUnknown* window,
                                               std::uint32_t width,
                                               std::uint32_t height,
                                               Format surface_format) {
    return std::make_unique<D3D12Device> (window, width, height,
                                          surface_format);
  }

  std::string d3d12_device_step () {
    std::string text = current_step.load (std::memory_order_relaxed);
    ID3D12Fence* fence = watched_fence;
    ID3D12Device* device = watched_device;
    if (fence && device) {
      char detail[160];
      std::snprintf (detail, sizeof detail,
                     " (awaiting frame %llu, GPU completed %llu, device "
                     "removed reason 0x%08lx)",
                     static_cast<unsigned long long> (awaited_value.load ()),
                     static_cast<unsigned long long> (
                       fence->GetCompletedValue ()),
                     static_cast<unsigned long> (
                       device->GetDeviceRemovedReason ()));
      text += detail;
    }
    return text;
  }
}
