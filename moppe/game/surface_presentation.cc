#include <moppe/game/surface_presentation.hh>

#include <moppe/profile.hh>
namespace moppe::game {
  namespace {
    template <typename Quantity>
    std::vector<float> scalar_values (std::span<const Quantity> section) {
      std::vector<float> values;
      values.reserve (section.size ());
      for (Quantity value : section)
        values.push_back (value.numerical_value_in (one));
      return values;
    }
  }

  void SurfacePresentation::refresh (const map::SurfaceGeometry& geometry,
                                     const map::SurfaceReadings& readings) {
    MOPPE_PROFILE_ZONE ("surface.pack_presentation");
    m_trails = scalar_values<map::TrailInfluence> (
      spatial::get<map::trail_influence> (readings));
    m_home_base = scalar_values<map::HomeBaseInfluence> (
      spatial::get<map::home_base_influence> (readings));
    m_forest = scalar_values<map::ForestCover> (
      spatial::get<map::forest_cover> (readings));
    m_moisture = scalar_values<map::SurfaceMoisture> (
      spatial::get<map::surface_moisture> (readings));
    const auto& distance = spatial::get<map::waterline_distance> (readings);
    m_waterline_distance.resize (distance.size ());
    for (std::size_t offset = 0; offset < distance.size (); ++offset)
      m_waterline_distance[offset] = distance[offset].numerical_value_in (u::m);
    const auto& flux = spatial::get<map::channel_flux> (readings);
    m_channel_flux.resize (2 * flux.size ());
    for (std::size_t offset = 0; offset < flux.size (); ++offset) {
      const Vec3 value = flux[offset].numerical_value_in (one);
      m_channel_flux[2 * offset] = value[0];
      m_channel_flux[2 * offset + 1] = value[2];
    }
    const auto erosion = scalar_values<map::ErosionExposure> (
      spatial::get<map::erosion_exposure> (readings));
    const auto deposition = scalar_values<map::DepositionCover> (
      spatial::get<map::deposition_cover> (readings));
    m_geology.resize (2 * erosion.size ());
    for (std::size_t offset = 0; offset < erosion.size (); ++offset) {
      m_geology[2 * offset] = erosion[offset];
      m_geology[2 * offset + 1] = deposition[offset];
    }
    m_snow_support = scalar_values<map::SnowSupport> (
      spatial::get<map::snow_support> (geometry));
  }

  void SurfacePresentation::upload (render::Renderer& renderer,
                                    bool include_forest) const {
    MOPPE_PROFILE_ZONE ("surface.upload_presentation");
    renderer.set_terrain_moisture (m_moisture);
    renderer.set_terrain_geology (m_geology);
    renderer.set_terrain_shore (m_waterline_distance);
    renderer.set_terrain_forest (include_forest
                                   ? std::span<const float> (m_forest)
                                   : std::span<const float> ());
    renderer.set_terrain_snow_support (m_snow_support);
    renderer.set_terrain_channel_flux (m_channel_flux);
    renderer.set_terrain_paths (m_trails, m_home_base);
  }
}
