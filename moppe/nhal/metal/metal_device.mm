// NHAL on Metal 4: MTL4 command queues and allocators, one argument table
// per stage holding buffer addresses, texture IDs, and the standard
// samplers, a residency set for everything the device creates, and a shared
// upload arena per frame slot. Apple GPUs share memory with the CPU, so
// buffers and sampled textures are shared storage and written in place.
#import <Metal/Metal.h>
#import <QuartzCore/CAMetalLayer.h>

#include <moppe/nhal/metal/metal_device.hh>
#include <moppe/nhal/table.hh>

#include <array>
#include <functional>
#include <vector>
#include <stdexcept>
#include <string>
#include <unordered_map>

namespace moppe::nhal {
  namespace {
    constexpr std::uint32_t frames_in_flight = 3;
    constexpr std::uint64_t arena_capacity = 16u << 20;
    constexpr NSUInteger max_buffers = 16;
    // Sampled textures at 0-15, storage textures at 16-31.
    constexpr NSUInteger max_textures = 32;
    constexpr NSUInteger storage_texture_base = 16;
    constexpr NSUInteger max_samplers = 4;
    constexpr NSUInteger max_timestamps = 64;

    std::string describe (NSError* error) {
      return error ? error.localizedDescription.UTF8String : "unknown error";
    }

    MTLPixelFormat pixel_format (Format format) {
      switch (format) {
      case Format::undefined: return MTLPixelFormatInvalid;
      case Format::rgba8_unorm: return MTLPixelFormatRGBA8Unorm;
      case Format::rgba8_unorm_srgb: return MTLPixelFormatRGBA8Unorm_sRGB;
      case Format::bgra8_unorm: return MTLPixelFormatBGRA8Unorm;
      case Format::bgra8_unorm_srgb: return MTLPixelFormatBGRA8Unorm_sRGB;
      case Format::rgb10a2_unorm: return MTLPixelFormatRGB10A2Unorm;
      case Format::rgba16_float: return MTLPixelFormatRGBA16Float;
      case Format::rg16_float: return MTLPixelFormatRG16Float;
      case Format::rg16_snorm: return MTLPixelFormatRG16Snorm;
      case Format::r16_float: return MTLPixelFormatR16Float;
      case Format::r8_unorm: return MTLPixelFormatR8Unorm;
      case Format::r32_float: return MTLPixelFormatR32Float;
      case Format::rg32_float: return MTLPixelFormatRG32Float;
      case Format::rgba32_float: return MTLPixelFormatRGBA32Float;
      case Format::r32_uint: return MTLPixelFormatR32Uint;
      case Format::d32_float: return MTLPixelFormatDepth32Float;
      }
      return MTLPixelFormatInvalid;
    }

    MTLCompareFunction compare_function (CompareOp op) {
      switch (op) {
      case CompareOp::never: return MTLCompareFunctionNever;
      case CompareOp::less: return MTLCompareFunctionLess;
      case CompareOp::equal: return MTLCompareFunctionEqual;
      case CompareOp::less_equal: return MTLCompareFunctionLessEqual;
      case CompareOp::greater: return MTLCompareFunctionGreater;
      case CompareOp::not_equal: return MTLCompareFunctionNotEqual;
      case CompareOp::greater_equal: return MTLCompareFunctionGreaterEqual;
      case CompareOp::always: return MTLCompareFunctionAlways;
      }
      return MTLCompareFunctionAlways;
    }

    MTLLoadAction load_action (Load load) {
      switch (load) {
      case Load::load: return MTLLoadActionLoad;
      case Load::clear: return MTLLoadActionClear;
      case Load::discard: return MTLLoadActionDontCare;
      }
      return MTLLoadActionDontCare;
    }

    struct MetalBuffer {
      id<MTLBuffer> buffer = nil;
    };

    struct MetalTexture {
      id<MTLTexture> texture = nil;
      TextureDesc desc {};
    };

    struct MetalPipeline {
      id<MTLRenderPipelineState> state = nil;
      id<MTLComputePipelineState> compute = nil;
      MTLSize threads = MTLSizeMake (1, 1, 1);
      id<MTLDepthStencilState> depth = nil;
      MTLCullMode cull = MTLCullModeNone;
      MTLWinding winding = MTLWindingCounterClockwise;
      MTLPrimitiveType primitive = MTLPrimitiveTypeTriangle;
      float depth_bias = 0, slope_scaled_depth_bias = 0;
    };

    class MetalDevice final : public Device {
    public:
      MetalDevice (CAMetalLayer* layer, Format surface_format)
        : m_layer (layer), m_surface_format (surface_format) {
        m_device = layer.device ? layer.device
                                : MTLCreateSystemDefaultDevice ();
        if (!m_device)
          throw std::runtime_error ("NHAL: no Metal device");
        m_queue = [m_device newMTL4CommandQueue];
        if (!m_queue)
          throw std::runtime_error ("NHAL: Metal 4 is unavailable");
        m_event = [m_device newSharedEvent];
        m_event.signaledValue = 0;

        NSError* error = nil;
        MTLResidencySetDescriptor* residency =
          [[MTLResidencySetDescriptor alloc] init];
        residency.initialCapacity = 256;
        residency.label = @"NHAL resources";
        m_residency = [m_device newResidencySetWithDescriptor:residency
                                                        error:&error];
        if (!m_residency)
          throw std::runtime_error ("NHAL: residency set: "
                                    + describe (error));
        [m_queue addResidencySet:m_residency];

        layer.device = m_device;
        layer.pixelFormat = pixel_format (surface_format);
        // Captures copy the drawable, so it cannot be framebuffer-only.
        layer.framebufferOnly = NO;
        if (surface_format == Format::rgba16_float) {
          CGColorSpaceRef linear =
            CGColorSpaceCreateWithName (kCGColorSpaceExtendedLinearSRGB);
          layer.colorspace = linear;
          CGColorSpaceRelease (linear);
#if !TARGET_OS_IPHONE
          layer.wantsExtendedDynamicRangeContent = YES;
#endif
        }
        [m_queue addResidencySet:layer.residencySet];

        for (std::uint32_t i = 0; i < frames_in_flight; ++i) {
          m_allocators[i] = [m_device newCommandAllocator];
          m_arena_buffers[i] =
            [m_device newBufferWithLength:arena_capacity
                                  options:MTLResourceStorageModeShared];
          m_arena_buffers[i].label =
            [NSString stringWithFormat:@"NHAL frame arena %u", i];
          [m_residency addAllocation:m_arena_buffers[i]];
        }
        m_residency_dirty = true;

        MTL4ArgumentTableDescriptor* table =
          [[MTL4ArgumentTableDescriptor alloc] init];
        table.maxBufferBindCount = max_buffers;
        table.maxTextureBindCount = max_textures;
        table.maxSamplerStateBindCount = max_samplers;
        table.initializeBindings = YES;
        table.label = @"NHAL vertex bindings";
        m_vertex_table = [m_device newArgumentTableWithDescriptor:table
                                                            error:&error];
        table.label = @"NHAL fragment bindings";
        m_fragment_table = [m_device newArgumentTableWithDescriptor:table
                                                              error:&error];
        table.label = @"NHAL compute bindings";
        m_compute_table = [m_device newArgumentTableWithDescriptor:table
                                                             error:&error];
        if (!m_vertex_table || !m_fragment_table || !m_compute_table)
          throw std::runtime_error ("NHAL: argument table: "
                                    + describe (error));
        make_samplers ();

        m_ticks_per_ms = [m_device queryTimestampFrequency] / 1000.0;
        MTL4CounterHeapDescriptor* counters =
          [[MTL4CounterHeapDescriptor alloc] init];
        counters.type = MTL4CounterHeapTypeTimestamp;
        counters.count = max_timestamps;
        for (std::uint32_t i = 0; i < frames_in_flight; ++i)
          m_timing[i].heap = [m_device newCounterHeapWithDescriptor:counters
                                                              error:&error];

        m_backbuffer = m_textures.insert<Texture> (MetalTexture {});
      }

      ~MetalDevice () override {
        wait_idle ();
      }

      DeviceInfo info () const override {
        return { "Metal 4", m_device.name.UTF8String, frames_in_flight };
      }

      Buffer create_buffer (const BufferDesc& desc,
                            std::span<const std::byte> initial) override {
        id<MTLBuffer> buffer =
          [m_device newBufferWithLength:std::max<std::uint64_t> (desc.size, 16)
                                options:MTLResourceStorageModeShared];
        if (!buffer)
          throw std::runtime_error ("NHAL: could not allocate a buffer");
        if (desc.label)
          buffer.label = @(desc.label);
        if (!initial.empty ())
          std::memcpy (buffer.contents, initial.data (),
                       std::min<std::size_t> (initial.size (), desc.size));
        make_resident (buffer);
        return m_buffers.insert<Buffer> (MetalBuffer { buffer });
      }

      void* contents (Buffer buffer) override {
        return m_buffers[buffer].buffer.contents;
      }

      Texture create_texture (const TextureDesc& desc) override {
        MTLTextureDescriptor* td = [[MTLTextureDescriptor alloc] init];
        td.textureType = desc.samples > 1 ? MTLTextureType2DMultisample
                                          : MTLTextureType2D;
        td.pixelFormat = pixel_format (desc.format);
        td.width = desc.width;
        td.height = desc.height;
        td.sampleCount = desc.samples;
        td.usage = MTLTextureUsageUnknown;
        if (desc.usage & usage_sampled)
          td.usage |= MTLTextureUsageShaderRead;
        if (desc.usage & (usage_render_target | usage_depth))
          td.usage |= MTLTextureUsageRenderTarget;
        if (desc.usage & usage_storage)
          td.usage |= MTLTextureUsageShaderRead | MTLTextureUsageShaderWrite;
        // Sampled-only textures are written in place by the CPU.
        td.storageMode = desc.usage == usage_sampled
                           ? MTLStorageModeShared
                           : MTLStorageModePrivate;
        id<MTLTexture> texture = [m_device newTextureWithDescriptor:td];
        if (!texture)
          throw std::runtime_error ("NHAL: could not create a texture");
        if (desc.label)
          texture.label = @(desc.label);
        make_resident (texture);
        return m_textures.insert<Texture> (MetalTexture { texture, desc });
      }

      // Sampled-only textures are shared memory: the write lands at once,
      // so a texture must not be rewritten while a frame still reads it.
      void write_texture (Texture handle, std::span<const std::byte> pixels,
                          std::uint32_t row_bytes) override {
        MetalTexture& t = m_textures[handle];
        if (t.texture.storageMode != MTLStorageModeShared)
          throw std::invalid_argument (
            "NHAL: only sampled-only textures can be written");
        if (!row_bytes)
          row_bytes = t.desc.width * bytes_per_pixel (t.desc.format);
        [t.texture replaceRegion:MTLRegionMake2D (0, 0, t.desc.width,
                                                  t.desc.height)
                     mipmapLevel:0
                       withBytes:pixels.data ()
                     bytesPerRow:row_bytes];
      }

      Pipeline create_render_pipeline (const RenderPipelineDesc& desc)
        override {
        const Program& program = *desc.program;
        MTLRenderPipelineDescriptor* d =
          [[MTLRenderPipelineDescriptor alloc] init];
        d.vertexFunction = function (desc.vertex.msl, program.vertex_entry);
        if (program.fragment_entry)
          d.fragmentFunction =
            function (desc.fragment.msl, program.fragment_entry);
        d.rasterSampleCount = desc.samples;
        d.label = @(desc.label ? desc.label : program.name);
        for (std::uint32_t i = 0; i < desc.color_count; ++i) {
          auto* color = d.colorAttachments[i];
          color.pixelFormat = pixel_format (desc.color_formats[i]);
          if (desc.blend[i] == Blend::none)
            continue;
          color.blendingEnabled = YES;
          const bool additive = desc.blend[i] == Blend::additive;
          color.sourceRGBBlendFactor = MTLBlendFactorSourceAlpha;
          color.destinationRGBBlendFactor =
            additive ? MTLBlendFactorOne : MTLBlendFactorOneMinusSourceAlpha;
          color.sourceAlphaBlendFactor = MTLBlendFactorOne;
          color.destinationAlphaBlendFactor =
            additive ? MTLBlendFactorOne : MTLBlendFactorOneMinusSourceAlpha;
        }
        d.depthAttachmentPixelFormat = pixel_format (desc.depth_format);
        if (desc.topology == Topology::line_list)
          d.inputPrimitiveTopology = MTLPrimitiveTopologyClassLine;

        NSError* error = nil;
        MetalPipeline pipeline;
        pipeline.state = [m_device newRenderPipelineStateWithDescriptor:d
                                                                  error:&error];
        if (!pipeline.state)
          throw std::runtime_error (std::string ("NHAL: pipeline ")
                                    + program.name + ": " + describe (error));

        MTLDepthStencilDescriptor* depth =
          [[MTLDepthStencilDescriptor alloc] init];
        const bool has_depth = desc.depth_format != Format::undefined;
        depth.depthCompareFunction = has_depth
                                       ? compare_function (desc.depth_compare)
                                       : MTLCompareFunctionAlways;
        depth.depthWriteEnabled = has_depth && desc.depth_write;
        pipeline.depth = [m_device newDepthStencilStateWithDescriptor:depth];

        pipeline.cull = desc.cull == Cull::back    ? MTLCullModeBack
                        : desc.cull == Cull::front ? MTLCullModeFront
                                                   : MTLCullModeNone;
        pipeline.depth_bias = desc.depth_bias;
        pipeline.slope_scaled_depth_bias = desc.slope_scaled_depth_bias;
        pipeline.winding = desc.front_counter_clockwise
                             ? MTLWindingCounterClockwise
                             : MTLWindingClockwise;
        pipeline.primitive =
          desc.topology == Topology::triangle_strip ? MTLPrimitiveTypeTriangleStrip
          : desc.topology == Topology::line_list    ? MTLPrimitiveTypeLine
                                                    : MTLPrimitiveTypeTriangle;
        return m_pipelines.insert<Pipeline> (pipeline);
      }

      Pipeline create_compute_pipeline (const ComputePipelineDesc& desc)
        override {
        const Program& program = *desc.program;
        NSError* error = nil;
        MetalPipeline pipeline;
        pipeline.compute = [m_device
          newComputePipelineStateWithFunction:function (desc.compute.msl,
                                                        program.compute_entry)
                                        error:&error];
        if (!pipeline.compute)
          throw std::runtime_error (std::string ("NHAL: compute pipeline ")
                                    + program.name + ": " + describe (error));
        pipeline.threads = MTLSizeMake (
          std::max (1u, program.workgroup_size[0]),
          std::max (1u, program.workgroup_size[1]),
          std::max (1u, program.workgroup_size[2]));
        return m_pipelines.insert<Pipeline> (pipeline);
      }

      void destroy (Buffer buffer) override {
        m_retired_allocations.retire (m_serial, m_buffers.take (buffer).buffer);
      }

      void destroy (Texture texture) override {
        m_retired_allocations.retire (m_serial,
                                      m_textures.take (texture).texture);
      }

      void destroy (Pipeline pipeline) override {
        MetalPipeline p = m_pipelines.take (pipeline);
        if (p.state)
          m_retired_objects.retire (m_serial, p.state);
        if (p.depth)
          m_retired_objects.retire (m_serial, p.depth);
        if (p.compute)
          m_retired_objects.retire (m_serial, p.compute);
      }

      Format surface_format () const override { return m_surface_format; }
      std::uint32_t surface_width () const override {
        return static_cast<std::uint32_t> (m_layer.drawableSize.width);
      }
      std::uint32_t surface_height () const override {
        return static_cast<std::uint32_t> (m_layer.drawableSize.height);
      }
      void resize_surface (std::uint32_t width, std::uint32_t height)
        override {
        m_layer.drawableSize = CGSizeMake (width, height);
      }

      bool begin_frame () override {
        const std::uint64_t next = m_serial + 1;
        if (next > frames_in_flight
            && ![m_event waitUntilSignaledValue:next - frames_in_flight
                                      timeoutMS:2000])
          throw std::runtime_error ("NHAL: timed out waiting for a frame");
        collect ();

        m_drawable = m_offered ? m_offered : [m_layer nextDrawable];
        m_offered = nil;
        if (!m_drawable)
          return false;
        m_textures[m_backbuffer].texture = m_drawable.texture;
        m_textures[m_backbuffer].desc = {
          static_cast<std::uint32_t> (m_drawable.texture.width),
          static_cast<std::uint32_t> (m_drawable.texture.height),
          m_surface_format, usage_render_target, 1, "backbuffer" };

        m_serial = next;
        const std::uint32_t slot = (m_serial - 1) % frames_in_flight;
        m_slot = slot;
        m_arena = { static_cast<std::byte*> (m_arena_buffers[slot].contents),
                    m_arena_buffers[slot].gpuAddress, arena_capacity, 0 };
        [m_allocators[slot] reset];
        m_commands = [m_device newCommandBuffer];
        [m_commands beginCommandBufferWithAllocator:m_allocators[slot]];
        FrameTiming& timing = m_timing[slot];
        timing.serial = m_serial;
        timing.labels.clear ();
        timing.count = 0;
        if (timing.heap)
          [timing.heap invalidateCounterRange:NSMakeRange (0, max_timestamps)];
        return true;
      }

      Texture backbuffer () override { return m_backbuffer; }

      void offer_drawable (void* drawable) override {
        m_offered = (__bridge id<CAMetalDrawable>)drawable;
      }

      Transient allocate (std::uint64_t size, std::uint64_t alignment)
        override {
        if (!m_arena.fits (size, alignment))
          throw std::runtime_error ("NHAL: frame arena exhausted");
        return m_arena.allocate (size, alignment);
      }

      void begin_render_pass (const RenderPassDesc& desc) override {
        MTL4RenderPassDescriptor* pass =
          [[MTL4RenderPassDescriptor alloc] init];
        std::uint32_t width = 0, height = 0;
        for (std::uint32_t i = 0; i < desc.color_count; ++i) {
          const ColorAttachment& color = desc.colors[i];
          MetalTexture& t = m_textures[color.texture];
          auto* attachment = pass.colorAttachments[i];
          attachment.texture = t.texture;
          attachment.loadAction = load_action (color.load);
          attachment.clearColor = MTLClearColorMake (
            color.clear[0], color.clear[1], color.clear[2], color.clear[3]);
          if (color.resolve) {
            attachment.resolveTexture = m_textures[color.resolve].texture;
            attachment.storeAction =
              color.store == Store::store
                ? MTLStoreActionStoreAndMultisampleResolve
                : MTLStoreActionMultisampleResolve;
          } else {
            attachment.storeAction = color.store == Store::store
                                       ? MTLStoreActionStore
                                       : MTLStoreActionDontCare;
          }
          width = t.desc.width;
          height = t.desc.height;
        }
        if (desc.depth.texture) {
          MetalTexture& t = m_textures[desc.depth.texture];
          pass.depthAttachment.texture = t.texture;
          pass.depthAttachment.loadAction = load_action (desc.depth.load);
          pass.depthAttachment.storeAction = desc.depth.store == Store::store
                                               ? MTLStoreActionStore
                                               : MTLStoreActionDontCare;
          pass.depthAttachment.clearDepth = desc.depth.clear;
          width = t.desc.width;
          height = t.desc.height;
        }
        timestamp_pass (desc.label ? desc.label : "render");
        m_encoder = [m_commands renderCommandEncoderWithDescriptor:pass];
        if (desc.label)
          m_encoder.label = @(desc.label);
        // Coarse ordering: whatever earlier passes wrote is visible here.
        [m_encoder barrierAfterQueueStages:MTLStageVertex | MTLStageFragment
                                           | MTLStageBlit | MTLStageDispatch
                              beforeStages:MTLStageVertex | MTLStageFragment
                         visibilityOptions:MTL4VisibilityOptionDevice];
        set_viewport (0, 0, float (width), float (height));
      }

      void begin_compute_pass (const char* label) override {
        timestamp_pass (label ? label : "compute");
        m_compute = [m_commands computeCommandEncoder];
        if (label)
          m_compute.label = @(label);
        [m_compute barrierAfterQueueStages:MTLStageVertex | MTLStageFragment
                                           | MTLStageBlit | MTLStageDispatch
                              beforeStages:MTLStageDispatch | MTLStageBlit
                         visibilityOptions:MTL4VisibilityOptionDevice];
        m_dispatched = false;
      }

      void end_compute_pass () override {
        [m_compute endEncoding];
        m_compute = nil;
        timestamp ();
      }

      void copy_to_buffer (Buffer target, std::uint64_t offset,
                           const Transient& source) override {
        id<MTL4ComputeCommandEncoder> copy = [m_commands computeCommandEncoder];
        [copy barrierAfterQueueStages:MTLStageVertex | MTLStageFragment
                                      | MTLStageBlit | MTLStageDispatch
                         beforeStages:MTLStageBlit
                    visibilityOptions:MTL4VisibilityOptionDevice];
        id<MTLBuffer> arena = m_arena_buffers[m_slot];
        [copy copyFromBuffer:arena
                sourceOffset:source.gpu_address - arena.gpuAddress
                    toBuffer:m_buffers[target].buffer
           destinationOffset:offset
                        size:source.size];
        [copy endEncoding];
      }

      void dispatch (std::uint32_t x, std::uint32_t y, std::uint32_t z)
        override {
        before_dispatch ();
        [m_compute dispatchThreadgroups:MTLSizeMake (x, y, z)
                  threadsPerThreadgroup:m_pipeline.threads];
      }

      void dispatch_indirect (Buffer arguments, std::uint64_t offset)
        override {
        before_dispatch ();
        [m_compute
          dispatchThreadgroupsWithIndirectBuffer:m_buffers[arguments]
                                                   .buffer.gpuAddress
                                                 + offset
                           threadsPerThreadgroup:m_pipeline.threads];
      }

      void draw_indirect (Buffer arguments, std::uint64_t offset) override {
        use_arguments ();
        [m_encoder drawPrimitives:m_pipeline.primitive
                   indirectBuffer:m_buffers[arguments].buffer.gpuAddress
                                  + offset];
      }

      void draw_indexed_indirect (Buffer indices, IndexType type,
                                  Buffer arguments, std::uint64_t offset)
        override {
        use_arguments ();
        id<MTLBuffer> index_buffer = m_buffers[indices].buffer;
        [m_encoder
          drawIndexedPrimitives:m_pipeline.primitive
                      indexType:type == IndexType::uint16
                                  ? MTLIndexTypeUInt16
                                  : MTLIndexTypeUInt32
                    indexBuffer:index_buffer.gpuAddress
              indexBufferLength:index_buffer.length
                 indirectBuffer:m_buffers[arguments].buffer.gpuAddress
                                + offset];
      }

      void end_render_pass () override {
        [m_encoder endEncoding];
        m_encoder = nil;
        timestamp ();
      }

      void set_pipeline (Pipeline handle) override {
        m_pipeline = m_pipelines[handle];
        if (m_pipeline.compute) {
          [m_compute setComputePipelineState:m_pipeline.compute];
          return;
        }
        [m_encoder setRenderPipelineState:m_pipeline.state];
        [m_encoder setDepthStencilState:m_pipeline.depth];
        [m_encoder setCullMode:m_pipeline.cull];
        [m_encoder setFrontFacingWinding:m_pipeline.winding];
        [m_encoder setDepthBias:m_pipeline.depth_bias
                     slopeScale:m_pipeline.slope_scaled_depth_bias
                          clamp:0];
      }

      void set_buffer (std::uint32_t binding, Buffer buffer,
                       std::uint64_t offset) override {
        bind_address (binding, m_buffers[buffer].buffer.gpuAddress + offset);
      }

      void set_buffer (std::uint32_t binding, const Transient& slice)
        override {
        bind_address (binding, slice.gpu_address);
      }

      void set_texture (std::uint32_t binding, Texture texture) override {
        bind_texture (binding, m_textures[texture].texture.gpuResourceID);
      }

      void set_storage_texture (std::uint32_t binding, Texture texture)
        override {
        bind_texture (storage_texture_base + binding,
                      m_textures[texture].texture.gpuResourceID);
      }

      void set_viewport (float x, float y, float width, float height)
        override {
        [m_encoder setViewport:MTLViewport { x, y, width, height, 0, 1 }];
      }

      void draw (std::uint32_t vertex_count, std::uint32_t instance_count,
                 std::uint32_t first_vertex, std::uint32_t first_instance)
        override {
        use_arguments ();
        [m_encoder drawPrimitives:m_pipeline.primitive
                      vertexStart:first_vertex
                      vertexCount:vertex_count
                    instanceCount:instance_count
                     baseInstance:first_instance];
      }

      void draw_indexed (Buffer indices, IndexType type,
                         std::uint32_t index_count,
                         std::uint32_t instance_count,
                         std::uint32_t first_index, std::int32_t base_vertex,
                         std::uint32_t first_instance) override {
        use_arguments ();
        const std::uint32_t size = type == IndexType::uint16 ? 2 : 4;
        [m_encoder
          drawIndexedPrimitives:m_pipeline.primitive
                     indexCount:index_count
                      indexType:type == IndexType::uint16
                                  ? MTLIndexTypeUInt16
                                  : MTLIndexTypeUInt32
                    indexBuffer:m_buffers[indices].buffer.gpuAddress
                                + std::uint64_t (first_index) * size
              indexBufferLength:std::uint64_t (index_count) * size
                  instanceCount:instance_count
                     baseVertex:base_vertex
                   baseInstance:first_instance];
      }

      void capture_frame (std::function<void (const Capture&)> done)
        override {
        m_capture_request = std::move (done);
      }

      void end_frame () override {
        if (m_encoder)
          end_render_pass ();
        if (m_compute)
          end_compute_pass ();
        if (m_capture_request)
          encode_capture ();
        [m_commands endCommandBuffer];
        [m_queue waitForDrawable:m_drawable];
        MTL4CommitOptions* options = [[MTL4CommitOptions alloc] init];
        [options addFeedbackHandler:^(id<MTL4CommitFeedback> feedback) {
          if (feedback.error)
            NSLog (@"NHAL: Metal 4 submission failed: %@",
                   feedback.error.localizedDescription);
        }];
        const id<MTL4CommandBuffer> commands[] = { m_commands };
        [m_queue commit:commands count:1 options:options];
        [m_queue signalEvent:m_event value:m_serial];
        [m_queue signalDrawable:m_drawable];
        [m_drawable present];
        m_drawable = nil;
        m_commands = nil;
        m_textures[m_backbuffer].texture = nil;
      }

      std::span<const PassTiming> pass_timings () const override {
        return m_pass_timings;
      }

      void wait_idle () override {
        if (m_serial)
          [m_event waitUntilSignaledValue:m_serial timeoutMS:5000];
        collect ();
      }

    private:
      // One frame slot's timestamps: a pair around each pass.
      struct FrameTiming {
        id<MTL4CounterHeap> heap = nil;
        std::uint64_t serial = 0;
        std::vector<std::string> labels;
        NSUInteger count = 0;
        bool resolved = true;
      };

      void timestamp_pass (const char* label) {
        FrameTiming& timing = m_timing[m_slot];
        if (!timing.heap || timing.count + 2 > max_timestamps)
          return;
        timing.labels.emplace_back (label);
        timestamp ();
      }

      void timestamp () {
        FrameTiming& timing = m_timing[m_slot];
        if (!timing.heap || timing.count >= max_timestamps
            || timing.count >= 2 * timing.labels.size ())
          return;
        [m_commands writeTimestampIntoHeap:timing.heap atIndex:timing.count];
        ++timing.count;
        timing.resolved = false;
      }

      void resolve_timings (std::uint64_t completed) {
        for (FrameTiming& timing : m_timing) {
          if (timing.resolved || timing.serial > completed
              || timing.count < 2)
            continue;
          timing.resolved = true;
          NSData* data =
            [timing.heap resolveCounterRange:NSMakeRange (0, timing.count)];
          if (!data)
            continue;
          const auto* ticks =
            static_cast<const MTL4TimestampHeapEntry*> (data.bytes);
          m_pass_timings.clear ();
          for (std::size_t i = 0; i + 1 < timing.count; i += 2)
            m_pass_timings.push_back (
              { timing.labels[i / 2],
                double (ticks[i + 1].timestamp - ticks[i].timestamp)
                  / m_ticks_per_ms });
        }
      }

      struct PendingCapture {
        std::uint64_t serial = 0;
        id<MTLBuffer> buffer = nil;
        Capture capture;
        std::function<void (const Capture&)> done;
      };

      void encode_capture () {
        id<MTLTexture> texture = m_drawable.texture;
        PendingCapture pending;
        pending.serial = m_serial;
        pending.capture.width = static_cast<std::uint32_t> (texture.width);
        pending.capture.height = static_cast<std::uint32_t> (texture.height);
        pending.capture.format = m_surface_format;
        pending.capture.row_bytes =
          pending.capture.width * bytes_per_pixel (m_surface_format);
        const std::uint64_t size =
          std::uint64_t (pending.capture.row_bytes) * pending.capture.height;
        pending.buffer =
          [m_device newBufferWithLength:size
                                options:MTLResourceStorageModeShared];
        [m_residency addAllocation:pending.buffer];
        [m_residency commit];
        id<MTL4ComputeCommandEncoder> copy = [m_commands computeCommandEncoder];
        [copy barrierAfterQueueStages:MTLStageVertex | MTLStageFragment
                         beforeStages:MTLStageBlit
                    visibilityOptions:MTL4VisibilityOptionDevice];
        [copy copyFromTexture:texture
                        sourceSlice:0
                        sourceLevel:0
                       sourceOrigin:MTLOriginMake (0, 0, 0)
                         sourceSize:MTLSizeMake (texture.width,
                                                 texture.height, 1)
                           toBuffer:pending.buffer
                  destinationOffset:0
             destinationBytesPerRow:pending.capture.row_bytes
           destinationBytesPerImage:size];
        [copy endEncoding];
        pending.capture.pixels = { static_cast<const std::byte*> (
                                     pending.buffer.contents),
                                   size };
        pending.done = std::move (m_capture_request);
        m_capture_request = nullptr;
        m_captures.push_back (std::move (pending));
      }

      id<MTLFunction> function (std::string_view source, const char* entry) {
        const std::string key (source);
        __strong id<MTLLibrary>& library = m_libraries[key];
        if (!library) {
          MTLCompileOptions* options = [[MTLCompileOptions alloc] init];
          options.languageVersion = MTLLanguageVersion4_0;
          NSError* error = nil;
          library = [m_device
            newLibraryWithSource:[[NSString alloc]
                                   initWithBytes:key.data ()
                                          length:key.size ()
                                        encoding:NSUTF8StringEncoding]
                         options:options
                           error:&error];
          if (!library)
            throw std::runtime_error ("NHAL: MSL: " + describe (error));
        }
        id<MTLFunction> f = [library newFunctionWithName:@(entry)];
        if (!f)
          throw std::runtime_error (std::string ("NHAL: no MSL function ")
                                    + entry);
        return f;
      }

      void make_samplers () {
        for (NSUInteger i = 0; i < max_samplers; ++i) {
          MTLSamplerDescriptor* d = [[MTLSamplerDescriptor alloc] init];
          d.supportArgumentBuffers = YES;
          const bool nearest = i == 2;
          d.minFilter = nearest ? MTLSamplerMinMagFilterNearest
                                : MTLSamplerMinMagFilterLinear;
          d.magFilter = d.minFilter;
          d.mipFilter = nearest ? MTLSamplerMipFilterNearest
                                : MTLSamplerMipFilterLinear;
          const MTLSamplerAddressMode mode =
            i == 1 ? MTLSamplerAddressModeRepeat
                   : MTLSamplerAddressModeClampToEdge;
          d.sAddressMode = d.tAddressMode = d.rAddressMode = mode;
          if (i == 3)
            d.compareFunction = MTLCompareFunctionLessEqual;
          m_samplers[i] = [m_device newSamplerStateWithDescriptor:d];
          [m_vertex_table setSamplerState:m_samplers[i].gpuResourceID
                                  atIndex:i];
          [m_fragment_table setSamplerState:m_samplers[i].gpuResourceID
                                    atIndex:i];
          [m_compute_table setSamplerState:m_samplers[i].gpuResourceID
                                   atIndex:i];
        }
      }

      void make_resident (id<MTLAllocation> allocation) {
        [m_residency addAllocation:allocation];
        m_residency_dirty = true;
      }

      void collect () {
        const std::uint64_t completed = m_event.signaledValue;
        m_retired_allocations.collect (completed,
                                       [&] (id<MTLAllocation> allocation) {
          [m_residency removeAllocation:allocation];
          m_residency_dirty = true;
        });
        m_retired_objects.collect (completed, [] (id) {});
        resolve_timings (completed);
        for (auto it = m_captures.begin (); it != m_captures.end ();) {
          if (it->serial > completed) {
            ++it;
            continue;
          }
          it->done (it->capture);
          [m_residency removeAllocation:it->buffer];
          m_residency_dirty = true;
          it = m_captures.erase (it);
        }
        if (m_residency_dirty) {
          [m_residency commit];
          m_residency_dirty = false;
        }
      }

      void bind_address (std::uint32_t binding, std::uint64_t address) {
        [m_vertex_table setAddress:address atIndex:binding];
        [m_fragment_table setAddress:address atIndex:binding];
        [m_compute_table setAddress:address atIndex:binding];
      }

      void bind_texture (NSUInteger index, MTLResourceID id) {
        [m_vertex_table setTexture:id atIndex:index];
        [m_fragment_table setTexture:id atIndex:index];
        [m_compute_table setTexture:id atIndex:index];
      }

      // Dispatches in one pass may read what earlier ones wrote.
      void before_dispatch () {
        if (m_dispatched)
          [m_compute barrierAfterEncoderStages:MTLStageDispatch
                           beforeEncoderStages:MTLStageDispatch
                             visibilityOptions:MTL4VisibilityOptionDevice];
        m_dispatched = true;
        [m_compute setArgumentTable:m_compute_table];
      }

      void use_arguments () {
        [m_encoder setArgumentTable:m_vertex_table
                           atStages:MTLRenderStageVertex];
        [m_encoder setArgumentTable:m_fragment_table
                           atStages:MTLRenderStageFragment];
      }

      CAMetalLayer* m_layer = nil;
      Format m_surface_format;
      id<MTLDevice> m_device = nil;
      id<MTL4CommandQueue> m_queue = nil;
      id<MTLSharedEvent> m_event = nil;
      id<MTLResidencySet> m_residency = nil;
      bool m_residency_dirty = false;
      std::array<id<MTL4CommandAllocator>, frames_in_flight> m_allocators;
      std::array<id<MTLBuffer>, frames_in_flight> m_arena_buffers;
      id<MTL4ArgumentTable> m_vertex_table = nil;
      id<MTL4ArgumentTable> m_fragment_table = nil;
      id<MTL4ArgumentTable> m_compute_table = nil;
      std::array<id<MTLSamplerState>, max_samplers> m_samplers;
      std::unordered_map<std::string, id<MTLLibrary>> m_libraries;

      Table<MetalBuffer> m_buffers;
      Table<MetalTexture> m_textures;
      Table<MetalPipeline> m_pipelines;
      Retirement<id<MTLAllocation>> m_retired_allocations;
      Retirement<id> m_retired_objects;

      std::uint64_t m_serial = 0;
      Arena m_arena;
      Texture m_backbuffer;
      id<CAMetalDrawable> m_drawable = nil;
      // A host driving frames from a display link offers each drawable.
      id<CAMetalDrawable> m_offered = nil;
      id<MTL4CommandBuffer> m_commands = nil;
      id<MTL4RenderCommandEncoder> m_encoder = nil;
      id<MTL4ComputeCommandEncoder> m_compute = nil;
      bool m_dispatched = false;
      std::uint32_t m_slot = 0;
      MetalPipeline m_pipeline;
      std::function<void (const Capture&)> m_capture_request;
      std::array<FrameTiming, frames_in_flight> m_timing;
      std::vector<PassTiming> m_pass_timings;
      double m_ticks_per_ms = 1e6;
      std::vector<PendingCapture> m_captures;
    };
  }

  std::unique_ptr<Device> create_metal_device (CAMetalLayer* layer,
                                               Format surface_format) {
    return std::make_unique<MetalDevice> (layer, surface_format);
  }

  void offer_metal_drawable (Device& device, void* drawable) {
    device.offer_drawable (drawable);
  }
}
