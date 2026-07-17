#ifndef MOPPE_GAME_WATER_PRESENTATION_HH
#define MOPPE_GAME_WATER_PRESENTATION_HH

#include <moppe/map/water_surface.hh>
#include <moppe/render/renderer.hh>

#include <span>
#include <vector>

namespace moppe::game {
  // Presentation of the water bundle and the coarse ocean mesh. The bundle
  // stays physical; normalized height and interleaved lanes are produced only
  // for the renderer contract.
  class WaterPresentation {
  public:
    void reset (render::OceanSetup ocean);
    void refresh (const map::WaterSurface& water,
                  meters_t terrain_height_scale);
    void upload (render::Renderer& renderer) const;

    std::span<const float> levels () const noexcept {
      return m_levels;
    }
    std::span<const float> flow () const noexcept {
      return m_flow;
    }

  private:
    render::OceanSetup m_ocean;
    std::vector<float> m_levels;
    std::vector<float> m_flow;
  };
}

#endif
