#define NS_PRIVATE_IMPLEMENTATION
#define CA_PRIVATE_IMPLEMENTATION
#define MTL_PRIVATE_IMPLEMENTATION
#include <Foundation/Foundation.hpp>
#include <Metal/Metal.hpp>
#include <QuartzCore/QuartzCore.hpp>

#include "atelier/atelier_renderer.hh"

#include "atelier/atelier.hh"

#include <array>
#include <chrono>
#include <cstring>
#include <stdexcept>
#include <string>

namespace atelier {
  namespace {
    std::runtime_error metal_error (const char* operation, NS::Error* error) {
      std::string message = operation;
      if (error && error->localizedDescription ()) {
        message += ": ";
        message += error->localizedDescription ()->utf8String ();
      }
      return std::runtime_error (message);
    }
  }

  class Renderer::Impl {
  public:
    static constexpr std::size_t frames_in_flight = 3;

    Impl () {
      m_device = NS::TransferPtr (MTL::CreateSystemDefaultDevice ());
      if (!m_device)
        throw std::runtime_error ("Metal is unavailable");

      m_layer = NS::RetainPtr (CA::MetalLayer::layer ());
      m_layer->setDevice (m_device.get ());
      m_layer->setPixelFormat (MTL::PixelFormatBGRA8Unorm_sRGB);
      m_layer->setFramebufferOnly (true);

      m_queue = NS::TransferPtr (m_device->newMTL4CommandQueue ());
      if (!m_queue)
        throw std::runtime_error ("Metal 4 is unavailable");
      m_command_buffer = NS::TransferPtr (m_device->newCommandBuffer ());
      for (auto& allocator : m_command_allocators)
        allocator = NS::TransferPtr (m_device->newCommandAllocator ());
      m_shared_event = NS::TransferPtr (m_device->newSharedEvent ());
      m_shared_event->setSignaledValue (0);

      NS::Error* error = nullptr;
      const std::string source (shader_source ());
      const auto source_string =
        NS::String::string (source.c_str (), NS::UTF8StringEncoding);
      auto library =
        NS::TransferPtr (m_device->newLibrary (source_string, nullptr, &error));
      if (!library)
        throw metal_error ("Could not compile atelier shaders", error);

      auto descriptor =
        NS::TransferPtr (MTL::RenderPipelineDescriptor::alloc ()->init ());
      auto vertex =
        NS::TransferPtr (library->newFunction (MTLSTR ("atelier_vertex")));
      auto fragment =
        NS::TransferPtr (library->newFunction (MTLSTR ("atelier_fragment")));
      descriptor->setVertexFunction (vertex.get ());
      descriptor->setFragmentFunction (fragment.get ());
      descriptor->colorAttachments ()->object (0)->setPixelFormat (
        MTL::PixelFormatBGRA8Unorm_sRGB);
      descriptor->setDepthAttachmentPixelFormat (MTL::PixelFormatDepth32Float);
      m_pipeline = NS::TransferPtr (
        m_device->newRenderPipelineState (descriptor.get (), &error));
      if (!m_pipeline)
        throw metal_error ("Could not create atelier pipeline", error);

      auto depth =
        NS::TransferPtr (MTL::DepthStencilDescriptor::alloc ()->init ());
      depth->setDepthCompareFunction (MTL::CompareFunctionLess);
      depth->setDepthWriteEnabled (true);
      m_depth_state =
        NS::TransferPtr (m_device->newDepthStencilState (depth.get ()));

      const std::vector<GpuVector> vertices = coin_wire_mesh ();
      m_vertex_count = vertices.size ();
      m_vertices = NS::TransferPtr (
        m_device->newBuffer (vertices.data (),
                             vertices.size () * sizeof (vertices[0]),
                             MTL::ResourceStorageModeShared));
      for (auto& uniforms : m_uniform_buffers) {
        uniforms = NS::TransferPtr (m_device->newBuffer (
          sizeof (Uniforms), MTL::ResourceStorageModeShared));
      }

      auto argument_descriptor =
        NS::TransferPtr (MTL4::ArgumentTableDescriptor::alloc ()->init ());
      argument_descriptor->setMaxBufferBindCount (2);
      m_argument_table = NS::TransferPtr (
        m_device->newArgumentTable (argument_descriptor.get (), &error));
      if (!m_argument_table)
        throw metal_error ("Could not create Metal 4 argument table", error);

      auto residency_descriptor =
        NS::TransferPtr (MTL::ResidencySetDescriptor::alloc ()->init ());
      residency_descriptor->setInitialCapacity (1 + frames_in_flight + 1);
      m_residency_set = NS::TransferPtr (
        m_device->newResidencySet (residency_descriptor.get (), &error));
      if (!m_residency_set)
        throw metal_error ("Could not create Metal 4 residency set", error);
      m_residency_set->addAllocation (m_vertices.get ());
      for (const auto& uniforms : m_uniform_buffers)
        m_residency_set->addAllocation (uniforms.get ());
      m_residency_set->commit ();
      m_queue->addResidencySet (m_residency_set.get ());
      m_queue->addResidencySet (m_layer->residencySet ());
      m_started_at = Clock::now ();
    }

    void* native_layer () const {
      return m_layer.get ();
    }

    void resize (double width, double height) {
      const auto pixel_width = static_cast<NS::UInteger> (width);
      const auto pixel_height = static_cast<NS::UInteger> (height);
      if (pixel_width == 0 || pixel_height == 0 ||
          (pixel_width == m_width && pixel_height == m_height))
        return;

      if (m_depth_texture) {
        wait_for_frame (m_frame_number);
        m_residency_set->removeAllocation (m_depth_texture.get ());
      }
      m_width = pixel_width;
      m_height = pixel_height;
      m_layer->setDrawableSize (CGSizeMake (width, height));
      MTL::TextureDescriptor* descriptor =
        MTL::TextureDescriptor::texture2DDescriptor (
          MTL::PixelFormatDepth32Float, m_width, m_height, false);
      descriptor->setStorageMode (MTL::StorageModePrivate);
      descriptor->setUsage (MTL::TextureUsageRenderTarget);
      m_depth_texture = NS::TransferPtr (m_device->newTexture (descriptor));
      m_residency_set->addAllocation (m_depth_texture.get ());
      m_residency_set->commit ();
    }

    void draw () {
      if (!m_depth_texture)
        return;
      CA::MetalDrawable* drawable = m_layer->nextDrawable ();
      if (!drawable)
        return;

      ++m_frame_number;
      const std::size_t frame_index = m_frame_number % frames_in_flight;
      if (m_frame_number > frames_in_flight)
        wait_for_frame (m_frame_number - frames_in_flight);
      MTL4::CommandAllocator* allocator =
        m_command_allocators[frame_index].get ();
      allocator->reset ();
      m_command_buffer->beginCommandBuffer (allocator);

      auto pass =
        NS::TransferPtr (MTL4::RenderPassDescriptor::alloc ()->init ());
      pass->setRenderTargetWidth (m_width);
      pass->setRenderTargetHeight (m_height);
      MTL::RenderPassColorAttachmentDescriptor* color =
        pass->colorAttachments ()->object (0);
      color->setTexture (drawable->texture ());
      color->setLoadAction (MTL::LoadActionClear);
      color->setStoreAction (MTL::StoreActionStore);
      color->setClearColor (MTL::ClearColor (0.025, 0.032, 0.05, 1));
      MTL::RenderPassDepthAttachmentDescriptor* depth =
        pass->depthAttachment ();
      depth->setTexture (m_depth_texture.get ());
      depth->setLoadAction (MTL::LoadActionClear);
      depth->setStoreAction (MTL::StoreActionDontCare);
      depth->setClearDepth (1.0);

      const std::chrono::duration<float> elapsed = Clock::now () - m_started_at;
      const Frame current_frame =
        frame (elapsed.count (), static_cast<float> (m_width) / m_height);
      MTL::Buffer* uniforms = m_uniform_buffers[frame_index].get ();
      std::memcpy (uniforms->contents (),
                   &current_frame.uniforms,
                   sizeof (current_frame.uniforms));
      m_argument_table->setAddress (m_vertices->gpuAddress (), 0);
      m_argument_table->setAddress (uniforms->gpuAddress (), 1);

      MTL4::RenderCommandEncoder* encoder =
        m_command_buffer->renderCommandEncoder (pass.get ());
      encoder->setRenderPipelineState (m_pipeline.get ());
      encoder->setDepthStencilState (m_depth_state.get ());
      encoder->setArgumentTable (m_argument_table.get (),
                                 MTL::RenderStageVertex);
      encoder->drawPrimitives (MTL::PrimitiveTypeTriangle,
                               static_cast<NS::UInteger> (0),
                               m_vertex_count);
      encoder->endEncoding ();
      m_command_buffer->endCommandBuffer ();

      const MTL4::CommandBuffer* submitted = m_command_buffer.get ();
      m_queue->wait (drawable);
      m_queue->commit (&submitted, 1);
      m_queue->signalEvent (m_shared_event.get (), m_frame_number);
      m_queue->signalDrawable (drawable);
      drawable->present ();
    }

  private:
    using Clock = std::chrono::steady_clock;

    void wait_for_frame (uint64_t frame_number) {
      if (frame_number != 0 &&
          !m_shared_event->waitUntilSignaledValue (frame_number, 1000)) {
        throw std::runtime_error ("Timed out waiting for a Metal 4 frame");
      }
    }

    NS::SharedPtr<MTL::Device> m_device;
    NS::SharedPtr<CA::MetalLayer> m_layer;
    NS::SharedPtr<MTL4::CommandQueue> m_queue;
    NS::SharedPtr<MTL4::CommandBuffer> m_command_buffer;
    std::array<NS::SharedPtr<MTL4::CommandAllocator>, frames_in_flight>
      m_command_allocators;
    NS::SharedPtr<MTL4::ArgumentTable> m_argument_table;
    NS::SharedPtr<MTL::ResidencySet> m_residency_set;
    NS::SharedPtr<MTL::SharedEvent> m_shared_event;
    NS::SharedPtr<MTL::RenderPipelineState> m_pipeline;
    NS::SharedPtr<MTL::DepthStencilState> m_depth_state;
    NS::SharedPtr<MTL::Buffer> m_vertices;
    std::array<NS::SharedPtr<MTL::Buffer>, frames_in_flight> m_uniform_buffers;
    NS::SharedPtr<MTL::Texture> m_depth_texture;
    NS::UInteger m_vertex_count = 0;
    NS::UInteger m_width = 0;
    NS::UInteger m_height = 0;
    uint64_t m_frame_number = 0;
    Clock::time_point m_started_at;
  };

  Renderer::Renderer () : m_impl (std::make_unique<Impl> ()) {}

  Renderer::~Renderer () = default;

  void* Renderer::native_layer () const {
    return m_impl->native_layer ();
  }

  void Renderer::resize (double width, double height) {
    m_impl->resize (width, height);
  }

  void Renderer::draw () {
    m_impl->draw ();
  }
}
