// NHAL on Vulkan 1.3: dynamic rendering, synchronization2, a timeline
// semaphore counting frames, and push descriptors.
//
// luv-shaderc folds the binding families into descriptor set 0 (buffers
// from binding 0, textures from 16, storage textures from 32, samplers from
// 48), so each program's reflection becomes one push-descriptor set layout
// whose samplers are the standard set, immutable. Every draw and dispatch
// pushes the bindings it sees; slots left unbound are null descriptors.
//
// Barriers are coarse and correct: a full memory barrier precedes every
// render pass, dispatch, and copy, carrying whatever image layout changes
// the command needs. Image layouts must be settled before a pass begins,
// but a pass learns which textures its draws sample only as they come, so
// each render pass records into a secondary command buffer, and the
// primary takes the barriers, then begins rendering and executes it.
//
// Where the surface reports present timing (VK_EXT_present_timing with
// VK_KHR_present_id2), every present carries its frame's serial and asks
// when its first pixel became visible; the answers, in CLOCK_MONOTONIC,
// predict when the next frame will be (presentation.hh).
#include <moppe/environment.hh>
#include <moppe/nhal/presentation.hh>
#include <moppe/nhal/table.hh>
#include <moppe/nhal/vulkan/vulkan_device.hh>

#include <algorithm>
#include <array>
#include <cstdio>
#include <cstring>
#include <iostream>
#include <limits>
#include <stdexcept>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <vector>

#include <time.h>

namespace moppe::nhal {
  namespace {
    constexpr std::uint32_t frames_in_flight = 3;
    constexpr std::uint64_t arena_capacity = 16u << 20;
    constexpr std::uint32_t max_bindings = 16;
    constexpr std::uint32_t max_timestamps = 64;
    // luv-shaderc's bases for each family within set 0.
    constexpr std::uint32_t texture_base = 16;
    constexpr std::uint32_t storage_texture_base = 32;
    constexpr std::uint32_t sampler_base = 48;
    constexpr std::uint32_t standard_samplers = 4;
    // Transient addresses are the arena's offsets above a tag, so a slice
    // finds its buffer and offset again when it is bound.
    constexpr std::uint64_t arena_tag = 1ull << 48;

    void check (VkResult result, const char* what) {
      if (result != VK_SUCCESS) {
        char message[128];
        std::snprintf (message, sizeof message, "NHAL: %s failed: %d", what,
                       int (result));
        throw std::runtime_error (message);
      }
    }

    VkFormat vk_format (Format format) {
      switch (format) {
      case Format::undefined: return VK_FORMAT_UNDEFINED;
      case Format::rgba8_unorm: return VK_FORMAT_R8G8B8A8_UNORM;
      case Format::rgba8_unorm_srgb: return VK_FORMAT_R8G8B8A8_SRGB;
      case Format::bgra8_unorm: return VK_FORMAT_B8G8R8A8_UNORM;
      case Format::bgra8_unorm_srgb: return VK_FORMAT_B8G8R8A8_SRGB;
      case Format::rgb10a2_unorm: return VK_FORMAT_A2B10G10R10_UNORM_PACK32;
      case Format::rgba16_float: return VK_FORMAT_R16G16B16A16_SFLOAT;
      case Format::rg16_float: return VK_FORMAT_R16G16_SFLOAT;
      case Format::rg16_snorm: return VK_FORMAT_R16G16_SNORM;
      case Format::r16_float: return VK_FORMAT_R16_SFLOAT;
      case Format::r8_unorm: return VK_FORMAT_R8_UNORM;
      case Format::r32_float: return VK_FORMAT_R32_SFLOAT;
      case Format::rg32_float: return VK_FORMAT_R32G32_SFLOAT;
      case Format::rgba32_float: return VK_FORMAT_R32G32B32A32_SFLOAT;
      case Format::r32_uint: return VK_FORMAT_R32_UINT;
      case Format::d32_float: return VK_FORMAT_D32_SFLOAT;
      }
      return VK_FORMAT_UNDEFINED;
    }

    VkCompareOp vk_compare (CompareOp op) {
      return static_cast<VkCompareOp> (op);
    }

    VkSampleCountFlagBits vk_samples (std::uint32_t samples) {
      return static_cast<VkSampleCountFlagBits> (std::max (samples, 1u));
    }

    VkDescriptorType descriptor_type (ResourceKind kind) {
      switch (kind) {
      case ResourceKind::uniform_block:
        return VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
      case ResourceKind::storage_buffer:
      case ResourceKind::read_write_storage_buffer:
        return VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
      case ResourceKind::read_write_texture_2d:
        return VK_DESCRIPTOR_TYPE_STORAGE_IMAGE;
      case ResourceKind::sampler:
      case ResourceKind::comparison_sampler:
        return VK_DESCRIPTOR_TYPE_SAMPLER;
      default: return VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE;
      }
    }

    std::uint32_t vk_binding (const Resource& r) {
      if (is_texture (r.kind))
        return texture_base + r.binding;
      if (is_storage_texture (r.kind))
        return storage_texture_base + r.binding;
      if (is_sampler (r.kind))
        return sampler_base + r.binding;
      return r.binding;
    }

    struct VkBufferRow {
      VkBuffer buffer = VK_NULL_HANDLE;
      VkDeviceMemory memory = VK_NULL_HANDLE;
      VkDeviceSize size = 0;
      void* mapped = nullptr;
    };

    struct VkTextureRow {
      VkImage image = VK_NULL_HANDLE;
      VkImageView view = VK_NULL_HANDLE;
      VkDeviceMemory memory = VK_NULL_HANDLE;
      TextureDesc desc {};
      VkImageLayout layout = VK_IMAGE_LAYOUT_UNDEFINED;
      bool swapchain = false;
      bool warned = false;
    };

    // A program's push-descriptor layout, made once from its reflection.
    struct Layout {
      VkDescriptorSetLayout set = VK_NULL_HANDLE;
      VkPipelineLayout pipeline = VK_NULL_HANDLE;
      std::vector<Resource> resources; // the pushed ones, samplers aside
    };

    struct VkPipelineRow {
      VkPipeline pipeline = VK_NULL_HANDLE;
      const Layout* layout = nullptr;
      bool compute = false;
      const char* name = "";
    };

    // What a destroyed object leaves for the device to release once the
    // frames that may use it are done.
    struct Retired {
      VkBuffer buffer = VK_NULL_HANDLE;
      VkImage image = VK_NULL_HANDLE;
      VkImageView view = VK_NULL_HANDLE;
      VkDeviceMemory memory = VK_NULL_HANDLE;
      VkPipeline pipeline = VK_NULL_HANDLE;
    };

    struct TextureHash {
      std::size_t operator() (Texture t) const {
        return std::hash<std::uint64_t> {}(
          (std::uint64_t (t.generation) << 32) | t.index);
      }
    };

    // A binding: a device buffer or an arena slice, as buffer and range.
    struct BoundBuffer {
      VkBuffer buffer = VK_NULL_HANDLE;
      VkDeviceSize offset = 0;
      VkDeviceSize range = VK_WHOLE_SIZE;
    };

    class VulkanDevice final : public Device {
    public:
      VulkanDevice (const VulkanSurface& surface, std::uint32_t width,
                    std::uint32_t height, Format surface_format)
        : m_surface_format (surface_format), m_width (width),
          m_height (height) {
        create_instance (surface);
        if (surface.create)
          m_surface = surface.create (m_instance);
        choose_physical_device ();
        create_device ();
        create_frames ();
        create_samplers ();
        if (m_surface)
          create_swapchain ();
        else
          create_offscreen_backbuffers ();
      }

      ~VulkanDevice () override {
        if (!m_device)
          return;
        vkDeviceWaitIdle (m_device);
        collect_all ();
        destroy_backbuffers ();
        // What the host never destroyed goes with the device.
        m_pipelines.each ([&] (VkPipelineRow& p) {
          vkDestroyPipeline (m_device, p.pipeline, nullptr);
        });
        m_buffers.each ([&] (VkBufferRow& b) { release (b); });
        m_textures.each ([&] (VkTextureRow& t) {
          Retired r;
          r.image = t.image;
          r.view = t.view;
          r.memory = t.memory;
          release (r);
        });
        if (m_swapchain)
          vkDestroySwapchainKHR (m_device, m_swapchain, nullptr);
        for (VkSemaphore s : m_render_done)
          vkDestroySemaphore (m_device, s, nullptr);
        for (auto& frame : m_frames) {
          vkDestroyCommandPool (m_device, frame.pool, nullptr);
          vkDestroySemaphore (m_device, frame.acquired, nullptr);
          vkDestroyQueryPool (m_device, frame.queries, nullptr);
          release (frame.arena);
        }
        vkDestroyCommandPool (m_device, m_setup_pool, nullptr);
        vkDestroyFence (m_device, m_setup_fence, nullptr);
        vkDestroySemaphore (m_device, m_timeline, nullptr);
        for (auto& [program, layout] : m_layouts) {
          vkDestroyPipelineLayout (m_device, layout->pipeline, nullptr);
          vkDestroyDescriptorSetLayout (m_device, layout->set, nullptr);
        }
        for (VkSampler sampler : m_samplers)
          vkDestroySampler (m_device, sampler, nullptr);
        vkDestroyDevice (m_device, nullptr);
        if (m_surface)
          vkDestroySurfaceKHR (m_instance, m_surface, nullptr);
        if (m_messenger) {
          auto destroy = reinterpret_cast<PFN_vkDestroyDebugUtilsMessengerEXT> (
            vkGetInstanceProcAddr (m_instance,
                                   "vkDestroyDebugUtilsMessengerEXT"));
          if (destroy)
            destroy (m_instance, m_messenger, nullptr);
        }
        vkDestroyInstance (m_instance, nullptr);
      }

      DeviceInfo info () const override {
        return { "Vulkan", m_adapter, frames_in_flight };
      }

      // -- resources --------------------------------------------------

      Buffer create_buffer (const BufferDesc& desc,
                            std::span<const std::byte> initial) override {
        const bool upload = desc.memory == Memory::upload;
        VkBufferRow row = make_buffer (
          std::max<std::uint64_t> (desc.size, 16),
          VK_BUFFER_USAGE_STORAGE_BUFFER_BIT
            | VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT
            | VK_BUFFER_USAGE_INDEX_BUFFER_BIT
            | VK_BUFFER_USAGE_INDIRECT_BUFFER_BIT
            | VK_BUFFER_USAGE_TRANSFER_SRC_BIT
            | VK_BUFFER_USAGE_TRANSFER_DST_BIT,
          upload ? Placement::upload : Placement::device);
        if (!initial.empty ()) {
          if (upload) {
            std::memcpy (row.mapped, initial.data (), initial.size ());
          } else {
            VkBufferRow staging = staging_buffer (initial);
            VkCommandBuffer cb = begin_setup ();
            const VkBufferCopy region { 0, 0, initial.size () };
            vkCmdCopyBuffer (cb, staging.buffer, row.buffer, 1, &region);
            finish_setup ();
            release (staging);
          }
        }
        name (VK_OBJECT_TYPE_BUFFER, std::uint64_t (row.buffer), desc.label);
        return m_buffers.insert<Buffer> (row);
      }

      void* contents (Buffer buffer) override {
        return m_buffers[buffer].mapped;
      }

      Texture create_texture (const TextureDesc& desc) override {
        VkTextureRow t;
        t.desc = desc;
        const bool depth = (desc.usage & usage_depth) != 0;
        VkImageCreateInfo info { VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO };
        info.imageType = VK_IMAGE_TYPE_2D;
        info.format = vk_format (desc.format);
        info.extent = { desc.width, desc.height, 1 };
        info.mipLevels = 1;
        info.arrayLayers = 1;
        info.samples = vk_samples (desc.samples);
        info.tiling = VK_IMAGE_TILING_OPTIMAL;
        info.usage = VK_IMAGE_USAGE_TRANSFER_SRC_BIT
                     | VK_IMAGE_USAGE_TRANSFER_DST_BIT;
        if (desc.usage & usage_sampled)
          info.usage |= VK_IMAGE_USAGE_SAMPLED_BIT;
        if (desc.usage & usage_render_target)
          info.usage |= VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT;
        if (depth)
          info.usage |= VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT;
        if (desc.usage & usage_storage)
          info.usage |= VK_IMAGE_USAGE_STORAGE_BIT;
        info.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
        check (vkCreateImage (m_device, &info, nullptr, &t.image), "image");
        VkMemoryRequirements needs;
        vkGetImageMemoryRequirements (m_device, t.image, &needs);
        t.memory = allocate_memory (needs, Placement::device);
        check (vkBindImageMemory (m_device, t.image, t.memory, 0),
               "bind image memory");
        t.view = make_view (t.image, info.format, depth);
        name (VK_OBJECT_TYPE_IMAGE, std::uint64_t (t.image), desc.label);
        return m_textures.insert<Texture> (t);
      }

      void write_texture (Texture handle, std::span<const std::byte> pixels,
                          std::uint32_t row_bytes) override {
        VkTextureRow& t = m_textures[handle];
        const std::uint32_t texel = bytes_per_pixel (t.desc.format);
        if (!row_bytes)
          row_bytes = t.desc.width * texel;
        VkBufferRow staging = staging_buffer (
          pixels.first (std::min<std::size_t> (
            pixels.size (), std::size_t (row_bytes) * t.desc.height)));
        VkCommandBuffer cb = begin_setup ();
        barrier (cb, { { &t, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
                             true } });
        VkBufferImageCopy region {};
        region.bufferRowLength = row_bytes / texel;
        region.imageSubresource = { VK_IMAGE_ASPECT_COLOR_BIT, 0, 0, 1 };
        region.imageExtent = { t.desc.width, t.desc.height, 1 };
        vkCmdCopyBufferToImage (cb, staging.buffer, t.image,
                                VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, 1,
                                &region);
        barrier (cb, { { &t, resting_layout (t), false } });
        finish_setup ();
        release (staging);
      }

      Pipeline create_render_pipeline (const RenderPipelineDesc& desc)
        override {
        const Layout& layout = program_layout (*desc.program);
        std::vector<VkPipelineShaderStageCreateInfo> stages;
        VkShaderModule vertex = shader_module (desc.vertex, desc.program,
                                               "vertex");
        stages.push_back (stage_info (VK_SHADER_STAGE_VERTEX_BIT, vertex,
                                      desc.program->vertex_entry));
        VkShaderModule fragment = VK_NULL_HANDLE;
        if (!desc.fragment.spirv.empty ()) {
          fragment = shader_module (desc.fragment, desc.program, "fragment");
          stages.push_back (stage_info (VK_SHADER_STAGE_FRAGMENT_BIT, fragment,
                                        desc.program->fragment_entry));
        }

        VkPipelineVertexInputStateCreateInfo input {
          VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO };
        VkPipelineInputAssemblyStateCreateInfo assembly {
          VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO };
        assembly.topology =
          desc.topology == Topology::triangle_strip
            ? VK_PRIMITIVE_TOPOLOGY_TRIANGLE_STRIP
          : desc.topology == Topology::line_list
            ? VK_PRIMITIVE_TOPOLOGY_LINE_LIST
            : VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST;
        // Strips restart at the all-ones index, as Metal's always do.
        assembly.primitiveRestartEnable =
          desc.topology == Topology::triangle_strip;
        VkPipelineViewportStateCreateInfo viewport {
          VK_STRUCTURE_TYPE_PIPELINE_VIEWPORT_STATE_CREATE_INFO };
        viewport.viewportCount = 1;
        viewport.scissorCount = 1;
        VkPipelineRasterizationStateCreateInfo raster {
          VK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_STATE_CREATE_INFO };
        raster.polygonMode = VK_POLYGON_MODE_FILL;
        raster.cullMode = desc.cull == Cull::back    ? VK_CULL_MODE_BACK_BIT
                          : desc.cull == Cull::front ? VK_CULL_MODE_FRONT_BIT
                                                     : VK_CULL_MODE_NONE;
        raster.frontFace = desc.front_counter_clockwise
                             ? VK_FRONT_FACE_COUNTER_CLOCKWISE
                             : VK_FRONT_FACE_CLOCKWISE;
        raster.depthBiasEnable = desc.depth_bias != 0
                                 || desc.slope_scaled_depth_bias != 0;
        raster.depthBiasConstantFactor = desc.depth_bias;
        raster.depthBiasSlopeFactor = desc.slope_scaled_depth_bias;
        raster.lineWidth = 1.0f;
        VkPipelineMultisampleStateCreateInfo multisample {
          VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO };
        multisample.rasterizationSamples = vk_samples (desc.samples);
        VkPipelineDepthStencilStateCreateInfo depth {
          VK_STRUCTURE_TYPE_PIPELINE_DEPTH_STENCIL_STATE_CREATE_INFO };
        if (desc.depth_format != Format::undefined) {
          depth.depthTestEnable = VK_TRUE;
          depth.depthWriteEnable = desc.depth_write;
          depth.depthCompareOp = vk_compare (desc.depth_compare);
        }
        std::array<VkPipelineColorBlendAttachmentState, 8> blends {};
        std::array<VkFormat, 8> formats {};
        for (std::uint32_t i = 0; i < desc.color_count; ++i) {
          formats[i] = vk_format (desc.color_formats[i]);
          auto& b = blends[i];
          b.colorWriteMask = VK_COLOR_COMPONENT_R_BIT | VK_COLOR_COMPONENT_G_BIT
                             | VK_COLOR_COMPONENT_B_BIT
                             | VK_COLOR_COMPONENT_A_BIT;
          if (desc.blend[i] == Blend::none)
            continue;
          const bool additive = desc.blend[i] == Blend::additive;
          b.blendEnable = VK_TRUE;
          b.srcColorBlendFactor = desc.blend[i] == Blend::premultiplied
                                    ? VK_BLEND_FACTOR_ONE
                                    : VK_BLEND_FACTOR_SRC_ALPHA;
          b.dstColorBlendFactor = additive
                                    ? VK_BLEND_FACTOR_ONE
                                    : VK_BLEND_FACTOR_ONE_MINUS_SRC_ALPHA;
          b.colorBlendOp = VK_BLEND_OP_ADD;
          b.srcAlphaBlendFactor = VK_BLEND_FACTOR_ONE;
          b.dstAlphaBlendFactor = additive
                                    ? VK_BLEND_FACTOR_ONE
                                    : VK_BLEND_FACTOR_ONE_MINUS_SRC_ALPHA;
          b.alphaBlendOp = VK_BLEND_OP_ADD;
        }
        VkPipelineColorBlendStateCreateInfo blend {
          VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO };
        blend.attachmentCount = desc.color_count;
        blend.pAttachments = blends.data ();
        const VkDynamicState dynamic_states[] = { VK_DYNAMIC_STATE_VIEWPORT,
                                                  VK_DYNAMIC_STATE_SCISSOR };
        VkPipelineDynamicStateCreateInfo dynamic {
          VK_STRUCTURE_TYPE_PIPELINE_DYNAMIC_STATE_CREATE_INFO };
        dynamic.dynamicStateCount = 2;
        dynamic.pDynamicStates = dynamic_states;
        VkPipelineRenderingCreateInfo rendering {
          VK_STRUCTURE_TYPE_PIPELINE_RENDERING_CREATE_INFO };
        rendering.colorAttachmentCount = desc.color_count;
        rendering.pColorAttachmentFormats = formats.data ();
        rendering.depthAttachmentFormat = vk_format (desc.depth_format);

        VkGraphicsPipelineCreateInfo info {
          VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO };
        info.pNext = &rendering;
        info.stageCount = std::uint32_t (stages.size ());
        info.pStages = stages.data ();
        info.pVertexInputState = &input;
        info.pInputAssemblyState = &assembly;
        info.pViewportState = &viewport;
        info.pRasterizationState = &raster;
        info.pMultisampleState = &multisample;
        info.pDepthStencilState = &depth;
        info.pColorBlendState = &blend;
        info.pDynamicState = &dynamic;
        info.layout = layout.pipeline;
        VkPipelineRow row;
        row.layout = &layout;
        row.name = desc.program->name;
        const VkResult result = vkCreateGraphicsPipelines (
          m_device, VK_NULL_HANDLE, 1, &info, nullptr, &row.pipeline);
        vkDestroyShaderModule (m_device, vertex, nullptr);
        if (fragment)
          vkDestroyShaderModule (m_device, fragment, nullptr);
        if (result != VK_SUCCESS)
          throw std::runtime_error (std::string ("NHAL: pipeline ")
                                    + desc.program->name + " failed: "
                                    + std::to_string (int (result)));
        name (VK_OBJECT_TYPE_PIPELINE, std::uint64_t (row.pipeline),
              desc.label ? desc.label : desc.program->name);
        return m_pipelines.insert<Pipeline> (row);
      }

      Pipeline create_compute_pipeline (const ComputePipelineDesc& desc)
        override {
        const Layout& layout = program_layout (*desc.program);
        VkShaderModule module = shader_module (desc.compute, desc.program,
                                               "compute");
        VkComputePipelineCreateInfo info {
          VK_STRUCTURE_TYPE_COMPUTE_PIPELINE_CREATE_INFO };
        info.stage = stage_info (VK_SHADER_STAGE_COMPUTE_BIT, module,
                                 desc.program->compute_entry);
        info.layout = layout.pipeline;
        VkPipelineRow row;
        row.layout = &layout;
        row.name = desc.program->name;
        row.compute = true;
        const VkResult result = vkCreateComputePipelines (
          m_device, VK_NULL_HANDLE, 1, &info, nullptr, &row.pipeline);
        vkDestroyShaderModule (m_device, module, nullptr);
        if (result != VK_SUCCESS)
          throw std::runtime_error (std::string ("NHAL: compute pipeline ")
                                    + desc.program->name + " failed: "
                                    + std::to_string (int (result)));
        name (VK_OBJECT_TYPE_PIPELINE, std::uint64_t (row.pipeline),
              desc.label ? desc.label : desc.program->name);
        return m_pipelines.insert<Pipeline> (row);
      }

      void destroy (Buffer buffer) override {
        VkBufferRow row = m_buffers.take (buffer);
        Retired r;
        r.buffer = row.buffer;
        r.memory = row.memory;
        m_retired.retire (m_serial, r);
      }

      void destroy (Texture texture) override {
        VkTextureRow row = m_textures.take (texture);
        Retired r;
        r.image = row.image;
        r.view = row.view;
        r.memory = row.memory;
        m_retired.retire (m_serial, r);
      }

      void destroy (Pipeline pipeline) override {
        Retired r;
        r.pipeline = m_pipelines.take (pipeline).pipeline;
        m_retired.retire (m_serial, r);
      }

      // -- the drawable -----------------------------------------------

      Format surface_format () const override { return m_surface_format; }
      std::uint32_t surface_width () const override { return m_width; }
      std::uint32_t surface_height () const override { return m_height; }

      void resize_surface (std::uint32_t width, std::uint32_t height)
        override {
        if (width == m_width && height == m_height && !m_stale)
          return;
        m_width = width;
        m_height = height;
        rebuild_backbuffers ();
      }

      // -- a frame ----------------------------------------------------

      bool begin_frame () override {
        const std::uint64_t next = m_serial + 1;
        const std::uint32_t slot = next % frames_in_flight;
        Frame& frame = m_frames[slot];
        wait_for (frame.serial);
        collect ();
        warm_present_timing ();
        drain_present_timing ();
        if (m_surface) {
          if (m_stale)
            rebuild_backbuffers ();
          if (!m_swapchain)
            return false;
          VkResult result = vkAcquireNextImageKHR (
            m_device, m_swapchain, std::numeric_limits<std::uint64_t>::max (),
            frame.acquired, VK_NULL_HANDLE, &m_image_index);
          if (result == VK_ERROR_OUT_OF_DATE_KHR) {
            rebuild_backbuffers ();
            if (!m_swapchain)
              return false;
            result = vkAcquireNextImageKHR (
              m_device, m_swapchain,
              std::numeric_limits<std::uint64_t>::max (), frame.acquired,
              VK_NULL_HANDLE, &m_image_index);
          }
          if (result == VK_SUBOPTIMAL_KHR)
            m_stale = true;
          else if (result != VK_SUCCESS)
            check (result, "vkAcquireNextImageKHR");
        } else {
          m_image_index = next % m_backbuffers.size ();
        }
        m_serial = next;
        m_slot = slot;
        check (vkResetCommandPool (m_device, frame.pool, 0),
               "reset command pool");
        frame.secondaries_used = 0;
        m_cb = frame.primary;
        VkCommandBufferBeginInfo begin {
          VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO };
        begin.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;
        check (vkBeginCommandBuffer (m_cb, &begin), "begin command buffer");
        vkCmdResetQueryPool (m_cb, frame.queries, 0, max_timestamps);
        m_arena = { static_cast<std::byte*> (frame.arena.mapped),
                    arena_tag, arena_capacity, 0 };
        m_current_backbuffer = m_backbuffers[m_image_index];
        frame.labels.clear ();
        frame.timestamps = 0;
        frame.resolved = false;
        reset_bindings ();
        return true;
      }

      Texture backbuffer () override { return m_current_backbuffer; }

      Transient allocate (std::uint64_t size, std::uint64_t alignment)
        override {
        alignment = std::max<std::uint64_t> (alignment, 256);
        if (!m_arena.fits (size, alignment))
          throw std::runtime_error ("NHAL: frame arena exhausted");
        return m_arena.allocate (size, alignment);
      }

      void begin_render_pass (const RenderPassDesc& desc) override {
        Frame& frame = m_frames[m_slot];
        if (frame.secondaries_used == frame.secondaries.size ()) {
          VkCommandBufferAllocateInfo info {
            VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO };
          info.commandPool = frame.pool;
          info.level = VK_COMMAND_BUFFER_LEVEL_SECONDARY;
          info.commandBufferCount = 1;
          VkCommandBuffer cb;
          check (vkAllocateCommandBuffers (m_device, &info, &cb),
                 "secondary command buffer");
          frame.secondaries.push_back (cb);
        }
        m_pass = desc;
        m_in_pass = true;
        m_pass_reads.clear ();
        m_pass_storage.clear ();

        std::array<VkFormat, 8> formats {};
        std::uint32_t width = 0, height = 0, samples = 1;
        for (std::uint32_t i = 0; i < desc.color_count; ++i) {
          const VkTextureRow& t = m_textures[desc.colors[i].texture];
          formats[i] = vk_format (t.desc.format);
          width = t.desc.width;
          height = t.desc.height;
          samples = t.desc.samples;
        }
        VkFormat depth_format = VK_FORMAT_UNDEFINED;
        if (desc.depth.texture) {
          const VkTextureRow& t = m_textures[desc.depth.texture];
          depth_format = vk_format (t.desc.format);
          width = t.desc.width;
          height = t.desc.height;
          samples = t.desc.samples;
        }
        m_pass_extent = { width, height };

        VkCommandBufferInheritanceRenderingInfo rendering {
          VK_STRUCTURE_TYPE_COMMAND_BUFFER_INHERITANCE_RENDERING_INFO };
        rendering.colorAttachmentCount = desc.color_count;
        rendering.pColorAttachmentFormats = formats.data ();
        rendering.depthAttachmentFormat = depth_format;
        rendering.rasterizationSamples = vk_samples (samples);
        VkCommandBufferInheritanceInfo inheritance {
          VK_STRUCTURE_TYPE_COMMAND_BUFFER_INHERITANCE_INFO };
        inheritance.pNext = &rendering;
        VkCommandBufferBeginInfo begin {
          VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO };
        begin.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT
                      | VK_COMMAND_BUFFER_USAGE_RENDER_PASS_CONTINUE_BIT;
        begin.pInheritanceInfo = &inheritance;
        m_cb = frame.secondaries[frame.secondaries_used++];
        check (vkBeginCommandBuffer (m_cb, &begin), "begin secondary");
        reset_bindings ();
        set_viewport (0, 0, float (width), float (height));
      }

      void end_render_pass () override {
        check (vkEndCommandBuffer (m_cb), "end secondary");
        const VkCommandBuffer secondary = m_cb;
        m_cb = m_frames[m_slot].primary;
        m_in_pass = false;

        // The layouts the pass needs: its attachments, and what its draws
        // sampled or stored to.
        std::vector<Transition> transitions;
        for (Texture handle : m_pass_reads) {
          VkTextureRow& t = m_textures[handle];
          transitions.push_back ({ &t, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,
                                   false });
        }
        for (Texture handle : m_pass_storage)
          transitions.push_back ({ &m_textures[handle],
                                   VK_IMAGE_LAYOUT_GENERAL, false });
        std::array<VkRenderingAttachmentInfo, 8> colors {};
        for (std::uint32_t i = 0; i < m_pass.color_count; ++i) {
          const ColorAttachment& c = m_pass.colors[i];
          VkTextureRow& t = m_textures[c.texture];
          transitions.push_back ({ &t,
                                   VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL,
                                   c.load != Load::load });
          auto& a = colors[i];
          a.sType = VK_STRUCTURE_TYPE_RENDERING_ATTACHMENT_INFO;
          a.imageView = t.view;
          a.imageLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
          a.loadOp = c.load == Load::clear  ? VK_ATTACHMENT_LOAD_OP_CLEAR
                     : c.load == Load::load ? VK_ATTACHMENT_LOAD_OP_LOAD
                                            : VK_ATTACHMENT_LOAD_OP_DONT_CARE;
          a.storeOp = c.store == Store::store ? VK_ATTACHMENT_STORE_OP_STORE
                                              : VK_ATTACHMENT_STORE_OP_DONT_CARE;
          std::memcpy (a.clearValue.color.float32, c.clear.data (),
                       sizeof (float) * 4);
          if (c.resolve) {
            VkTextureRow& r = m_textures[c.resolve];
            transitions.push_back ({ &r,
                                     VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL,
                                     true });
            a.resolveMode = VK_RESOLVE_MODE_AVERAGE_BIT;
            a.resolveImageView = r.view;
            a.resolveImageLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
          }
        }
        VkRenderingAttachmentInfo depth {
          VK_STRUCTURE_TYPE_RENDERING_ATTACHMENT_INFO };
        if (m_pass.depth.texture) {
          const DepthAttachment& d = m_pass.depth;
          VkTextureRow& t = m_textures[d.texture];
          transitions.push_back ({ &t,
                                   VK_IMAGE_LAYOUT_DEPTH_ATTACHMENT_OPTIMAL,
                                   d.load != Load::load });
          depth.imageView = t.view;
          depth.imageLayout = VK_IMAGE_LAYOUT_DEPTH_ATTACHMENT_OPTIMAL;
          depth.loadOp = d.load == Load::clear  ? VK_ATTACHMENT_LOAD_OP_CLEAR
                         : d.load == Load::load ? VK_ATTACHMENT_LOAD_OP_LOAD
                                                : VK_ATTACHMENT_LOAD_OP_DONT_CARE;
          depth.storeOp = d.store == Store::store
                            ? VK_ATTACHMENT_STORE_OP_STORE
                            : VK_ATTACHMENT_STORE_OP_DONT_CARE;
          depth.clearValue.depthStencil.depth = d.clear;
        }

        timestamp_begin (m_pass.label ? m_pass.label : "render");
        barrier (m_cb, transitions);
        VkRenderingInfo info { VK_STRUCTURE_TYPE_RENDERING_INFO };
        info.flags = VK_RENDERING_CONTENTS_SECONDARY_COMMAND_BUFFERS_BIT;
        info.renderArea = { { 0, 0 }, m_pass_extent };
        info.layerCount = 1;
        info.colorAttachmentCount = m_pass.color_count;
        info.pColorAttachments = colors.data ();
        if (m_pass.depth.texture)
          info.pDepthAttachment = &depth;
        vkCmdBeginRendering (m_cb, &info);
        vkCmdExecuteCommands (m_cb, 1, &secondary);
        vkCmdEndRendering (m_cb);
        timestamp_end ();
        m_pass = {};
        reset_bindings ();
      }

      void begin_compute_pass (const char* label) override {
        timestamp_begin (label ? label : "compute");
        reset_bindings ();
      }

      void end_compute_pass () override { timestamp_end (); }

      void copy_to_buffer (Buffer target, std::uint64_t offset,
                           const Transient& source) override {
        barrier (m_cb);
        const VkBufferCopy region { source.gpu_address - arena_tag, offset,
                                    source.size };
        vkCmdCopyBuffer (m_cb, m_frames[m_slot].arena.buffer,
                         m_buffers[target].buffer, 1, &region);
      }

      void set_pipeline (Pipeline handle) override {
        const VkPipelineRow& p = m_pipelines[handle];
        if (m_pipeline == &p)
          return;
        m_pipeline = &p;
        m_pipeline_bound = false;
        m_bindings_dirty = true;
      }

      void set_buffer (std::uint32_t binding, Buffer buffer,
                       std::uint64_t offset) override {
        const VkBufferRow& row = m_buffers[buffer];
        m_bound_buffers.at (binding) = { row.buffer, offset,
                                         row.size - offset };
        m_bindings_dirty = true;
      }

      void set_buffer (std::uint32_t binding, const Transient& slice)
        override {
        m_bound_buffers.at (binding) = { m_frames[m_slot].arena.buffer,
                                         slice.gpu_address - arena_tag,
                                         std::max<std::uint64_t> (slice.size,
                                                                  4) };
        m_bindings_dirty = true;
      }

      void set_texture (std::uint32_t binding, Texture texture) override {
        m_bound_textures.at (binding) = texture;
        m_bindings_dirty = true;
      }

      void set_storage_texture (std::uint32_t binding, Texture texture)
        override {
        m_bound_storage.at (binding) = texture;
        m_bindings_dirty = true;
      }

      void set_viewport (float x, float y, float width, float height)
        override {
        const VkViewport viewport { x, y, width, height, 0, 1 };
        const VkRect2D scissor {
          { std::int32_t (x), std::int32_t (y) },
          { std::uint32_t (width), std::uint32_t (height) } };
        vkCmdSetViewport (m_cb, 0, 1, &viewport);
        vkCmdSetScissor (m_cb, 0, 1, &scissor);
      }

      void draw (std::uint32_t vertex_count, std::uint32_t instance_count,
                 std::uint32_t first_vertex, std::uint32_t first_instance)
        override {
        bind_arguments ();
        vkCmdDraw (m_cb, vertex_count, instance_count, first_vertex,
                   first_instance);
      }

      void draw_indexed (Buffer indices, IndexType type,
                         std::uint32_t index_count,
                         std::uint32_t instance_count,
                         std::uint32_t first_index, std::int32_t base_vertex,
                         std::uint32_t first_instance) override {
        bind_arguments ();
        set_index_buffer (indices, type);
        vkCmdDrawIndexed (m_cb, index_count, instance_count, first_index,
                          base_vertex, first_instance);
      }

      void draw_indirect (Buffer arguments, std::uint64_t offset) override {
        bind_arguments ();
        vkCmdDrawIndirect (m_cb, m_buffers[arguments].buffer, offset, 1,
                           sizeof (DrawIndirectArgs));
      }

      void draw_indexed_indirect (Buffer indices, IndexType type,
                                  Buffer arguments, std::uint64_t offset)
        override {
        bind_arguments ();
        set_index_buffer (indices, type);
        vkCmdDrawIndexedIndirect (m_cb, m_buffers[arguments].buffer, offset,
                                  1, sizeof (DrawIndexedIndirectArgs));
      }

      void dispatch (std::uint32_t x, std::uint32_t y, std::uint32_t z)
        override {
        prepare_dispatch ();
        vkCmdDispatch (m_cb, x, y, z);
      }

      void dispatch_indirect (Buffer arguments, std::uint64_t offset)
        override {
        prepare_dispatch ();
        vkCmdDispatchIndirect (m_cb, m_buffers[arguments].buffer, offset);
      }

      void capture_frame (std::function<void (const Capture&)> done)
        override {
        m_capture_request = std::move (done);
      }

      std::span<const PassTiming> pass_timings () const override {
        return m_pass_timings;
      }

      void end_frame () override {
        Frame& frame = m_frames[m_slot];
        VkTextureRow& back = m_textures[m_current_backbuffer];
        if (m_capture_request)
          encode_capture (back);
        if (m_surface)
          barrier (m_cb, { { &back, VK_IMAGE_LAYOUT_PRESENT_SRC_KHR,
                                 false } });
        check (vkEndCommandBuffer (m_cb), "end command buffer");

        VkCommandBufferSubmitInfo command {
          VK_STRUCTURE_TYPE_COMMAND_BUFFER_SUBMIT_INFO };
        command.commandBuffer = m_cb;
        VkSemaphoreSubmitInfo wait { VK_STRUCTURE_TYPE_SEMAPHORE_SUBMIT_INFO };
        wait.semaphore = frame.acquired;
        wait.stageMask = VK_PIPELINE_STAGE_2_ALL_COMMANDS_BIT;
        std::array<VkSemaphoreSubmitInfo, 2> signals {};
        signals[0].sType = VK_STRUCTURE_TYPE_SEMAPHORE_SUBMIT_INFO;
        signals[0].semaphore = m_timeline;
        signals[0].value = m_serial;
        signals[0].stageMask = VK_PIPELINE_STAGE_2_ALL_COMMANDS_BIT;
        if (m_surface) {
          signals[1].sType = VK_STRUCTURE_TYPE_SEMAPHORE_SUBMIT_INFO;
          signals[1].semaphore = m_render_done[m_image_index];
          signals[1].stageMask = VK_PIPELINE_STAGE_2_ALL_COMMANDS_BIT;
        }
        VkSubmitInfo2 submit { VK_STRUCTURE_TYPE_SUBMIT_INFO_2 };
        submit.waitSemaphoreInfoCount = m_surface ? 1 : 0;
        submit.pWaitSemaphoreInfos = &wait;
        submit.commandBufferInfoCount = 1;
        submit.pCommandBufferInfos = &command;
        submit.signalSemaphoreInfoCount = m_surface ? 2 : 1;
        submit.pSignalSemaphoreInfos = signals.data ();
        check (vkQueueSubmit2 (m_queue, 1, &submit, VK_NULL_HANDLE),
               "vkQueueSubmit2");
        frame.serial = m_serial;

        if (m_surface) {
          VkPresentInfoKHR present { VK_STRUCTURE_TYPE_PRESENT_INFO_KHR };
          // The frame's serial names the present, and asks when it appears.
          const std::uint64_t present_id = m_serial;
          VkPresentTimingInfoEXT timing {
            VK_STRUCTURE_TYPE_PRESENT_TIMING_INFO_EXT };
          timing.presentStageQueries = m_monotonic ? m_present_stage : 0;
          VkPresentTimingsInfoEXT timings {
            VK_STRUCTURE_TYPE_PRESENT_TIMINGS_INFO_EXT };
          timings.swapchainCount = 1;
          timings.pTimingInfos = &timing;
          VkPresentId2KHR ids { VK_STRUCTURE_TYPE_PRESENT_ID_2_KHR };
          ids.pNext = m_monotonic ? &timings : nullptr;
          ids.swapchainCount = 1;
          ids.pPresentIds = &present_id;
          if (m_present_stage)
            present.pNext = &ids;
          present.waitSemaphoreCount = 1;
          present.pWaitSemaphores = &m_render_done[m_image_index];
          present.swapchainCount = 1;
          present.pSwapchains = &m_swapchain;
          present.pImageIndices = &m_image_index;
          const VkResult result = vkQueuePresentKHR (m_queue, &present);
          if (result == VK_ERROR_OUT_OF_DATE_KHR
              || result == VK_SUBOPTIMAL_KHR)
            m_stale = true;
          else if (result == VK_ERROR_PRESENT_TIMING_QUEUE_FULL_EXT) {
            // Reports are drained every frame, so a full queue means the
            // surface stopped answering; stop asking.
            std::cerr << "NHAL: present timing queue full; frames keep the "
                         "host's time"
                      << std::endl;
            m_monotonic = false;
          }
          else
            check (result, "vkQueuePresentKHR");
        }
        m_cb = VK_NULL_HANDLE;
      }

      void wait_idle () override {
        if (m_serial)
          wait_for (m_serial);
        collect ();
      }

      FrameTiming next_frame_timing () const override {
        if (!m_monotonic)
          return {};
        return m_presentation.predict (m_serial + 1, steady_seconds ());
      }

    private:
      enum class Placement { device, upload, readback };

      struct Frame {
        VkCommandPool pool = VK_NULL_HANDLE;
        VkCommandBuffer primary = VK_NULL_HANDLE;
        std::vector<VkCommandBuffer> secondaries;
        std::size_t secondaries_used = 0;
        VkSemaphore acquired = VK_NULL_HANDLE;
        VkBufferRow arena;
        std::uint64_t serial = 0;
        VkQueryPool queries = VK_NULL_HANDLE;
        std::vector<std::string> labels;
        std::uint32_t timestamps = 0;
        bool resolved = true;
      };

      // An image's next layout; `discard` lets the barrier drop its
      // contents, for attachments about to be cleared or overwritten.
      struct Transition {
        VkTextureRow* texture;
        VkImageLayout layout;
        bool discard;
      };

      struct PendingCapture {
        std::uint64_t serial = 0;
        VkBufferRow readback;
        Capture capture;
        std::function<void (const Capture&)> done;
      };

      // -- setup ------------------------------------------------------

      void create_instance (const VulkanSurface& surface) {
        std::vector<const char*> extensions = surface.instance_extensions;
        std::vector<const char*> layers;
        const char* validation = moppe::environment ("MOPPE_VULKAN_VALIDATION");
        m_validation = validation && *validation && *validation != '0';
        std::uint32_t count = 0;
        vkEnumerateInstanceExtensionProperties (nullptr, &count, nullptr);
        std::vector<VkExtensionProperties> available (count);
        vkEnumerateInstanceExtensionProperties (nullptr, &count,
                                                available.data ());
        for (const auto& e : available)
          if (!std::strcmp (e.extensionName,
                            VK_EXT_DEBUG_UTILS_EXTENSION_NAME))
            m_debug_utils = true;
        if (m_debug_utils)
          extensions.push_back (VK_EXT_DEBUG_UTILS_EXTENSION_NAME);
        // Present timing asks the surface through its second capabilities
        // query.
        for (const auto& e : available)
          if (surface.create
              && !std::strcmp (e.extensionName,
                               VK_KHR_GET_SURFACE_CAPABILITIES_2_EXTENSION_NAME)) {
            m_surface_capabilities_2 = true;
            extensions.push_back (
              VK_KHR_GET_SURFACE_CAPABILITIES_2_EXTENSION_NAME);
          }
        if (m_validation)
          layers.push_back ("VK_LAYER_KHRONOS_validation");

        VkApplicationInfo app { VK_STRUCTURE_TYPE_APPLICATION_INFO };
        app.pApplicationName = "moppe";
        app.pEngineName = "NHAL";
        app.apiVersion = VK_API_VERSION_1_3;
        VkInstanceCreateInfo info { VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO };
        info.pApplicationInfo = &app;
        info.enabledExtensionCount = std::uint32_t (extensions.size ());
        info.ppEnabledExtensionNames = extensions.data ();
        info.enabledLayerCount = std::uint32_t (layers.size ());
        info.ppEnabledLayerNames = layers.data ();
        check (vkCreateInstance (&info, nullptr, &m_instance),
               "vkCreateInstance");

        if (m_debug_utils && m_validation) {
          VkDebugUtilsMessengerCreateInfoEXT messenger {
            VK_STRUCTURE_TYPE_DEBUG_UTILS_MESSENGER_CREATE_INFO_EXT };
          messenger.messageSeverity =
            VK_DEBUG_UTILS_MESSAGE_SEVERITY_WARNING_BIT_EXT
            | VK_DEBUG_UTILS_MESSAGE_SEVERITY_ERROR_BIT_EXT;
          messenger.messageType =
            VK_DEBUG_UTILS_MESSAGE_TYPE_GENERAL_BIT_EXT
            | VK_DEBUG_UTILS_MESSAGE_TYPE_VALIDATION_BIT_EXT
            | VK_DEBUG_UTILS_MESSAGE_TYPE_PERFORMANCE_BIT_EXT;
          messenger.pfnUserCallback = &VulkanDevice::report;
          auto create = reinterpret_cast<PFN_vkCreateDebugUtilsMessengerEXT> (
            vkGetInstanceProcAddr (m_instance,
                                   "vkCreateDebugUtilsMessengerEXT"));
          if (create)
            create (m_instance, &messenger, nullptr, &m_messenger);
        }
      }

      static VKAPI_ATTR VkBool32 VKAPI_CALL
      report (VkDebugUtilsMessageSeverityFlagBitsEXT,
              VkDebugUtilsMessageTypeFlagsEXT,
              const VkDebugUtilsMessengerCallbackDataEXT* data, void*) {
        std::cerr << "NHAL Vulkan: " << data->pMessage << std::endl;
        return VK_FALSE;
      }

      void choose_physical_device () {
        std::uint32_t count = 0;
        vkEnumeratePhysicalDevices (m_instance, &count, nullptr);
        std::vector<VkPhysicalDevice> devices (count);
        vkEnumeratePhysicalDevices (m_instance, &count, devices.data ());
        if (devices.empty ())
          throw std::runtime_error ("NHAL: no Vulkan devices");
        int chosen = -1;
        if (const char* index = moppe::environment ("MOPPE_VULKAN_DEVICE"))
          chosen = std::clamp (std::atoi (index), 0, int (count) - 1);
        for (std::uint32_t i = 0; chosen < 0 && i < count; ++i) {
          VkPhysicalDeviceProperties p;
          vkGetPhysicalDeviceProperties (devices[i], &p);
          if (p.deviceType == VK_PHYSICAL_DEVICE_TYPE_DISCRETE_GPU)
            chosen = int (i);
        }
        if (chosen < 0)
          chosen = 0;
        m_physical = devices[chosen];
        VkPhysicalDeviceProperties properties;
        vkGetPhysicalDeviceProperties (m_physical, &properties);
        m_adapter = properties.deviceName;
        m_limits = properties.limits;
        m_ms_per_tick = double (properties.limits.timestampPeriod) * 1e-6;
        vkGetPhysicalDeviceMemoryProperties (m_physical, &m_memory);

        std::uint32_t families = 0;
        vkGetPhysicalDeviceQueueFamilyProperties (m_physical, &families,
                                                  nullptr);
        std::vector<VkQueueFamilyProperties> family (families);
        vkGetPhysicalDeviceQueueFamilyProperties (m_physical, &families,
                                                  family.data ());
        for (std::uint32_t i = 0; i < families; ++i) {
          const VkQueueFlags needed = VK_QUEUE_GRAPHICS_BIT
                                      | VK_QUEUE_COMPUTE_BIT;
          if ((family[i].queueFlags & needed) != needed)
            continue;
          VkBool32 presents = VK_TRUE;
          if (m_surface)
            vkGetPhysicalDeviceSurfaceSupportKHR (m_physical, i, m_surface,
                                                  &presents);
          if (presents) {
            m_family = i;
            m_timestamp_bits = family[i].timestampValidBits;
            return;
          }
        }
        throw std::runtime_error ("NHAL: " + m_adapter
                                  + " has no queue that draws and presents");
      }

      void create_device () {
        VkPhysicalDeviceRobustness2FeaturesEXT robustness {
          VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_ROBUSTNESS_2_FEATURES_EXT };
        VkPhysicalDeviceVulkan13Features v13 {
          VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_3_FEATURES };
        v13.pNext = &robustness;
        VkPhysicalDeviceVulkan12Features v12 {
          VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_2_FEATURES };
        v12.pNext = &v13;
        VkPhysicalDeviceFeatures2 supported {
          VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_FEATURES_2 };
        supported.pNext = &v12;
        vkGetPhysicalDeviceFeatures2 (m_physical, &supported);
        if (!v13.dynamicRendering || !v13.synchronization2
            || !v12.timelineSemaphore || !robustness.nullDescriptor)
          throw std::runtime_error (
            "NHAL: " + m_adapter
            + " lacks dynamic rendering, synchronization2, timeline "
              "semaphores, or null descriptors");

        VkPhysicalDeviceRobustness2FeaturesEXT want_robustness {
          VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_ROBUSTNESS_2_FEATURES_EXT };
        want_robustness.nullDescriptor = VK_TRUE;
        VkPhysicalDeviceVulkan13Features want13 {
          VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_3_FEATURES };
        want13.pNext = &want_robustness;
        want13.dynamicRendering = VK_TRUE;
        want13.synchronization2 = VK_TRUE;
        VkPhysicalDeviceVulkan12Features want12 {
          VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_2_FEATURES };
        want12.pNext = &want13;
        want12.timelineSemaphore = VK_TRUE;
        VkPhysicalDeviceFeatures2 want {
          VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_FEATURES_2 };
        want.pNext = &want12;
        const VkPhysicalDeviceFeatures& has = supported.features;
        // Attachments blend independently, as Direct3D's do with
        // IndependentBlendEnable.
        if (!has.independentBlend)
          throw std::runtime_error ("NHAL: " + m_adapter
                                    + " lacks independent blending");
        want.features.independentBlend = VK_TRUE;
        want.features.fragmentStoresAndAtomics = has.fragmentStoresAndAtomics;
        want.features.vertexPipelineStoresAndAtomics =
          has.vertexPipelineStoresAndAtomics;
        want.features.shaderStorageImageExtendedFormats =
          has.shaderStorageImageExtendedFormats;
        want.features.depthClamp = has.depthClamp;

        std::vector<const char*> extensions {
          VK_KHR_PUSH_DESCRIPTOR_EXTENSION_NAME,
          VK_EXT_ROBUSTNESS_2_EXTENSION_NAME };
        if (m_surface)
          extensions.push_back (VK_KHR_SWAPCHAIN_EXTENSION_NAME);
        VkPhysicalDevicePresentId2FeaturesKHR want_id2 {
          VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_PRESENT_ID_2_FEATURES_KHR };
        VkPhysicalDevicePresentTimingFeaturesEXT want_timing {
          VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_PRESENT_TIMING_FEATURES_EXT };
        if (choose_present_timing ()) {
          extensions.push_back (VK_EXT_PRESENT_TIMING_EXTENSION_NAME);
          extensions.push_back (VK_KHR_PRESENT_ID_2_EXTENSION_NAME);
          extensions.push_back (VK_KHR_CALIBRATED_TIMESTAMPS_EXTENSION_NAME);
          want_id2.presentId2 = VK_TRUE;
          want_timing.presentTiming = VK_TRUE;
          want_timing.pNext = &want_id2;
          want_id2.pNext = want.pNext;
          want.pNext = &want_timing;
        }
        const float priority = 1.0f;
        VkDeviceQueueCreateInfo queue {
          VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO };
        queue.queueFamilyIndex = m_family;
        queue.queueCount = 1;
        queue.pQueuePriorities = &priority;
        VkDeviceCreateInfo info { VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO };
        info.pNext = &want;
        info.queueCreateInfoCount = 1;
        info.pQueueCreateInfos = &queue;
        info.enabledExtensionCount = std::uint32_t (extensions.size ());
        info.ppEnabledExtensionNames = extensions.data ();
        check (vkCreateDevice (m_physical, &info, nullptr, &m_device),
               "vkCreateDevice");
        vkGetDeviceQueue (m_device, m_family, 0, &m_queue);
        m_push_descriptors =
          reinterpret_cast<PFN_vkCmdPushDescriptorSetKHR> (
            vkGetDeviceProcAddr (m_device, "vkCmdPushDescriptorSetKHR"));
        if (!m_push_descriptors)
          throw std::runtime_error ("NHAL: no vkCmdPushDescriptorSetKHR");
        if (m_debug_utils)
          m_set_name = reinterpret_cast<PFN_vkSetDebugUtilsObjectNameEXT> (
            vkGetDeviceProcAddr (m_device, "vkSetDebugUtilsObjectNameEXT"));
        if (m_present_stage) {
          auto load = [&] (const char* name) {
            return vkGetDeviceProcAddr (m_device, name);
          };
          m_timing_queue_size =
            reinterpret_cast<PFN_vkSetSwapchainPresentTimingQueueSizeEXT> (
              load ("vkSetSwapchainPresentTimingQueueSizeEXT"));
          m_timing_properties =
            reinterpret_cast<PFN_vkGetSwapchainTimingPropertiesEXT> (
              load ("vkGetSwapchainTimingPropertiesEXT"));
          m_time_domains =
            reinterpret_cast<PFN_vkGetSwapchainTimeDomainPropertiesEXT> (
              load ("vkGetSwapchainTimeDomainPropertiesEXT"));
          m_past_timing = reinterpret_cast<PFN_vkGetPastPresentationTimingEXT> (
            load ("vkGetPastPresentationTimingEXT"));
          m_calibrate = reinterpret_cast<PFN_vkGetCalibratedTimestampsKHR> (
            load ("vkGetCalibratedTimestampsKHR"));
          if (!m_timing_queue_size || !m_timing_properties || !m_time_domains
              || !m_past_timing || !m_calibrate)
            m_present_stage = 0;
        }
      }

      // Whether the device and surface can say when presents appear, and
      // at which stage to ask: the first pixel visible, else sent out.
      bool choose_present_timing () {
        m_present_stage = 0;
        if (!m_surface || !m_surface_capabilities_2)
          return false;
        std::uint32_t count = 0;
        vkEnumerateDeviceExtensionProperties (m_physical, nullptr, &count,
                                              nullptr);
        std::vector<VkExtensionProperties> available (count);
        vkEnumerateDeviceExtensionProperties (m_physical, nullptr, &count,
                                              available.data ());
        auto has = [&] (const char* name) {
          return std::any_of (available.begin (), available.end (),
                              [&] (const VkExtensionProperties& e) {
                                return !std::strcmp (e.extensionName, name);
                              });
        };
        if (!has (VK_EXT_PRESENT_TIMING_EXTENSION_NAME)
            || !has (VK_KHR_PRESENT_ID_2_EXTENSION_NAME)
            || !has (VK_KHR_CALIBRATED_TIMESTAMPS_EXTENSION_NAME))
          return false;
        VkPhysicalDevicePresentId2FeaturesKHR id2 {
          VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_PRESENT_ID_2_FEATURES_KHR };
        VkPhysicalDevicePresentTimingFeaturesEXT timing {
          VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_PRESENT_TIMING_FEATURES_EXT };
        timing.pNext = &id2;
        VkPhysicalDeviceFeatures2 features {
          VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_FEATURES_2 };
        features.pNext = &timing;
        vkGetPhysicalDeviceFeatures2 (m_physical, &features);
        if (!timing.presentTiming || !id2.presentId2)
          return false;

        auto surface_capabilities =
          reinterpret_cast<PFN_vkGetPhysicalDeviceSurfaceCapabilities2KHR> (
            vkGetInstanceProcAddr (m_instance,
                                   "vkGetPhysicalDeviceSurfaceCapabilities2KHR"));
        if (!surface_capabilities)
          return false;
        VkSurfaceCapabilitiesPresentId2KHR surface_id2 {
          VK_STRUCTURE_TYPE_SURFACE_CAPABILITIES_PRESENT_ID_2_KHR };
        VkPresentTimingSurfaceCapabilitiesEXT surface_timing {
          VK_STRUCTURE_TYPE_PRESENT_TIMING_SURFACE_CAPABILITIES_EXT };
        surface_timing.pNext = &surface_id2;
        VkSurfaceCapabilities2KHR capabilities {
          VK_STRUCTURE_TYPE_SURFACE_CAPABILITIES_2_KHR };
        capabilities.pNext = &surface_timing;
        VkPhysicalDeviceSurfaceInfo2KHR info {
          VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_SURFACE_INFO_2_KHR };
        info.surface = m_surface;
        if (surface_capabilities (m_physical, &info, &capabilities)
              != VK_SUCCESS
            || !surface_timing.presentTimingSupported
            || !surface_id2.presentId2Supported)
          return false;
        for (VkPresentStageFlagsEXT stage :
             { VkPresentStageFlagsEXT (
                 VK_PRESENT_STAGE_IMAGE_FIRST_PIXEL_VISIBLE_BIT_EXT),
               VkPresentStageFlagsEXT (
                 VK_PRESENT_STAGE_IMAGE_FIRST_PIXEL_OUT_BIT_EXT) })
          if (surface_timing.presentStageQueries & stage) {
            m_present_stage = stage;
            return true;
          }
        return false;
      }

      // Once the swapchain knows them, its refresh period and the time
      // domain its reports use: CLOCK_MONOTONIC, the steady clock's, or else
      // the swapchain's own, calibrated against it.
      void warm_present_timing () {
        if (!m_present_stage || !m_swapchain)
          return;
        if (!m_time_domain_ready) {
          VkSwapchainTimeDomainPropertiesEXT domains {
            VK_STRUCTURE_TYPE_SWAPCHAIN_TIME_DOMAIN_PROPERTIES_EXT };
          std::uint64_t counter = 0;
          if (m_time_domains (m_device, m_swapchain, &domains, &counter)
              == VK_NOT_READY)
            return;
          std::vector<VkTimeDomainKHR> kinds (domains.timeDomainCount);
          std::vector<std::uint64_t> ids (domains.timeDomainCount);
          domains.pTimeDomains = kinds.data ();
          domains.pTimeDomainIds = ids.data ();
          if (m_time_domains (m_device, m_swapchain, &domains, &counter)
              == VK_NOT_READY)
            return;
          m_time_domain_ready = true;
          for (VkTimeDomainKHR wanted :
               { VK_TIME_DOMAIN_CLOCK_MONOTONIC_KHR,
                 VK_TIME_DOMAIN_SWAPCHAIN_LOCAL_EXT,
                 VK_TIME_DOMAIN_PRESENT_STAGE_LOCAL_EXT })
            for (std::uint32_t i = 0; !m_monotonic && i < domains.timeDomainCount;
                 ++i)
              if (kinds[i] == wanted) {
                m_time_domain = kinds[i];
                m_time_domain_id = ids[i];
                m_monotonic = true;
              }
          if (!m_monotonic)
            std::cerr << "NHAL: present timing offers no usable time "
                         "domain; frames keep the host's time"
                      << std::endl;
        }
        if (m_monotonic && !m_refresh_known) {
          VkSwapchainTimingPropertiesEXT properties {
            VK_STRUCTURE_TYPE_SWAPCHAIN_TIMING_PROPERTIES_EXT };
          std::uint64_t counter = 0;
          if (m_timing_properties (m_device, m_swapchain, &properties,
                                   &counter)
                == VK_SUCCESS
              && properties.refreshDuration) {
            m_refresh_known = true;
            m_presentation.set_refresh (
              double (properties.refreshDuration) * 1e-9);
          }
        }
      }

      // Records the presents that have reported when they appeared.
      void drain_present_timing () {
        if (!m_monotonic)
          return;
        VkPastPresentationTimingInfoEXT info {
          VK_STRUCTURE_TYPE_PAST_PRESENTATION_TIMING_INFO_EXT };
        info.swapchain = m_swapchain;
        VkPastPresentationTimingPropertiesEXT properties {
          VK_STRUCTURE_TYPE_PAST_PRESENTATION_TIMING_PROPERTIES_EXT };
        if (m_past_timing (m_device, &info, &properties) != VK_SUCCESS
            || !properties.presentationTimingCount)
          return;
        const std::uint32_t count = properties.presentationTimingCount;
        std::vector<VkPastPresentationTimingEXT> timings (count);
        std::vector<VkPresentStageTimeEXT> stages (count);
        for (std::uint32_t i = 0; i < count; ++i) {
          timings[i].sType = VK_STRUCTURE_TYPE_PAST_PRESENTATION_TIMING_EXT;
          timings[i].presentStageCount = 1;
          timings[i].pPresentStages = &stages[i];
        }
        properties.pPresentationTimings = timings.data ();
        const VkResult result = m_past_timing (m_device, &info, &properties);
        if (result != VK_SUCCESS && result != VK_INCOMPLETE)
          return;
        // Nanoseconds in the swapchain's domain, then in CLOCK_MONOTONIC,
        // then seconds on the steady clock (which on Linux is the same,
        // though the offset keeps them honest anywhere it is not).
        std::int64_t to_monotonic = 0;
        if (m_time_domain != VK_TIME_DOMAIN_CLOCK_MONOTONIC_KHR) {
          VkSwapchainCalibratedTimestampInfoEXT local {
            VK_STRUCTURE_TYPE_SWAPCHAIN_CALIBRATED_TIMESTAMP_INFO_EXT };
          local.swapchain = m_swapchain;
          local.presentStage =
            m_time_domain == VK_TIME_DOMAIN_PRESENT_STAGE_LOCAL_EXT
              ? m_present_stage
              : 0;
          local.timeDomainId = m_time_domain_id;
          std::array<VkCalibratedTimestampInfoKHR, 2> clocks {};
          clocks[0].sType = clocks[1].sType =
            VK_STRUCTURE_TYPE_CALIBRATED_TIMESTAMP_INFO_KHR;
          clocks[0].pNext = &local;
          clocks[0].timeDomain = m_time_domain;
          clocks[1].timeDomain = VK_TIME_DOMAIN_CLOCK_MONOTONIC_KHR;
          std::array<std::uint64_t, 2> stamps {};
          std::uint64_t deviation = 0;
          if (m_calibrate (m_device, 2, clocks.data (), stamps.data (),
                           &deviation)
              != VK_SUCCESS)
            return;
          to_monotonic = std::int64_t (stamps[1]) - std::int64_t (stamps[0]);
        }
        timespec monotonic {};
        clock_gettime (CLOCK_MONOTONIC, &monotonic);
        const double offset = steady_seconds ()
                              - (double (monotonic.tv_sec)
                                 + double (monotonic.tv_nsec) * 1e-9);
        for (std::uint32_t i = 0; i < properties.presentationTimingCount;
             ++i) {
          const VkPastPresentationTimingEXT& t = timings[i];
          if (!t.reportComplete || !t.presentStageCount
              || t.timeDomain != m_time_domain
              || t.timeDomainId != m_time_domain_id || !stages[i].time)
            continue;
          const std::int64_t nanoseconds =
            std::int64_t (stages[i].time) + to_monotonic;
          m_presentation.presented (t.presentId,
                                    double (nanoseconds) * 1e-9 + offset);
        }
      }

      void create_frames () {
        VkSemaphoreTypeCreateInfo timeline {
          VK_STRUCTURE_TYPE_SEMAPHORE_TYPE_CREATE_INFO };
        timeline.semaphoreType = VK_SEMAPHORE_TYPE_TIMELINE;
        VkSemaphoreCreateInfo semaphore {
          VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO };
        semaphore.pNext = &timeline;
        check (vkCreateSemaphore (m_device, &semaphore, nullptr, &m_timeline),
               "timeline semaphore");
        VkCommandPoolCreateInfo pool {
          VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO };
        pool.queueFamilyIndex = m_family;
        for (Frame& frame : m_frames) {
          check (vkCreateCommandPool (m_device, &pool, nullptr, &frame.pool),
                 "command pool");
          VkCommandBufferAllocateInfo info {
            VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO };
          info.commandPool = frame.pool;
          info.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
          info.commandBufferCount = 1;
          check (vkAllocateCommandBuffers (m_device, &info, &frame.primary),
                 "command buffer");
          const VkSemaphoreCreateInfo binary {
            VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO };
          check (vkCreateSemaphore (m_device, &binary, nullptr,
                                    &frame.acquired),
                 "semaphore");
          frame.arena = make_buffer (
            arena_capacity,
            VK_BUFFER_USAGE_STORAGE_BUFFER_BIT
              | VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT
              | VK_BUFFER_USAGE_INDEX_BUFFER_BIT
              | VK_BUFFER_USAGE_TRANSFER_SRC_BIT,
            Placement::upload);
          VkQueryPoolCreateInfo queries {
            VK_STRUCTURE_TYPE_QUERY_POOL_CREATE_INFO };
          queries.queryType = VK_QUERY_TYPE_TIMESTAMP;
          queries.queryCount = max_timestamps;
          check (vkCreateQueryPool (m_device, &queries, nullptr,
                                    &frame.queries),
                 "query pool");
        }
        pool.flags = VK_COMMAND_POOL_CREATE_TRANSIENT_BIT
                     | VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT;
        check (vkCreateCommandPool (m_device, &pool, nullptr, &m_setup_pool),
               "setup pool");
        VkCommandBufferAllocateInfo info {
          VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO };
        info.commandPool = m_setup_pool;
        info.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
        info.commandBufferCount = 1;
        check (vkAllocateCommandBuffers (m_device, &info, &m_setup),
               "setup command buffer");
        const VkFenceCreateInfo fence { VK_STRUCTURE_TYPE_FENCE_CREATE_INFO };
        check (vkCreateFence (m_device, &fence, nullptr, &m_setup_fence),
               "setup fence");
      }

      // The standard samplers, by binding number (docs/nhal.md).
      void create_samplers () {
        for (std::uint32_t i = 0; i < standard_samplers; ++i) {
          VkSamplerCreateInfo s { VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO };
          const bool nearest = i == 2;
          s.magFilter = s.minFilter = nearest ? VK_FILTER_NEAREST
                                              : VK_FILTER_LINEAR;
          s.mipmapMode = nearest ? VK_SAMPLER_MIPMAP_MODE_NEAREST
                                 : VK_SAMPLER_MIPMAP_MODE_LINEAR;
          s.addressModeU = s.addressModeV = s.addressModeW =
            i == 1 ? VK_SAMPLER_ADDRESS_MODE_REPEAT
                   : VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
          s.compareEnable = i == 3;
          s.compareOp = VK_COMPARE_OP_LESS_OR_EQUAL;
          s.maxLod = VK_LOD_CLAMP_NONE;
          check (vkCreateSampler (m_device, &s, nullptr, &m_samplers[i]),
                 "sampler");
        }
      }

      void create_swapchain () {
        VkSurfaceCapabilitiesKHR caps;
        check (vkGetPhysicalDeviceSurfaceCapabilitiesKHR (m_physical,
                                                          m_surface, &caps),
               "surface capabilities");
        VkExtent2D extent { m_width, m_height };
        if (caps.currentExtent.width != 0xFFFFFFFFu)
          extent = caps.currentExtent;
        extent.width = std::clamp (extent.width, caps.minImageExtent.width,
                                   caps.maxImageExtent.width);
        extent.height = std::clamp (extent.height, caps.minImageExtent.height,
                                    caps.maxImageExtent.height);
        m_stale = false;
        if (extent.width == 0 || extent.height == 0)
          return;
        m_width = extent.width;
        m_height = extent.height;

        std::uint32_t count = 0;
        vkGetPhysicalDeviceSurfaceFormatsKHR (m_physical, m_surface, &count,
                                              nullptr);
        std::vector<VkSurfaceFormatKHR> formats (count);
        vkGetPhysicalDeviceSurfaceFormatsKHR (m_physical, m_surface, &count,
                                              formats.data ());
        // The surface format asked for, else an ordinary 8-bit one, in
        // the sRGB colour space the renderer's tonemap writes for.
        VkSurfaceFormatKHR format {};
        bool found = false;
        for (Format candidate :
             { m_surface_format, Format::bgra8_unorm, Format::rgba8_unorm })
          for (const auto& f : formats)
            if (!found && f.format == vk_format (candidate)
                && f.colorSpace == VK_COLOR_SPACE_SRGB_NONLINEAR_KHR) {
              format = f;
              m_surface_format = candidate;
              found = true;
            }
        if (!found)
          throw std::runtime_error ("NHAL: no usable surface format");

        VkPresentModeKHR mode = VK_PRESENT_MODE_FIFO_KHR;
        if (const char* v = moppe::environment ("MOPPE_VULKAN_PRESENT")) {
          const std::string wanted = v;
          if (wanted == "mailbox")
            mode = VK_PRESENT_MODE_MAILBOX_KHR;
          else if (wanted == "immediate")
            mode = VK_PRESENT_MODE_IMMEDIATE_KHR;
          std::uint32_t modes = 0;
          vkGetPhysicalDeviceSurfacePresentModesKHR (m_physical, m_surface,
                                                     &modes, nullptr);
          std::vector<VkPresentModeKHR> available (modes);
          vkGetPhysicalDeviceSurfacePresentModesKHR (
            m_physical, m_surface, &modes, available.data ());
          if (std::find (available.begin (), available.end (), mode)
              == available.end ())
            mode = VK_PRESENT_MODE_FIFO_KHR;
        }

        VkSwapchainCreateInfoKHR info {
          VK_STRUCTURE_TYPE_SWAPCHAIN_CREATE_INFO_KHR };
        info.surface = m_surface;
        info.minImageCount = std::max (caps.minImageCount, frames_in_flight);
        if (caps.maxImageCount)
          info.minImageCount = std::min (info.minImageCount,
                                         caps.maxImageCount);
        info.imageFormat = format.format;
        info.imageColorSpace = format.colorSpace;
        info.imageExtent = extent;
        info.imageArrayLayers = 1;
        info.imageUsage = VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT
                          | (caps.supportedUsageFlags
                             & VK_IMAGE_USAGE_TRANSFER_SRC_BIT);
        info.imageSharingMode = VK_SHARING_MODE_EXCLUSIVE;
        info.preTransform = caps.currentTransform;
        info.compositeAlpha =
          (caps.supportedCompositeAlpha & VK_COMPOSITE_ALPHA_OPAQUE_BIT_KHR)
            ? VK_COMPOSITE_ALPHA_OPAQUE_BIT_KHR
            : VkCompositeAlphaFlagBitsKHR (
                caps.supportedCompositeAlpha
                & -caps.supportedCompositeAlpha);
        info.presentMode = mode;
        info.clipped = VK_TRUE;
        if (m_present_stage)
          info.flags |= VK_SWAPCHAIN_CREATE_PRESENT_ID_2_BIT_KHR
                        | VK_SWAPCHAIN_CREATE_PRESENT_TIMING_BIT_EXT;
        check (vkCreateSwapchainKHR (m_device, &info, nullptr, &m_swapchain),
               "vkCreateSwapchainKHR");
        if (m_present_stage) {
          // Room for the reports of a second of frames between drains.
          check (m_timing_queue_size (m_device, m_swapchain, 64),
                 "vkSetSwapchainPresentTimingQueueSizeEXT");
          m_time_domain_ready = m_monotonic = m_refresh_known = false;
        }

        vkGetSwapchainImagesKHR (m_device, m_swapchain, &count, nullptr);
        std::vector<VkImage> images (count);
        vkGetSwapchainImagesKHR (m_device, m_swapchain, &count,
                                 images.data ());
        for (VkImage image : images) {
          VkTextureRow t;
          t.image = image;
          t.view = make_view (image, format.format, false);
          t.desc = { m_width, m_height, m_surface_format,
                     usage_render_target, 1, "backbuffer" };
          t.swapchain = true;
          m_backbuffers.push_back (m_textures.insert<Texture> (t));
          const VkSemaphoreCreateInfo binary {
            VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO };
          VkSemaphore done;
          check (vkCreateSemaphore (m_device, &binary, nullptr, &done),
                 "semaphore");
          m_render_done.push_back (done);
        }
      }

      // Headless: the backbuffers are the device's own images.
      void create_offscreen_backbuffers () {
        for (std::uint32_t i = 0; i < frames_in_flight; ++i)
          m_backbuffers.push_back (create_texture (
            { m_width, m_height, m_surface_format, usage_render_target, 1,
              "backbuffer" }));
      }

      void destroy_backbuffers () {
        for (Texture t : m_backbuffers) {
          VkTextureRow row = m_textures.take (t);
          vkDestroyImageView (m_device, row.view, nullptr);
          if (!row.swapchain) {
            vkDestroyImage (m_device, row.image, nullptr);
            vkFreeMemory (m_device, row.memory, nullptr);
          }
        }
        m_backbuffers.clear ();
        for (VkSemaphore s : m_render_done)
          vkDestroySemaphore (m_device, s, nullptr);
        m_render_done.clear ();
      }

      void rebuild_backbuffers () {
        wait_idle ();
        vkDeviceWaitIdle (m_device);
        destroy_backbuffers ();
        if (m_surface) {
          if (m_swapchain)
            vkDestroySwapchainKHR (m_device, m_swapchain, nullptr);
          m_swapchain = VK_NULL_HANDLE;
          create_swapchain ();
        } else {
          create_offscreen_backbuffers ();
        }
      }

      // -- memory -----------------------------------------------------

      VkDeviceMemory allocate_memory (const VkMemoryRequirements& needs,
                                      Placement placement) {
        const VkMemoryPropertyFlags required =
          placement == Placement::device
            ? VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT
            : VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT
                | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT;
        // Upload memory the GPU reads in place prefers its own heap where
        // the CPU can reach it; readbacks prefer cached memory.
        const VkMemoryPropertyFlags preferred =
          placement == Placement::upload
            ? required | VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT
          : placement == Placement::readback
            ? required | VK_MEMORY_PROPERTY_HOST_CACHED_BIT
            : required;
        int type = -1;
        for (VkMemoryPropertyFlags flags : { preferred, required }) {
          for (std::uint32_t i = 0; type < 0 && i < m_memory.memoryTypeCount;
               ++i)
            if ((needs.memoryTypeBits & (1u << i))
                && (m_memory.memoryTypes[i].propertyFlags & flags) == flags)
              type = int (i);
          if (type >= 0)
            break;
        }
        if (type < 0)
          throw std::runtime_error ("NHAL: no suitable Vulkan memory type");
        VkMemoryAllocateInfo info { VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO };
        info.allocationSize = needs.size;
        info.memoryTypeIndex = std::uint32_t (type);
        VkDeviceMemory memory;
        check (vkAllocateMemory (m_device, &info, nullptr, &memory),
               "vkAllocateMemory");
        return memory;
      }

      VkBufferRow make_buffer (VkDeviceSize size, VkBufferUsageFlags usage,
                               Placement placement) {
        VkBufferRow row;
        row.size = size;
        VkBufferCreateInfo info { VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO };
        info.size = size;
        info.usage = usage;
        info.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
        check (vkCreateBuffer (m_device, &info, nullptr, &row.buffer),
               "vkCreateBuffer");
        VkMemoryRequirements needs;
        vkGetBufferMemoryRequirements (m_device, row.buffer, &needs);
        row.memory = allocate_memory (needs, placement);
        check (vkBindBufferMemory (m_device, row.buffer, row.memory, 0),
               "bind buffer memory");
        if (placement != Placement::device)
          check (vkMapMemory (m_device, row.memory, 0, VK_WHOLE_SIZE, 0,
                              &row.mapped),
                 "map buffer");
        return row;
      }

      void release (VkBufferRow& row) {
        if (row.buffer)
          vkDestroyBuffer (m_device, row.buffer, nullptr);
        if (row.memory)
          vkFreeMemory (m_device, row.memory, nullptr);
        row = {};
      }

      VkBufferRow staging_buffer (std::span<const std::byte> data) {
        VkBufferRow row = make_buffer (std::max<std::size_t> (data.size (), 16),
                                       VK_BUFFER_USAGE_TRANSFER_SRC_BIT,
                                       Placement::upload);
        std::memcpy (row.mapped, data.data (), data.size ());
        return row;
      }

      VkImageView make_view (VkImage image, VkFormat format, bool depth) {
        VkImageViewCreateInfo info { VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO };
        info.image = image;
        info.viewType = VK_IMAGE_VIEW_TYPE_2D;
        info.format = format;
        info.subresourceRange = { depth ? VK_IMAGE_ASPECT_DEPTH_BIT
                                        : VK_IMAGE_ASPECT_COLOR_BIT,
                                  0, 1, 0, 1 };
        VkImageView view;
        check (vkCreateImageView (m_device, &info, nullptr, &view),
               "image view");
        return view;
      }

      // Where a texture waits between uses.
      static VkImageLayout resting_layout (const VkTextureRow& t) {
        return (t.desc.usage & usage_sampled)
                 ? VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL
                 : VK_IMAGE_LAYOUT_GENERAL;
      }

      VkCommandBuffer begin_setup () {
        check (vkResetCommandBuffer (m_setup, 0), "reset setup");
        VkCommandBufferBeginInfo begin {
          VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO };
        begin.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;
        check (vkBeginCommandBuffer (m_setup, &begin), "begin setup");
        return m_setup;
      }

      // Setup copies run on their own command buffer and finish before
      // returning, after whatever the queue already holds.
      void finish_setup () {
        check (vkEndCommandBuffer (m_setup), "end setup");
        VkSubmitInfo submit { VK_STRUCTURE_TYPE_SUBMIT_INFO };
        submit.commandBufferCount = 1;
        submit.pCommandBuffers = &m_setup;
        check (vkQueueSubmit (m_queue, 1, &submit, m_setup_fence),
               "setup submit");
        check (vkWaitForFences (m_device, 1, &m_setup_fence, VK_TRUE,
                                std::numeric_limits<std::uint64_t>::max ()),
               "setup wait");
        vkResetFences (m_device, 1, &m_setup_fence);
      }

      // -- layouts and bindings ---------------------------------------

      const Layout& program_layout (const Program& program) {
        std::unique_ptr<Layout>& cached = m_layouts[&program];
        if (cached)
          return *cached;
        auto layout = std::make_unique<Layout> ();
        std::vector<VkDescriptorSetLayoutBinding> bindings;
        for (const Resource& r : program.resources) {
          VkDescriptorSetLayoutBinding b {};
          b.binding = vk_binding (r);
          b.descriptorType = descriptor_type (r.kind);
          b.descriptorCount = 1;
          b.stageFlags = VK_SHADER_STAGE_ALL;
          if (is_sampler (r.kind))
            b.pImmutableSamplers = &m_samplers.at (r.binding);
          else
            layout->resources.push_back (r);
          bindings.push_back (b);
        }
        VkDescriptorSetLayoutCreateInfo set {
          VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO };
        set.flags = VK_DESCRIPTOR_SET_LAYOUT_CREATE_PUSH_DESCRIPTOR_BIT_KHR;
        set.bindingCount = std::uint32_t (bindings.size ());
        set.pBindings = bindings.data ();
        check (vkCreateDescriptorSetLayout (m_device, &set, nullptr,
                                            &layout->set),
               "descriptor set layout");
        VkPipelineLayoutCreateInfo info {
          VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO };
        info.setLayoutCount = 1;
        info.pSetLayouts = &layout->set;
        check (vkCreatePipelineLayout (m_device, &info, nullptr,
                                       &layout->pipeline),
               "pipeline layout");
        cached = std::move (layout);
        return *cached;
      }

      VkShaderModule shader_module (const StageCode& code,
                                    const Program* program,
                                    const char* stage) {
        if (code.spirv.empty ())
          throw std::runtime_error (std::string ("NHAL: ") + program->name
                                    + " has no SPIR-V " + stage + " stage");
        VkShaderModuleCreateInfo info {
          VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO };
        info.codeSize = code.spirv.size_bytes ();
        info.pCode = code.spirv.data ();
        VkShaderModule module;
        check (vkCreateShaderModule (m_device, &info, nullptr, &module),
               "shader module");
        return module;
      }

      static VkPipelineShaderStageCreateInfo
      stage_info (VkShaderStageFlagBits stage, VkShaderModule module,
                  const char* entry) {
        VkPipelineShaderStageCreateInfo info {
          VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO };
        info.stage = stage;
        info.module = module;
        info.pName = entry;
        return info;
      }

      void reset_bindings () {
        m_pipeline_bound = false;
        m_bindings_dirty = true;
      }

      bool attached (Texture handle) const {
        for (std::uint32_t i = 0; i < m_pass.color_count; ++i)
          if (m_pass.colors[i].texture == handle
              || m_pass.colors[i].resolve == handle)
            return true;
        return m_pass.depth.texture == handle;
      }

      // Binds the pipeline and pushes the bindings for the next draw or
      // dispatch, noting the layouts its textures need.
      void bind_arguments () {
        if (!m_pipeline)
          throw std::runtime_error ("NHAL: draw without a pipeline");
        const VkPipelineBindPoint point = m_pipeline->compute
                                            ? VK_PIPELINE_BIND_POINT_COMPUTE
                                            : VK_PIPELINE_BIND_POINT_GRAPHICS;
        if (!m_pipeline_bound) {
          vkCmdBindPipeline (m_cb, point, m_pipeline->pipeline);
          m_pipeline_bound = true;
        }
        if (!m_bindings_dirty)
          return;
        m_bindings_dirty = false;
        const Layout& layout = *m_pipeline->layout;
        std::array<VkWriteDescriptorSet, 3 * max_bindings> writes {};
        std::array<VkDescriptorBufferInfo, max_bindings> buffers {};
        std::array<VkDescriptorImageInfo, 2 * max_bindings> images {};
        std::uint32_t count = 0, buffer_count = 0, image_count = 0;
        for (const Resource& r : layout.resources) {
          VkWriteDescriptorSet& w = writes[count++];
          w.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
          w.dstBinding = vk_binding (r);
          w.descriptorCount = 1;
          w.descriptorType = descriptor_type (r.kind);
          if (is_buffer (r.kind)) {
            const BoundBuffer& bound = m_bound_buffers[r.binding];
            VkDescriptorBufferInfo& info = buffers[buffer_count++];
            info.buffer = bound.buffer;
            info.offset = bound.buffer ? bound.offset : 0;
            info.range = bound.buffer ? bound.range : VK_WHOLE_SIZE;
            if (r.kind == ResourceKind::uniform_block && bound.buffer)
              info.range = std::min<VkDeviceSize> (
                info.range, m_limits.maxUniformBufferRange);
            w.pBufferInfo = &info;
          } else {
            const bool storage = is_storage_texture (r.kind);
            const Texture handle = storage ? m_bound_storage[r.binding]
                                           : m_bound_textures[r.binding];
            VkDescriptorImageInfo& info = images[image_count++];
            info.imageLayout = storage
                                 ? VK_IMAGE_LAYOUT_GENERAL
                                 : VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
            if (handle)
              info.imageView = use_texture (handle, storage);
            w.pImageInfo = &info;
          }
        }
        if (count)
          m_push_descriptors (m_cb, point, layout.pipeline, 0, count,
                              writes.data ());
      }

      // The view a binding reads through, after noting its layout. In a
      // render pass the layout changes before the pass begins; a texture
      // the pass renders into reads as null, as on Direct3D 12.
      VkImageView use_texture (Texture handle, bool storage) {
        VkTextureRow& t = m_textures[handle];
        if (m_in_pass) {
          if (attached (handle)) {
            if (!t.warned) {
              t.warned = true;
              std::cerr << "NHAL: " << m_pipeline->name
                        << " binds attachment "
                        << (t.desc.label ? t.desc.label : "(unnamed)")
                        << std::endl;
            }
            return VK_NULL_HANDLE;
          }
          (storage ? m_pass_storage : m_pass_reads).insert (handle);
        } else {
          m_dispatch_transitions.push_back (
            { &t,
              storage ? VK_IMAGE_LAYOUT_GENERAL
                      : VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,
              false });
        }
        return t.view;
      }

      void prepare_dispatch () {
        m_dispatch_transitions.clear ();
        m_bindings_dirty = true;
        bind_arguments ();
        barrier (m_cb, m_dispatch_transitions);
      }

      void set_index_buffer (Buffer indices, IndexType type) {
        vkCmdBindIndexBuffer (m_cb, m_buffers[indices].buffer, 0,
                              type == IndexType::uint16
                                ? VK_INDEX_TYPE_UINT16
                                : VK_INDEX_TYPE_UINT32);
      }

      // A full memory barrier on `cb`, with the image layout changes
      // `transitions` ask for.
      void barrier (VkCommandBuffer cb,
                    std::span<const Transition> transitions = {}) {
        std::vector<VkImageMemoryBarrier2> images;
        for (const Transition& t : transitions) {
          VkTextureRow& row = *t.texture;
          if (row.layout == t.layout)
            continue;
          VkImageMemoryBarrier2 b { VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER_2 };
          b.srcStageMask = VK_PIPELINE_STAGE_2_ALL_COMMANDS_BIT;
          b.srcAccessMask = VK_ACCESS_2_MEMORY_WRITE_BIT;
          b.dstStageMask = VK_PIPELINE_STAGE_2_ALL_COMMANDS_BIT;
          b.dstAccessMask = VK_ACCESS_2_MEMORY_READ_BIT
                            | VK_ACCESS_2_MEMORY_WRITE_BIT;
          b.oldLayout = t.discard ? VK_IMAGE_LAYOUT_UNDEFINED : row.layout;
          b.newLayout = t.layout;
          b.srcQueueFamilyIndex = b.dstQueueFamilyIndex =
            VK_QUEUE_FAMILY_IGNORED;
          b.image = row.image;
          b.subresourceRange = { is_depth (row.desc.format)
                                   ? VK_IMAGE_ASPECT_DEPTH_BIT
                                   : VK_IMAGE_ASPECT_COLOR_BIT,
                                 0, 1, 0, 1 };
          images.push_back (b);
          row.layout = t.layout;
        }
        VkMemoryBarrier2 memory { VK_STRUCTURE_TYPE_MEMORY_BARRIER_2 };
        memory.srcStageMask = VK_PIPELINE_STAGE_2_ALL_COMMANDS_BIT;
        memory.srcAccessMask = VK_ACCESS_2_MEMORY_WRITE_BIT;
        memory.dstStageMask = VK_PIPELINE_STAGE_2_ALL_COMMANDS_BIT;
        memory.dstAccessMask = VK_ACCESS_2_MEMORY_READ_BIT
                               | VK_ACCESS_2_MEMORY_WRITE_BIT;
        VkDependencyInfo info { VK_STRUCTURE_TYPE_DEPENDENCY_INFO };
        info.memoryBarrierCount = 1;
        info.pMemoryBarriers = &memory;
        info.imageMemoryBarrierCount = std::uint32_t (images.size ());
        info.pImageMemoryBarriers = images.data ();
        vkCmdPipelineBarrier2 (cb, &info);
      }

      void barrier (VkCommandBuffer cb,
                    std::initializer_list<Transition> transitions) {
        barrier (cb, std::span<const Transition> (transitions.begin (),
                                                  transitions.size ()));
      }

      // -- timings, captures, and retirement ---------------------------

      void timestamp_begin (const char* label) {
        Frame& frame = m_frames[m_slot];
        if (frame.timestamps + 2 > max_timestamps)
          return;
        frame.labels.emplace_back (label);
        vkCmdWriteTimestamp2 (m_cb, VK_PIPELINE_STAGE_2_ALL_COMMANDS_BIT,
                              frame.queries, frame.timestamps++);
      }

      void timestamp_end () {
        Frame& frame = m_frames[m_slot];
        if (frame.timestamps >= max_timestamps
            || frame.timestamps >= 2 * frame.labels.size ())
          return;
        vkCmdWriteTimestamp2 (m_cb, VK_PIPELINE_STAGE_2_ALL_COMMANDS_BIT,
                              frame.queries, frame.timestamps++);
      }

      void resolve_timings (std::uint64_t completed) {
        for (Frame& frame : m_frames) {
          if (frame.resolved || frame.serial > completed
              || frame.timestamps < 2)
            continue;
          frame.resolved = true;
          std::array<std::uint64_t, max_timestamps> ticks {};
          if (vkGetQueryPoolResults (m_device, frame.queries, 0,
                                     frame.timestamps, sizeof ticks,
                                     ticks.data (), sizeof (std::uint64_t),
                                     VK_QUERY_RESULT_64_BIT)
              != VK_SUCCESS)
            continue;
          const std::uint64_t mask =
            m_timestamp_bits >= 64 ? ~0ull : (1ull << m_timestamp_bits) - 1;
          m_pass_timings.clear ();
          for (std::uint32_t i = 0; i + 1 < frame.timestamps; i += 2)
            m_pass_timings.push_back (
              { frame.labels[i / 2],
                double ((ticks[i + 1] - ticks[i]) & mask) * m_ms_per_tick });
        }
      }

      void encode_capture (VkTextureRow& back) {
        PendingCapture pending;
        const std::uint32_t row_bytes =
          back.desc.width * bytes_per_pixel (back.desc.format);
        pending.readback = make_buffer (
          VkDeviceSize (row_bytes) * back.desc.height,
          VK_BUFFER_USAGE_TRANSFER_DST_BIT, Placement::readback);
        barrier (m_cb, { { &back, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,
                               false } });
        VkBufferImageCopy region {};
        region.imageSubresource = { VK_IMAGE_ASPECT_COLOR_BIT, 0, 0, 1 };
        region.imageExtent = { back.desc.width, back.desc.height, 1 };
        vkCmdCopyImageToBuffer (m_cb, back.image,
                                VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,
                                pending.readback.buffer, 1, &region);
        pending.serial = m_serial;
        pending.capture = { back.desc.width, back.desc.height,
                            back.desc.format, row_bytes, {} };
        pending.done = std::move (m_capture_request);
        m_capture_request = nullptr;
        m_captures.push_back (std::move (pending));
      }

      std::uint64_t completed () const {
        std::uint64_t value = 0;
        vkGetSemaphoreCounterValue (m_device, m_timeline, &value);
        return value;
      }

      // A frame that never completes is a hung GPU; say which.
      void wait_for (std::uint64_t value) {
        if (!value || completed () >= value)
          return;
        VkSemaphoreWaitInfo info { VK_STRUCTURE_TYPE_SEMAPHORE_WAIT_INFO };
        info.semaphoreCount = 1;
        info.pSemaphores = &m_timeline;
        info.pValues = &value;
        const VkResult result =
          vkWaitSemaphores (m_device, &info, 5'000'000'000ull);
        if (result == VK_SUCCESS)
          return;
        throw std::runtime_error (
          "NHAL: frame " + std::to_string (value)
          + " did not complete (GPU completed " + std::to_string (completed ())
          + ", result " + std::to_string (int (result)) + ")");
      }

      void collect () {
        const std::uint64_t done = completed ();
        m_retired.collect (done, [&] (Retired& r) { release (r); });
        resolve_timings (done);
        for (auto it = m_captures.begin (); it != m_captures.end ();) {
          if (it->serial > done) {
            ++it;
            continue;
          }
          it->capture.pixels = {
            static_cast<const std::byte*> (it->readback.mapped),
            std::size_t (it->capture.row_bytes) * it->capture.height };
          it->done (it->capture);
          release (it->readback);
          it = m_captures.erase (it);
        }
      }

      void collect_all () {
        m_retired.collect (std::numeric_limits<std::uint64_t>::max (),
                           [&] (Retired& r) { release (r); });
        for (PendingCapture& c : m_captures)
          release (c.readback);
        m_captures.clear ();
      }

      void release (Retired& r) {
        if (r.pipeline)
          vkDestroyPipeline (m_device, r.pipeline, nullptr);
        if (r.view)
          vkDestroyImageView (m_device, r.view, nullptr);
        if (r.image)
          vkDestroyImage (m_device, r.image, nullptr);
        if (r.buffer)
          vkDestroyBuffer (m_device, r.buffer, nullptr);
        if (r.memory)
          vkFreeMemory (m_device, r.memory, nullptr);
      }

      void name (VkObjectType type, std::uint64_t handle, const char* label) {
        if (!m_set_name || !label)
          return;
        VkDebugUtilsObjectNameInfoEXT info {
          VK_STRUCTURE_TYPE_DEBUG_UTILS_OBJECT_NAME_INFO_EXT };
        info.objectType = type;
        info.objectHandle = handle;
        info.pObjectName = label;
        m_set_name (m_device, &info);
      }

      Format m_surface_format;
      std::uint32_t m_width, m_height;
      std::string m_adapter;
      bool m_validation = false;
      bool m_debug_utils = false;
      VkInstance m_instance = VK_NULL_HANDLE;
      VkDebugUtilsMessengerEXT m_messenger = VK_NULL_HANDLE;
      VkSurfaceKHR m_surface = VK_NULL_HANDLE;
      VkPhysicalDevice m_physical = VK_NULL_HANDLE;
      VkPhysicalDeviceLimits m_limits {};
      VkPhysicalDeviceMemoryProperties m_memory {};
      std::uint32_t m_family = 0;
      std::uint32_t m_timestamp_bits = 64;
      double m_ms_per_tick = 1e-6;
      VkDevice m_device = VK_NULL_HANDLE;
      VkQueue m_queue = VK_NULL_HANDLE;
      PFN_vkCmdPushDescriptorSetKHR m_push_descriptors = nullptr;
      PFN_vkSetDebugUtilsObjectNameEXT m_set_name = nullptr;
      bool m_surface_capabilities_2 = false;
      VkPresentStageFlagsEXT m_present_stage = 0;
      PFN_vkSetSwapchainPresentTimingQueueSizeEXT m_timing_queue_size = nullptr;
      PFN_vkGetSwapchainTimingPropertiesEXT m_timing_properties = nullptr;
      PFN_vkGetSwapchainTimeDomainPropertiesEXT m_time_domains = nullptr;
      PFN_vkGetPastPresentationTimingEXT m_past_timing = nullptr;
      bool m_time_domain_ready = false, m_monotonic = false;
      bool m_refresh_known = false;
      VkTimeDomainKHR m_time_domain = VK_TIME_DOMAIN_CLOCK_MONOTONIC_KHR;
      std::uint64_t m_time_domain_id = 0;
      PFN_vkGetCalibratedTimestampsKHR m_calibrate = nullptr;
      PresentationClock m_presentation;
      VkSemaphore m_timeline = VK_NULL_HANDLE;
      std::array<Frame, frames_in_flight> m_frames;
      VkCommandPool m_setup_pool = VK_NULL_HANDLE;
      VkCommandBuffer m_setup = VK_NULL_HANDLE;
      VkFence m_setup_fence = VK_NULL_HANDLE;
      std::array<VkSampler, standard_samplers> m_samplers {};

      VkSwapchainKHR m_swapchain = VK_NULL_HANDLE;
      std::vector<Texture> m_backbuffers;
      std::vector<VkSemaphore> m_render_done;
      std::uint32_t m_image_index = 0;
      bool m_stale = false;

      Table<VkBufferRow> m_buffers;
      Table<VkTextureRow> m_textures;
      Table<VkPipelineRow> m_pipelines;
      Retirement<Retired> m_retired;
      std::unordered_map<const Program*, std::unique_ptr<Layout>> m_layouts;

      std::uint64_t m_serial = 0;
      std::uint32_t m_slot = 0;
      Arena m_arena;
      VkCommandBuffer m_cb = VK_NULL_HANDLE;
      Texture m_current_backbuffer;
      RenderPassDesc m_pass;
      bool m_in_pass = false;
      VkExtent2D m_pass_extent {};
      std::unordered_set<Texture, TextureHash> m_pass_reads, m_pass_storage;
      std::vector<Transition> m_dispatch_transitions;
      const VkPipelineRow* m_pipeline = nullptr;
      bool m_pipeline_bound = false;
      bool m_bindings_dirty = true;
      std::array<BoundBuffer, max_bindings> m_bound_buffers {};
      std::array<Texture, max_bindings> m_bound_textures {};
      std::array<Texture, max_bindings> m_bound_storage {};
      std::vector<PassTiming> m_pass_timings;
      std::function<void (const Capture&)> m_capture_request;
      std::vector<PendingCapture> m_captures;
    };
  }

  std::unique_ptr<Device> create_vulkan_device (const VulkanSurface& surface,
                                                std::uint32_t width,
                                                std::uint32_t height,
                                                Format surface_format) {
    return std::make_unique<VulkanDevice> (surface, width, height,
                                           surface_format);
  }
}
