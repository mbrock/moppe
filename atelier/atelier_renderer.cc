#define NS_PRIVATE_IMPLEMENTATION
#define CA_PRIVATE_IMPLEMENTATION
#define MTL_PRIVATE_IMPLEMENTATION
#include <Foundation/Foundation.hpp>
#include <Metal/Metal.hpp>
#include <QuartzCore/QuartzCore.hpp>

#include "atelier/atelier_renderer.hh"

#include "atelier/atelier.hh"

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
    explicit Impl (MTL::Device* device) {
      m_queue = NS::TransferPtr (device->newCommandQueue ());

      NS::Error* error = nullptr;
      const std::string source (shader_source ());
      const auto source_string =
        NS::String::string (source.c_str (), NS::UTF8StringEncoding);
      auto library =
        NS::TransferPtr (device->newLibrary (source_string, nullptr, &error));
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
        device->newRenderPipelineState (descriptor.get (), &error));
      if (!m_pipeline)
        throw metal_error ("Could not create atelier pipeline", error);

      auto depth =
        NS::TransferPtr (MTL::DepthStencilDescriptor::alloc ()->init ());
      depth->setDepthCompareFunction (MTL::CompareFunctionLess);
      depth->setDepthWriteEnabled (true);
      m_depth_state =
        NS::TransferPtr (device->newDepthStencilState (depth.get ()));

      const std::vector<GpuVector> vertices = coin_wire_mesh ();
      m_vertex_count = vertices.size ();
      m_vertices = NS::TransferPtr (
        device->newBuffer (vertices.data (),
                           vertices.size () * sizeof (vertices[0]),
                           MTL::ResourceStorageModeShared));
    }

    void draw (MTL::RenderPassDescriptor* pass,
               CA::MetalDrawable* drawable,
               float elapsed_seconds,
               float aspect) {
      const Frame current_frame = frame (elapsed_seconds, aspect);
      pass->colorAttachments ()->object (0)->setClearColor (
        MTL::ClearColor (0.025, 0.032, 0.05, 1));
      pass->depthAttachment ()->setClearDepth (1.0);

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
    NS::SharedPtr<MTL::CommandQueue> m_queue;
    NS::SharedPtr<MTL::RenderPipelineState> m_pipeline;
    NS::SharedPtr<MTL::DepthStencilState> m_depth_state;
    NS::SharedPtr<MTL::Buffer> m_vertices;
    NS::UInteger m_vertex_count = 0;
  };

  Renderer::Renderer (MTL::Device* device)
      : m_impl (std::make_unique<Impl> (device)) {}

  Renderer::~Renderer () = default;

  void Renderer::draw (MTL::RenderPassDescriptor* pass,
                       CA::MetalDrawable* drawable,
                       float elapsed_seconds,
                       float aspect) {
    m_impl->draw (pass, drawable, elapsed_seconds, aspect);
  }
}
