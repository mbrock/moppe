#define NS_PRIVATE_IMPLEMENTATION
#define CA_PRIVATE_IMPLEMENTATION
#define MTL_PRIVATE_IMPLEMENTATION
#include <Foundation/Foundation.hpp>
#include <Metal/Metal.hpp>
#include <QuartzCore/QuartzCore.hpp>

#include "atelier/atelier_renderer.hh"

#include "atelier/atelier.hh"

#include <chrono>
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
    Impl () {
      m_device = NS::TransferPtr (MTL::CreateSystemDefaultDevice ());
      if (!m_device)
        throw std::runtime_error ("Metal is unavailable");

      m_layer = NS::RetainPtr (CA::MetalLayer::layer ());
      m_layer->setDevice (m_device.get ());
      m_layer->setPixelFormat (MTL::PixelFormatBGRA8Unorm_sRGB);
      m_layer->setFramebufferOnly (true);
      m_queue = NS::TransferPtr (m_device->newCommandQueue ());

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

      m_width = pixel_width;
      m_height = pixel_height;
      m_layer->setDrawableSize (CGSizeMake (width, height));
      MTL::TextureDescriptor* descriptor =
        MTL::TextureDescriptor::texture2DDescriptor (
          MTL::PixelFormatDepth32Float, m_width, m_height, false);
      descriptor->setStorageMode (MTL::StorageModePrivate);
      descriptor->setUsage (MTL::TextureUsageRenderTarget);
      m_depth_texture = NS::TransferPtr (m_device->newTexture (descriptor));
    }

    void draw () {
      if (!m_depth_texture)
        return;
      CA::MetalDrawable* drawable = m_layer->nextDrawable ();
      if (!drawable)
        return;

      MTL::RenderPassDescriptor* pass =
        MTL::RenderPassDescriptor::renderPassDescriptor ();
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
      MTL::CommandBuffer* command = m_queue->commandBuffer ();
      MTL::RenderCommandEncoder* encoder = command->renderCommandEncoder (pass);
      encoder->setRenderPipelineState (m_pipeline.get ());
      encoder->setDepthStencilState (m_depth_state.get ());
      encoder->setVertexBuffer (m_vertices.get (), 0, 0);
      encoder->setVertexBytes (
        &current_frame.uniforms, sizeof (current_frame.uniforms), 1);
      encoder->drawPrimitives (MTL::PrimitiveTypeTriangle,
                               static_cast<NS::UInteger> (0),
                               m_vertex_count);
      encoder->endEncoding ();
      command->presentDrawable (drawable);
      command->commit ();
    }

  private:
    using Clock = std::chrono::steady_clock;

    NS::SharedPtr<MTL::Device> m_device;
    NS::SharedPtr<CA::MetalLayer> m_layer;
    NS::SharedPtr<MTL::CommandQueue> m_queue;
    NS::SharedPtr<MTL::RenderPipelineState> m_pipeline;
    NS::SharedPtr<MTL::DepthStencilState> m_depth_state;
    NS::SharedPtr<MTL::Buffer> m_vertices;
    NS::SharedPtr<MTL::Texture> m_depth_texture;
    NS::UInteger m_vertex_count = 0;
    NS::UInteger m_width = 0;
    NS::UInteger m_height = 0;
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
