#ifndef MOPPE_GAME_SURFACE_PRESENTATION_HH
#define MOPPE_GAME_SURFACE_PRESENTATION_HH

#include <moppe/map/surface.hh>
#include <moppe/render/renderer.hh>

#include <span>
#include <vector>

namespace moppe::game {
  // The explicit bridge from typed intrinsic surface sections to the scalar
  // texture payloads consumed by the renderer. No simulation rule belongs
  // here: this is the one place where quantities become GPU-facing numbers.
  class SurfacePresentation {
  public:
    void refresh (const map::Surface& surface);
    void upload (render::Renderer& renderer, bool include_forest) const;

    std::span<const float> trails () const noexcept {
      return m_trails;
    }
    std::span<const float> home_base () const noexcept {
      return m_home_base;
    }
    std::span<const float> forest () const noexcept {
      return m_forest;
    }
    std::span<const float> moisture () const noexcept {
      return m_moisture;
    }
    std::span<const float> waterline_distance () const noexcept {
      return m_waterline_distance;
    }
    std::span<const float> geology () const noexcept {
      return m_geology;
    }
    std::span<const float> snow_support () const noexcept {
      return m_snow_support;
    }
    std::span<const float> channel_flux () const noexcept {
      return m_channel_flux;
    }

  private:
    std::vector<float> m_trails;
    std::vector<float> m_home_base;
    std::vector<float> m_forest;
    std::vector<float> m_moisture;
    std::vector<float> m_waterline_distance;
    std::vector<float> m_geology;
    std::vector<float> m_snow_support;
    std::vector<float> m_channel_flux;
  };
}

#endif
