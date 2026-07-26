#pragma once

#include "lavoir/metal.hh"

#include <cstddef>
#include <stdexcept>

/// The window-facing CAMetalLayer and the multisampled colour and
/// depth targets a frame is drawn into. Each frame renders at
/// `sample_count` samples and resolves into the layer's next drawable
/// or, for headless capture, into a shared texture the CPU reads back.
/// The clear colour is the workshop's ground: freshly washed linen.

namespace lavoir {
  inline constexpr NS::UInteger sample_count = 4;

  struct viewport {
    pixels_t width = pixels_t::zero ();
    pixels_t height = pixels_t::zero ();

    bool is_empty () const {
      return width == pixels_t::zero () || height == pixels_t::zero ();
    }

    /// Two pixel counts cancel to a plain ratio.
    quantity<one, double> aspect_ratio () const {
      if (is_empty ())
        throw std::invalid_argument ("An empty viewport has no aspect ratio");
      return value_cast<double> (width) / value_cast<double> (height);
    }
  };

  inline const MTL::ClearColor linen { 0.957, 0.945, 0.921, 1.0 };

  class surface {
  public:
    explicit surface (MTL::Device* device) : m_device (device) {
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

    viewport extent () const {
      return m_viewport;
    }

    void resize (gpu& metal, viewport extent) {
      if (extent.width == m_viewport.width &&
          extent.height == m_viewport.height)
        return;

      discard_render_targets (metal);
      m_viewport = extent;
      m_layer->setDrawableSize (
        CGSizeMake (extent.width.numerical_value_in (pixel),
                    extent.height.numerical_value_in (pixel)));
      if (extent.is_empty ())
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

    /// A CPU-readable stand-in for a drawable, used by capture.
    NS::SharedPtr<MTL::Texture> make_capture_target (gpu& metal) {
      MTL::TextureDescriptor* descriptor =
        MTL::TextureDescriptor::texture2DDescriptor (
          MTL::PixelFormatBGRA8Unorm_sRGB,
          m_viewport.width.numerical_value_in (pixel),
          m_viewport.height.numerical_value_in (pixel),
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
      pass->setRenderTargetWidth (m_viewport.width.numerical_value_in (pixel));
      pass->setRenderTargetHeight (
        m_viewport.height.numerical_value_in (pixel));

      MTL::RenderPassColorAttachmentDescriptor* colour =
        pass->colorAttachments ()->object (0);
      colour->setTexture (m_colour_texture.get ());
      colour->setResolveTexture (resolve_target);
      colour->setLoadAction (MTL::LoadActionClear);
      colour->setStoreAction (MTL::StoreActionMultisampleResolve);
      colour->setClearColor (linen);

      MTL::RenderPassDepthAttachmentDescriptor* depth =
        pass->depthAttachment ();
      depth->setTexture (m_depth_texture.get ());
      depth->setLoadAction (MTL::LoadActionClear);
      depth->setStoreAction (MTL::StoreActionDontCare);
      depth->setClearDepth (1.0);
      return pass;
    }

  private:
    NS::SharedPtr<MTL::Texture> make_render_target (gpu& metal,
                                                    MTL::PixelFormat format) {
      MTL::TextureDescriptor* descriptor =
        MTL::TextureDescriptor::texture2DDescriptor (
          format,
          m_viewport.width.numerical_value_in (pixel),
          m_viewport.height.numerical_value_in (pixel),
          false);
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

    void discard_render_targets (gpu& metal) {
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
    viewport m_viewport;
  };
}
