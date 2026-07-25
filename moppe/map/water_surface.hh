#ifndef MOPPE_MAP_WATER_SURFACE_HH
#define MOPPE_MAP_WATER_SURFACE_HH

#include <moppe/map/surface_sections.hh>
#include <moppe/terrain/watercourse.hh>

#include <span>

namespace moppe::map {
  using terrain::water_velocity;
  using terrain::WaterVelocity;
  using terrain::wave_amplitude;
  using terrain::WaveAmplitude;
  using WaterSurfaceSections = terrain::WaterSheets;

  // Standing and running water sampled over the terrain lattice. Elevation
  // shares the ground's affine frame; amplitude and velocity describe the
  // water itself and therefore live in a distinct bundle.
  class WaterSurface {
  public:
    explicit WaterSurface (WaterSurfaceSections sections);

    WaterSurface (terrain::TerrainDomain domain,
                  std::span<const float> level_and_amplitude,
                  std::span<const float> planar_flow);

    const WaterSurfaceSections& sections () const noexcept {
      return m_sections;
    }

    SurfaceElevation elevation_at (const position_t& position) const {
      return spatial::sample<surface_elevation> (m_sections, position);
    }

    WaterVelocity velocity_at (const position_t& position) const {
      return spatial::sample<water_velocity> (m_sections, position);
    }

  private:
    WaterSurfaceSections m_sections;
  };
}

#endif
