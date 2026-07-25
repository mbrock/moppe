#include <moppe/map/water_surface.hh>

#include <moppe/terrain/watercourse.hh>

#include <stdexcept>
#include <utility>

namespace moppe::map {
  WaterSurface::WaterSurface (SurfaceDomain domain,
                              const terrain::WaterSheets& sheets,
                              meters_t terrain_height_scale)
      : m_sections (std::move (domain)) {
    if (terrain_height_scale <= 0.0f * u::m)
      throw std::invalid_argument ("Water surface needs a positive height "
                                   "scale");
    const SurfaceDomain& lattice = m_sections.domain ();
    if (sheets.surface.domain ().width != lattice.width () ||
        sheets.surface.domain ().height != lattice.height ())
      throw std::invalid_argument (
        "Water sheets do not share the surface lattice");
    const std::span<const float> levels = sheets.surface.values ();
    const float height_scale = meters_value (terrain_height_scale);
    for (std::size_t offset = 0; offset < lattice.size (); ++offset) {
      auto site = m_sections[m_sections.index (offset)];
      spatial::get<surface_elevation> (site) = SurfaceElevation (
        levels[offset] * height_scale * surface_elevation[u::m]);
      spatial::get<wave_amplitude> (site) =
        sheets.amplitude[offset] * wave_amplitude[one];
      spatial::get<water_velocity> (site) =
        Vec3 (sheets.flow[2 * offset], 0.0f, sheets.flow[2 * offset + 1]) *
        water_velocity[u::m / u::s];
    }
  }

  WaterSurface::WaterSurface (
    SurfaceDomain domain,
    std::span<const float> normalized_level_and_amplitude,
    std::span<const float> planar_flow,
    meters_t terrain_height_scale)
      : m_sections (std::move (domain)) {
    if (terrain_height_scale <= 0.0f * u::m)
      throw std::invalid_argument ("Water surface needs a positive height "
                                   "scale");
    if (normalized_level_and_amplitude.size () != 2 * m_sections.size ())
      throw std::invalid_argument (
        "Water surface needs one level and amplitude per lattice site");
    if (planar_flow.size () != 2 * m_sections.size ())
      throw std::invalid_argument (
        "Water surface needs one planar velocity per lattice site");

    for (std::size_t offset = 0; offset < m_sections.size (); ++offset) {
      auto site = m_sections[m_sections.index (offset)];
      spatial::get<surface_elevation> (site) = SurfaceElevation (
        normalized_level_and_amplitude[2 * offset] *
        meters_value (terrain_height_scale) * surface_elevation[u::m]);
      spatial::get<wave_amplitude> (site) =
        normalized_level_and_amplitude[2 * offset + 1] * wave_amplitude[one];
      spatial::get<water_velocity> (site) =
        Vec3 (planar_flow[2 * offset], 0.0f, planar_flow[2 * offset + 1]) *
        water_velocity[u::m / u::s];
    }
  }
}
