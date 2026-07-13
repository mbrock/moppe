#pragma once

#include "atelier/atelier.hh"
#include "atelier/metal.hh"

// RenderSurface: the window-facing CAMetalLayer plus the multisampled
// colour and depth targets the scene is drawn into.  Each frame
// renders at `sample_count` samples and resolves into either the
// layer's next drawable or, for headless capture, a plain shared
// texture the CPU can read back.

namespace atelier {
  inline constexpr NS::UInteger sample_count = 4;

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
}
