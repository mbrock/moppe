#ifndef MOPPE_GAME_WATER_PRESENTATION_HH
#define MOPPE_GAME_WATER_PRESENTATION_HH

#include <moppe/render/renderer.hh>
#include <moppe/terrain/watercourse.hh>

#include <span>
#include <vector>

namespace moppe::game {
  // Presentation of the water bundle and the coarse ocean mesh. Elevation
  // stays in metres; only heterogeneous sections are packed into renderer
  // lanes.
  class WaterPresentation {
  public:
    // The renderer's ocean setup is built here so that world assembly retains
    // its physical datum and extent until the presentation boundary.
    void reset (meters_t water_datum, const spatial_extent_t& world_extent);
    void refresh (const terrain::WaterSheets& water);
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
