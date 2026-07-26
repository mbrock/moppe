// The one translation unit that carries the metal-cpp implementation.
#define NS_PRIVATE_IMPLEMENTATION
#define CA_PRIVATE_IMPLEMENTATION
#define MTL_PRIVATE_IMPLEMENTATION
#include <Foundation/Foundation.hpp>
#include <Metal/Metal.hpp>
#include <QuartzCore/QuartzCore.hpp>

#include "lavoir/renderer.hh"

#include "lavoir/lending.hh"
#include "lavoir/metal.hh"
#include "lavoir/pipeline.hh"
#include "lavoir/surface.hh"
#include "lavoir/wave.hh"

#include <cmath>
#include <stdexcept>
#include <vector>

namespace lavoir {
  namespace {
    /// The water's construction: a stretch of surface a dozen metres
    /// wide at finger resolution, waves at strolling pace, and the
    /// simulation ticking once per rendered frame. The Courant number
    /// these choices imply is a typed fact checked at construction.
    inline constexpr std::size_t water_sites = 768;
    inline constexpr auto water_width = 12.0f * si::metre;
    inline constexpr auto water_speed = 0.35f * si::metre / si::second;
    inline constexpr step_pace_t frame_pace = (1.0 / 60.0) * si::second / step;

    /// Where the resting waterline sits in the view, and the exchange
    /// rate carrying metres of displacement into clip space, chosen so
    /// a centimetre of water reads on screen.
    inline constexpr float rest_level_ndc = -0.12f;
    inline constexpr auto ndc_per_metre = 6.0f / si::metre;
  }

  /// The wash-house with water in it: a leapfrog wave field whose
  /// three storage levels are three tenancies, the level being written
  /// never one the GPU may still be reading. The renderer ticks the
  /// water once per frame and reports whichever level is current.
  class renderer::impl {
  public:
    impl ()
        : m_surface (m_metal.device ()),
          m_water (interval (water_sites),
                   water_width / (water_sites * unit),
                   water_speed,
                   frame_pace),
          m_pipeline (m_metal) {
      m_metal.use_residency_set (m_surface.residency_set ());
      m_tenancies.reserve (wave_levels);
      for (std::size_t level = 0; level < wave_levels; ++level)
        m_tenancies.emplace_back (m_metal,
                                  m_water.level_column (level).storage ());
      m_metal.commit_residency ();
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

      m_water.tick ();
      const frame_slot slot = m_metal.begin_frame ();
      const auto pass = m_surface.make_render_pass (drawable->texture ());
      m_pipeline.encode (m_metal,
                         pass.get (),
                         slot,
                         m_tenancies[m_water.level ()],
                         frame_uniforms ());
      m_metal.submit (drawable, slot);
    }

    image capture (seconds_t elapsed) {
      const viewport extent = m_surface.extent ();
      if (!m_surface.has_render_target ())
        throw std::runtime_error ("Cannot capture an unsized surface");

      // Run the water forward to the requested moment on its own
      // axis: a duration over a pace is a count of steps.
      const auto requested = elapsed / m_water.pace ();
      const auto ticks = static_cast<std::int64_t> (
        std::ceil (requested.numerical_value_in (step)));
      for (std::int64_t i = 0; i < ticks; ++i)
        m_water.tick ();

      const auto target = m_surface.make_capture_target (m_metal);
      const frame_slot slot = m_metal.begin_frame ();
      const auto pass = m_surface.make_render_pass (target.get ());
      m_pipeline.encode (m_metal,
                         pass.get (),
                         slot,
                         m_tenancies[m_water.level ()],
                         frame_uniforms ());
      m_metal.submit_offscreen (slot);
      m_metal.wait_until_idle ();

      // The row stride and total size fall out of the unit algebra:
      // bytes per pixel times pixels is bytes.
      const bytes_t row = extent.width * bgra_stride;
      const bytes_t total = extent.width * extent.height * bgra_stride / px;
      image pixels {
        .width = extent.width,
        .height = extent.height,
        .bgra = std::make_unique<std::uint8_t[]> (
          total.numerical_value_in (iec::byte)),
      };
      target->getBytes (pixels.bgra.get (),
                        row.numerical_value_in (iec::byte),
                        MTL::Region (0,
                                     0,
                                     extent.width.numerical_value_in (px),
                                     extent.height.numerical_value_in (px)),
                        0);
      m_metal.remove_resident (target.get ());
      m_metal.commit_residency ();
      return pixels;
    }

  private:
    water_uniforms frame_uniforms () const {
      return water_uniforms {
        .site_count = static_cast<std::uint32_t> (m_water.sites ().size ()),
        .ndc_per_metre = (ndc_per_metre * si::metre).numerical_value_in (one),
        .rest_level_ndc = rest_level_ndc,
        .time_seconds = static_cast<float> (
          (m_water.age () * m_water.pace ()).numerical_value_in (si::second)),
      };
    }

    gpu m_metal;
    surface m_surface;
    water_line m_water;
    water_pipeline m_pipeline;
    std::vector<tenancy> m_tenancies;
  };

  renderer::renderer () : m_impl (std::make_unique<impl> ()) {}

  renderer::~renderer () = default;

  void* renderer::native_layer () const {
    return m_impl->native_layer ();
  }

  void renderer::resize (pixels_t width, pixels_t height) {
    m_impl->resize (viewport { .width = width, .height = height });
  }

  void renderer::draw () {
    m_impl->draw ();
  }

  image renderer::capture (seconds_t elapsed) {
    return m_impl->capture (elapsed);
  }
}
