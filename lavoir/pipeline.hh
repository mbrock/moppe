#pragma once

#include "lavoir/lending.hh"
#include "lavoir/metal.hh"
#include "lavoir/shaders.hh"
#include "lavoir/surface.hh"
#include "lavoir/units.hh"

#include <cstring>

/// The water pipeline: one vertex-pulling render pipeline whose
/// bindings live in a Metal 4 argument table. Slot zero holds the
/// frame's uniforms from a small per-frame ring; slot one holds the
/// tenancy of whichever wave level is current -- the argument table is
/// the published register of exactly what the pass may touch.

namespace lavoir {
  class water_pipeline {
  public:
    explicit water_pipeline (gpu& metal) {
      MTL::Device* device = metal.device ();

      NS::Error* error = nullptr;
      const auto source =
        NS::String::string (water_shader_source, NS::UTF8StringEncoding);
      auto library =
        NS::TransferPtr (device->newLibrary (source, nullptr, &error));
      if (!library)
        throw metal_error ("Could not compile the water shaders", error);

      auto vertex = NS::TransferPtr (library->newFunction (
        NS::String::string ("water_vertex", NS::UTF8StringEncoding)));
      auto fragment = NS::TransferPtr (library->newFunction (
        NS::String::string ("water_fragment", NS::UTF8StringEncoding)));

      auto descriptor =
        NS::TransferPtr (MTL::RenderPipelineDescriptor::alloc ()->init ());
      descriptor->setLabel (
        NS::String::string ("water", NS::UTF8StringEncoding));
      descriptor->setVertexFunction (vertex.get ());
      descriptor->setFragmentFunction (fragment.get ());
      descriptor->setRasterSampleCount (sample_count);
      descriptor->colorAttachments ()->object (0)->setPixelFormat (
        MTL::PixelFormatBGRA8Unorm_sRGB);
      descriptor->setDepthAttachmentPixelFormat (MTL::PixelFormatDepth32Float);

      m_pipeline = NS::TransferPtr (device->newRenderPipelineState (
        descriptor.get (), MTL::PipelineOptionNone, nullptr, &error));
      if (!m_pipeline)
        throw metal_error ("Could not create the water pipeline", error);

      auto table_descriptor =
        NS::TransferPtr (MTL4::ArgumentTableDescriptor::alloc ()->init ());
      table_descriptor->setMaxBufferBindCount (2);
      m_arguments = NS::TransferPtr (
        device->newArgumentTable (table_descriptor.get (), &error));
      if (!m_arguments)
        throw metal_error ("Could not create the water argument table", error);

      for (auto& uniforms : m_uniform_ring) {
        uniforms = NS::TransferPtr (device->newBuffer (
          sizeof (water_uniforms), MTL::ResourceStorageModeShared));
        if (!uniforms)
          throw std::runtime_error ("Could not create a uniform buffer");
        metal.make_resident (uniforms.get ());
      }
      metal.commit_residency ();
    }

    /// Encode one report of the water: uniforms posted to this frame's
    /// slot, the current level's tenancy bound beside them, and one
    /// triangle strip pulled from the lent column.
    void encode (gpu& metal,
                 MTL4::RenderPassDescriptor* pass,
                 frame_slot slot,
                 const tenancy& heights,
                 water_uniforms uniforms) {
      MTL::Buffer* frame_uniforms =
        m_uniform_ring[slot.buffer_index.numerical_value_in (frame)].get ();
      std::memcpy (frame_uniforms->contents (), &uniforms, sizeof (uniforms));

      m_arguments->setAddress (frame_uniforms->gpuAddress (), 0);
      m_arguments->setAddress (heights.gpu_address (), 1);

      MTL4::RenderCommandEncoder* encoder =
        metal.command_buffer ()->renderCommandEncoder (pass);
      encoder->setRenderPipelineState (m_pipeline.get ());
      encoder->setArgumentTable (
        m_arguments.get (),
        MTL::RenderStages (MTL::RenderStageVertex | MTL::RenderStageFragment));
      encoder->drawPrimitives (MTL::PrimitiveTypeTriangleStrip,
                               NS::UInteger (0),
                               NS::UInteger (2 * uniforms.site_count));
      encoder->endEncoding ();
    }

  private:
    NS::SharedPtr<MTL::RenderPipelineState> m_pipeline;
    NS::SharedPtr<MTL4::ArgumentTable> m_arguments;
    std::array<NS::SharedPtr<MTL::Buffer>, allocator_ring_size> m_uniform_ring;
  };
}
