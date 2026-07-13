#define NS_PRIVATE_IMPLEMENTATION
#define CA_PRIVATE_IMPLEMENTATION
#define MTL_PRIVATE_IMPLEMENTATION
#include <Foundation/Foundation.hpp>
#include <Metal/Metal.hpp>
#include <QuartzCore/QuartzCore.hpp>

#include "atelier/atelier_renderer.hh"

#include "atelier/atelier_shaders.hh"

#include <array>
#include <chrono>
#include <cstddef>
#include <cstring>
#include <stdexcept>
#include <string>

namespace atelier {
  namespace {
    constexpr std::size_t frames_in_flight = 3;
    constexpr NS::UInteger sample_count = 4;

    std::runtime_error metal_error (const char* operation, NS::Error* error) {
      std::string message = operation;
      if (error && error->localizedDescription ()) {
        message += ": ";
        message += error->localizedDescription ()->utf8String ();
      }
      return std::runtime_error (message);
    }

    struct FrameSlot {
      uint64_t sequence;
      std::size_t buffer_index;
    };

    class MetalExecution {
    public:
      MetalExecution () {
        m_device = NS::TransferPtr (MTL::CreateSystemDefaultDevice ());
        if (!m_device)
          throw std::runtime_error ("Metal is unavailable");

        m_queue = NS::TransferPtr (m_device->newMTL4CommandQueue ());
        if (!m_queue)
          throw std::runtime_error ("Metal 4 is unavailable");
        m_command_buffer = NS::TransferPtr (m_device->newCommandBuffer ());
        for (auto& allocator : m_command_allocators)
          allocator = NS::TransferPtr (m_device->newCommandAllocator ());

        m_completion_event = NS::TransferPtr (m_device->newSharedEvent ());
        m_completion_event->setSignaledValue (0);

        NS::Error* error = nullptr;
        auto descriptor =
          NS::TransferPtr (MTL::ResidencySetDescriptor::alloc ()->init ());
        descriptor->setInitialCapacity (1 + frames_in_flight + 2);
        m_residency_set = NS::TransferPtr (
          m_device->newResidencySet (descriptor.get (), &error));
        if (!m_residency_set)
          throw metal_error ("Could not create Metal 4 residency set", error);
        m_queue->addResidencySet (m_residency_set.get ());
      }

      MTL::Device* device () const {
        return m_device.get ();
      }

      MTL4::CommandBuffer* command_buffer () const {
        return m_command_buffer.get ();
      }

      void use_residency_set (const MTL::ResidencySet* residency_set) {
        m_queue->addResidencySet (residency_set);
      }

      void make_resident (const MTL::Allocation* allocation) {
        m_residency_set->addAllocation (allocation);
      }

      void remove_resident (const MTL::Allocation* allocation) {
        m_residency_set->removeAllocation (allocation);
      }

      void commit_residency () {
        m_residency_set->commit ();
      }

      FrameSlot begin_frame () {
        const uint64_t sequence = ++m_frame_sequence;
        if (sequence > frames_in_flight)
          wait_until_complete (sequence - frames_in_flight);

        const std::size_t buffer_index = sequence % frames_in_flight;
        MTL4::CommandAllocator* allocator =
          m_command_allocators[buffer_index].get ();
        allocator->reset ();
        m_command_buffer->beginCommandBuffer (allocator);
        return { sequence, buffer_index };
      }

      void submit (CA::MetalDrawable* drawable, FrameSlot frame) {
        m_command_buffer->endCommandBuffer ();
        const MTL4::CommandBuffer* submitted = m_command_buffer.get ();
        m_queue->wait (drawable);
        m_queue->commit (&submitted, 1);
        m_queue->signalEvent (m_completion_event.get (), frame.sequence);
        m_queue->signalDrawable (drawable);
        drawable->present ();
      }

      void submit_offscreen (FrameSlot frame) {
        m_command_buffer->endCommandBuffer ();
        const MTL4::CommandBuffer* submitted = m_command_buffer.get ();
        m_queue->commit (&submitted, 1);
        m_queue->signalEvent (m_completion_event.get (), frame.sequence);
      }

      void wait_until_idle () {
        wait_until_complete (m_frame_sequence);
      }

    private:
      void wait_until_complete (uint64_t sequence) {
        if (sequence != 0 &&
            !m_completion_event->waitUntilSignaledValue (sequence, 1000)) {
          throw std::runtime_error ("Timed out waiting for a Metal 4 frame");
        }
      }

      NS::SharedPtr<MTL::Device> m_device;
      NS::SharedPtr<MTL4::CommandQueue> m_queue;
      NS::SharedPtr<MTL4::CommandBuffer> m_command_buffer;
      std::array<NS::SharedPtr<MTL4::CommandAllocator>, frames_in_flight>
        m_command_allocators;
      NS::SharedPtr<MTL::SharedEvent> m_completion_event;
      NS::SharedPtr<MTL::ResidencySet> m_residency_set;
      uint64_t m_frame_sequence = 0;
    };

    class RenderSurface {
    public:
      explicit RenderSurface (MTL::Device* device) : m_device (device) {
        m_layer = NS::RetainPtr (CA::MetalLayer::layer ());
        m_layer->setDevice (device);
        m_layer->setPixelFormat (MTL::PixelFormatBGRA8Unorm_sRGB);
        m_layer->setFramebufferOnly (true);
      }

      void* native_layer () const {
        return m_layer.get ();
      }

      const MTL::ResidencySet* residency_set () const {
        return m_layer->residencySet ();
      }

      bool has_render_target () const {
        return static_cast<bool> (m_depth_texture);
      }

      Viewport viewport () const {
        return m_viewport;
      }

      void resize (MetalExecution& metal, Viewport viewport) {
        if (viewport.width == m_viewport.width &&
            viewport.height == m_viewport.height)
          return;

        discard_render_targets (metal);
        m_viewport = viewport;
        m_layer->setDrawableSize (CGSizeMake (viewport.width, viewport.height));
        if (viewport.is_empty ())
          return;

        m_colour_texture =
          make_render_target (metal, MTL::PixelFormatBGRA8Unorm_sRGB);
        m_depth_texture =
          make_render_target (metal, MTL::PixelFormatDepth32Float);
        metal.commit_residency ();
      }

      CA::MetalDrawable* next_drawable () const {
        return m_layer->nextDrawable ();
      }

      // A CPU-readable stand-in for a drawable, used by capture.
      NS::SharedPtr<MTL::Texture> make_capture_target (MetalExecution& metal) {
        MTL::TextureDescriptor* descriptor =
          MTL::TextureDescriptor::texture2DDescriptor (
            MTL::PixelFormatBGRA8Unorm_sRGB,
            m_viewport.width,
            m_viewport.height,
            false);
        descriptor->setStorageMode (MTL::StorageModeShared);
        descriptor->setUsage (MTL::TextureUsageRenderTarget);
        auto texture = NS::TransferPtr (m_device->newTexture (descriptor));
        if (!texture)
          throw std::runtime_error ("Could not create a capture target");
        metal.make_resident (texture.get ());
        metal.commit_residency ();
        return texture;
      }

      NS::SharedPtr<MTL4::RenderPassDescriptor>
      make_render_pass (MTL::Texture* resolve_target) const {
        auto pass =
          NS::TransferPtr (MTL4::RenderPassDescriptor::alloc ()->init ());
        pass->setRenderTargetWidth (m_viewport.width);
        pass->setRenderTargetHeight (m_viewport.height);

        MTL::RenderPassColorAttachmentDescriptor* colour =
          pass->colorAttachments ()->object (0);
        colour->setTexture (m_colour_texture.get ());
        colour->setResolveTexture (resolve_target);
        colour->setLoadAction (MTL::LoadActionClear);
        colour->setStoreAction (MTL::StoreActionMultisampleResolve);
        colour->setClearColor (MTL::ClearColor (0.025, 0.032, 0.05, 1));

        MTL::RenderPassDepthAttachmentDescriptor* depth =
          pass->depthAttachment ();
        depth->setTexture (m_depth_texture.get ());
        depth->setLoadAction (MTL::LoadActionClear);
        depth->setStoreAction (MTL::StoreActionDontCare);
        depth->setClearDepth (1.0);
        return pass;
      }

    private:
      NS::SharedPtr<MTL::Texture> make_render_target (MetalExecution& metal,
                                                      MTL::PixelFormat format) {
        MTL::TextureDescriptor* descriptor =
          MTL::TextureDescriptor::texture2DDescriptor (
            format, m_viewport.width, m_viewport.height, false);
        descriptor->setTextureType (MTL::TextureType2DMultisample);
        descriptor->setSampleCount (sample_count);
        descriptor->setStorageMode (MTL::StorageModePrivate);
        descriptor->setUsage (MTL::TextureUsageRenderTarget);
        auto texture = NS::TransferPtr (m_device->newTexture (descriptor));
        if (!texture)
          throw std::runtime_error ("Could not create a render target");
        metal.make_resident (texture.get ());
        return texture;
      }

      void discard_render_targets (MetalExecution& metal) {
        if (!m_colour_texture && !m_depth_texture)
          return;
        metal.wait_until_idle ();
        for (auto* texture : { &m_colour_texture, &m_depth_texture }) {
          if (*texture) {
            metal.remove_resident (texture->get ());
            texture->reset ();
          }
        }
        metal.commit_residency ();
      }

      MTL::Device* m_device;
      NS::SharedPtr<CA::MetalLayer> m_layer;
      NS::SharedPtr<MTL::Texture> m_colour_texture;
      NS::SharedPtr<MTL::Texture> m_depth_texture;
      Viewport m_viewport;
    };

    class ScenePipeline {
    public:
      explicit ScenePipeline (MetalExecution& metal) {
        MTL::Device* device = metal.device ();
        const auto library = compile_shaders (device);
        m_pipeline = make_pipeline (device, library.get ());
        m_depth_state = make_depth_state (device);
        m_argument_table = make_argument_table (device);

        const CoinMesh mesh = coin_wireframe ();
        m_vertices = NS::TransferPtr (device->newBuffer (
          mesh.data (), sizeof (mesh), MTL::ResourceStorageModeShared));
        if (!m_vertices)
          throw std::runtime_error ("Could not create vertex buffer");
        metal.make_resident (m_vertices.get ());

        for (auto& frame : m_frame_buffers) {
          frame = NS::TransferPtr (
            device->newBuffer (sizeof (Frame), MTL::ResourceStorageModeShared));
          if (!frame)
            throw std::runtime_error ("Could not create frame buffer");
          metal.make_resident (frame.get ());
        }
        metal.commit_residency ();
      }

      void prepare (FrameSlot slot, const Frame& frame) {
        MTL::Buffer* buffer = m_frame_buffers[slot.buffer_index].get ();
        std::memcpy (buffer->contents (), &frame, sizeof (frame));
        m_argument_table->setAddress (m_vertices->gpuAddress (), 0);
        m_argument_table->setAddress (buffer->gpuAddress (), 1);
        m_argument_table->setAddress (
          buffer->gpuAddress () + offsetof (Frame, coins), 2);
      }

      void encode (MTL4::CommandBuffer* command_buffer,
                   MTL4::RenderPassDescriptor* pass) const {
        MTL4::RenderCommandEncoder* encoder =
          command_buffer->renderCommandEncoder (pass);
        encoder->setRenderPipelineState (m_pipeline.get ());
        encoder->setDepthStencilState (m_depth_state.get ());
        encoder->setArgumentTable (m_argument_table.get (),
                                   MTL::RenderStageVertex);
        encoder->drawPrimitives (MTL::PrimitiveTypeTriangle,
                                 0,
                                 std::tuple_size_v<CoinMesh>,
                                 coin_count);
        encoder->endEncoding ();
      }

    private:
      static NS::SharedPtr<MTL::Library> compile_shaders (MTL::Device* device) {
        NS::Error* error = nullptr;
        const std::string source (metal_shader_source);
        const auto source_string =
          NS::String::string (source.c_str (), NS::UTF8StringEncoding);
        auto library =
          NS::TransferPtr (device->newLibrary (source_string, nullptr, &error));
        if (!library)
          throw metal_error ("Could not compile atelier shaders", error);
        return library;
      }

      static NS::SharedPtr<MTL::RenderPipelineState>
      make_pipeline (MTL::Device* device, MTL::Library* library) {
        auto descriptor =
          NS::TransferPtr (MTL::RenderPipelineDescriptor::alloc ()->init ());
        auto vertex =
          NS::TransferPtr (library->newFunction (MTLSTR ("atelier_vertex")));
        auto fragment =
          NS::TransferPtr (library->newFunction (MTLSTR ("atelier_fragment")));
        descriptor->setVertexFunction (vertex.get ());
        descriptor->setFragmentFunction (fragment.get ());
        descriptor->setRasterSampleCount (sample_count);
        descriptor->colorAttachments ()->object (0)->setPixelFormat (
          MTL::PixelFormatBGRA8Unorm_sRGB);
        descriptor->setDepthAttachmentPixelFormat (
          MTL::PixelFormatDepth32Float);

        NS::Error* error = nullptr;
        auto pipeline = NS::TransferPtr (
          device->newRenderPipelineState (descriptor.get (), &error));
        if (!pipeline)
          throw metal_error ("Could not create atelier pipeline", error);
        return pipeline;
      }

      static NS::SharedPtr<MTL::DepthStencilState>
      make_depth_state (MTL::Device* device) {
        auto descriptor =
          NS::TransferPtr (MTL::DepthStencilDescriptor::alloc ()->init ());
        descriptor->setDepthCompareFunction (MTL::CompareFunctionLess);
        descriptor->setDepthWriteEnabled (true);
        auto state =
          NS::TransferPtr (device->newDepthStencilState (descriptor.get ()));
        if (!state)
          throw std::runtime_error ("Could not create depth state");
        return state;
      }

      static NS::SharedPtr<MTL4::ArgumentTable>
      make_argument_table (MTL::Device* device) {
        auto descriptor =
          NS::TransferPtr (MTL4::ArgumentTableDescriptor::alloc ()->init ());
        descriptor->setMaxBufferBindCount (3);
        NS::Error* error = nullptr;
        auto table = NS::TransferPtr (
          device->newArgumentTable (descriptor.get (), &error));
        if (!table)
          throw metal_error ("Could not create Metal 4 argument table", error);
        return table;
      }

      NS::SharedPtr<MTL::RenderPipelineState> m_pipeline;
      NS::SharedPtr<MTL::DepthStencilState> m_depth_state;
      NS::SharedPtr<MTL4::ArgumentTable> m_argument_table;
      NS::SharedPtr<MTL::Buffer> m_vertices;
      std::array<NS::SharedPtr<MTL::Buffer>, frames_in_flight> m_frame_buffers;
    };
  }

  class Renderer::Impl {
  public:
    Impl ()
        : m_surface (m_metal.device ()), m_scene (m_metal),
          m_started_at (Clock::now ()) {
      m_metal.use_residency_set (m_surface.residency_set ());
    }

    void* native_layer () const {
      return m_surface.native_layer ();
    }

    void resize (Viewport viewport) {
      m_surface.resize (m_metal, viewport);
    }

    void draw () {
      if (!m_surface.has_render_target ())
        return;
      CA::MetalDrawable* drawable = m_surface.next_drawable ();
      if (!drawable)
        return;

      const FrameSlot slot = m_metal.begin_frame ();
      const Frame frame =
        compose_frame (Clock::now () - m_started_at, m_surface.viewport ());
      m_scene.prepare (slot, frame);

      const auto pass = m_surface.make_render_pass (drawable->texture ());
      m_scene.encode (m_metal.command_buffer (), pass.get ());
      m_metal.submit (drawable, slot);
    }

    Image capture (Duration elapsed) {
      const Viewport viewport = m_surface.viewport ();
      if (!m_surface.has_render_target ())
        throw std::runtime_error ("Cannot capture an unsized surface");

      const auto target = m_surface.make_capture_target (m_metal);
      const FrameSlot slot = m_metal.begin_frame ();
      m_scene.prepare (slot, compose_frame (elapsed, viewport));
      const auto pass = m_surface.make_render_pass (target.get ());
      m_scene.encode (m_metal.command_buffer (), pass.get ());
      m_metal.submit_offscreen (slot);
      m_metal.wait_until_idle ();

      Image image {
        .width = viewport.width,
        .height = viewport.height,
        .bgra = std::make_unique<std::uint8_t[]> (4 * viewport.width *
                                                  viewport.height),
      };
      target->getBytes (image.bgra.get (),
                        4 * viewport.width,
                        MTL::Region (0, 0, viewport.width, viewport.height),
                        0);
      m_metal.remove_resident (target.get ());
      m_metal.commit_residency ();
      return image;
    }

  private:
    using Clock = std::chrono::steady_clock;

    MetalExecution m_metal;
    RenderSurface m_surface;
    ScenePipeline m_scene;
    Clock::time_point m_started_at;
  };

  Renderer::Renderer () : m_impl (std::make_unique<Impl> ()) {}

  Renderer::~Renderer () = default;

  void* Renderer::native_layer () const {
    return m_impl->native_layer ();
  }

  void Renderer::resize (Viewport viewport) {
    m_impl->resize (viewport);
  }

  void Renderer::draw () {
    m_impl->draw ();
  }

  Image Renderer::capture (Duration elapsed) {
    return m_impl->capture (elapsed);
  }
}
