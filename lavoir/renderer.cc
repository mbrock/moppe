// The one translation unit that carries the metal-cpp implementation.
#define NS_PRIVATE_IMPLEMENTATION
#define CA_PRIVATE_IMPLEMENTATION
#define MTL_PRIVATE_IMPLEMENTATION
#include <Foundation/Foundation.hpp>
#include <Metal/Metal.hpp>
#include <QuartzCore/QuartzCore.hpp>

#include "lavoir/renderer.hh"

#include "lavoir/metal.hh"
#include "lavoir/surface.hh"

#include <stdexcept>

namespace lavoir {
  /// The first light of the workshop: every frame clears to linen and
  /// resolves. Scenes will earn their pipelines; the frame pacing,
  /// residency, and capture path are already the real ones.
  class renderer::impl {
  public:
    impl () : m_surface (m_metal.device ()) {
      m_metal.use_residency_set (m_surface.residency_set ());
    }

    void* native_layer () const {
      return m_surface.native_layer ();
    }

    void resize (viewport extent) {
      m_surface.resize (m_metal, extent);
    }

    void draw () {
      if (!m_surface.has_render_target ())
        return;
      CA::MetalDrawable* drawable = m_surface.next_drawable ();
      if (!drawable)
        return;

      const frame_slot slot = m_metal.begin_frame ();
      clear_into (drawable->texture ());
      m_metal.submit (drawable, slot);
    }

    image capture (double seconds_elapsed) {
      (void)seconds_elapsed;
      const viewport extent = m_surface.extent ();
      if (!m_surface.has_render_target ())
        throw std::runtime_error ("Cannot capture an unsized surface");

      const auto target = m_surface.make_capture_target (m_metal);
      const frame_slot slot = m_metal.begin_frame ();
      clear_into (target.get ());
      m_metal.submit_offscreen (slot);
      m_metal.wait_until_idle ();

      image pixels {
        .width = extent.width,
        .height = extent.height,
        .bgra =
          std::make_unique<std::uint8_t[]> (4 * extent.width * extent.height),
      };
      target->getBytes (pixels.bgra.get (),
                        4 * extent.width,
                        MTL::Region (0, 0, extent.width, extent.height),
                        0);
      m_metal.remove_resident (target.get ());
      m_metal.commit_residency ();
      return pixels;
    }

  private:
    void clear_into (MTL::Texture* resolve_target) {
      const auto pass = m_surface.make_render_pass (resolve_target);
      MTL4::RenderCommandEncoder* encoder =
        m_metal.command_buffer ()->renderCommandEncoder (pass.get ());
      encoder->endEncoding ();
    }

    gpu m_metal;
    surface m_surface;
  };

  renderer::renderer () : m_impl (std::make_unique<impl> ()) {}

  renderer::~renderer () = default;

  void* renderer::native_layer () const {
    return m_impl->native_layer ();
  }

  void renderer::resize (std::size_t width, std::size_t height) {
    m_impl->resize (viewport { .width = width, .height = height });
  }

  void renderer::draw () {
    m_impl->draw ();
  }

  image renderer::capture (double seconds_elapsed) {
    return m_impl->capture (seconds_elapsed);
  }
}
