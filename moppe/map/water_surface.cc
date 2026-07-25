#include <moppe/map/water_surface.hh>

#include <moppe/terrain/watercourse.hh>

#include <stdexcept>
#include <utility>

namespace moppe::map {
  WaterSurface::WaterSurface (WaterSurfaceSections sections)
      : m_sections (std::move (sections)) {}

  WaterSurface::WaterSurface (terrain::TerrainDomain domain,
                              std::span<const float> level_and_amplitude,
                              std::span<const float> planar_flow)
      : m_sections (std::move (domain)) {
    if (level_and_amplitude.size () != 2 * m_sections.size ())
      throw std::invalid_argument (
        "Water surface needs one level and amplitude per lattice site");
    if (planar_flow.size () != 2 * m_sections.size ())
      throw std::invalid_argument (
        "Water surface needs one planar velocity per lattice site");

    for (std::size_t offset = 0; offset < m_sections.size (); ++offset) {
      auto site = m_sections[m_sections.index (offset)];
      spatial::get<surface_elevation> (site) = SurfaceElevation (
        level_and_amplitude[2 * offset] * surface_elevation[u::m]);
      spatial::get<wave_amplitude> (site) =
        level_and_amplitude[2 * offset + 1] * wave_amplitude[one];
      spatial::get<water_velocity> (site) =
        Vec3 (planar_flow[2 * offset], 0.0f, planar_flow[2 * offset + 1]) *
        water_velocity[u::m / u::s];
    }
  }
}
