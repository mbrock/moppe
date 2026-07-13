#pragma once

#include "atelier/atelier.hh"
#include "atelier/atelier_shaders.hh"
#include "atelier/metal.hh"
#include "atelier/surface.hh"

#include <array>
#include <cstddef>
#include <cstring>

// TilePipeline: one mesh threadgroup per leaf of the current partition.  The
// GPU produces each beveled hexagonal prism procedurally, so refinement and
// melding only change the compact per-tile instance buffer.

namespace atelier {
  inline constexpr std::size_t max_tile_instances = 8192;

  struct GpuFrame {
    Uniforms uniforms;
    std::array<TileInstance, max_tile_instances> tiles;
  };

  class TilePipeline {
  public:
    explicit TilePipeline (MetalExecution& metal) {
      MTL::Device* device = metal.device ();
      const auto library = compile_shaders (device);
      m_pipeline = make_pipeline (device, library.get ());
      m_depth_state = make_depth_state (device);
      m_arguments = make_argument_table (device);

      for (auto& frame : m_frames) {
        frame = NS::TransferPtr (device->newBuffer (
          sizeof (GpuFrame), MTL::ResourceStorageModeShared));
        if (!frame)
          throw std::runtime_error ("Could not create a frame buffer");
        metal.make_resident (frame.get ());
      }
      metal.commit_residency ();
    }

    void prepare (FrameSlot slot, const Frame& frame) {
      if (frame.tiles.size () > max_tile_instances)
        throw std::runtime_error ("The cellular landscape exceeded its GPU "
                                  "instance capacity");
      MTL::Buffer* buffer = m_frames[slot.buffer_index].get ();
      auto* gpu = static_cast<GpuFrame*> (buffer->contents ());
      gpu->uniforms = frame.uniforms;
      std::memcpy (gpu->tiles.data (),
                   frame.tiles.data (),
                   frame.tiles.size () * sizeof (TileInstance));
      m_arguments->setAddress (
        buffer->gpuAddress () + offsetof (GpuFrame, uniforms), 1);
      m_arguments->setAddress (
        buffer->gpuAddress () + offsetof (GpuFrame, tiles), 2);
      m_instance_count = frame.tiles.size ();
    }

    void encode (MTL4::CommandBuffer* command_buffer,
                 MTL4::RenderPassDescriptor* pass) const {
      MTL4::RenderCommandEncoder* encoder =
        command_buffer->renderCommandEncoder (pass);
      encoder->setRenderPipelineState (m_pipeline.get ());
      encoder->setDepthStencilState (m_depth_state.get ());
      encoder->setArgumentTable (m_arguments.get (), MTL::RenderStageMesh);
      encoder->drawMeshThreadgroups (MTL::Size (m_instance_count, 1, 1),
                                     MTL::Size (1, 1, 1),
                                     MTL::Size (32, 1, 1));
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
        NS::TransferPtr (MTL::MeshRenderPipelineDescriptor::alloc ()->init ());
      auto mesh = NS::TransferPtr (library->newFunction (MTLSTR ("puck_mesh")));
      auto fragment =
        NS::TransferPtr (library->newFunction (MTLSTR ("puck_fragment")));
      descriptor->setMeshFunction (mesh.get ());
      descriptor->setFragmentFunction (fragment.get ());
      descriptor->setMaxTotalThreadsPerMeshThreadgroup (32);
      descriptor->setRasterSampleCount (sample_count);
      descriptor->colorAttachments ()->object (0)->setPixelFormat (
        MTL::PixelFormatBGRA8Unorm_sRGB);
      descriptor->setDepthAttachmentPixelFormat (MTL::PixelFormatDepth32Float);

      NS::Error* error = nullptr;
      auto pipeline = NS::TransferPtr (device->newRenderPipelineState (
        descriptor.get (), MTL::PipelineOptionNone, nullptr, &error));
      if (!pipeline)
        throw metal_error ("Could not create atelier mesh pipeline", error);
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
      auto table =
        NS::TransferPtr (device->newArgumentTable (descriptor.get (), &error));
      if (!table)
        throw metal_error ("Could not create Metal 4 argument table", error);
      return table;
    }

    NS::SharedPtr<MTL::RenderPipelineState> m_pipeline;
    NS::SharedPtr<MTL::DepthStencilState> m_depth_state;
    NS::SharedPtr<MTL4::ArgumentTable> m_arguments;
    std::array<NS::SharedPtr<MTL::Buffer>, frames_in_flight> m_frames;
    std::size_t m_instance_count = 0;
  };
}
