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

  void SurfacePresentation::refresh (const map::Surface& surface) {
    MOPPE_PROFILE_ZONE ("surface.materialize_presentation");
    const map::SurfaceAtlas& atlas = surface.atlas ();
    const map::SurfaceAtlas::Use& use = atlas.use ();
    const map::SurfaceAtlas::Ecology& ecology = atlas.ecology ();
    const map::SurfaceAtlas::Hydrology& hydrology = atlas.hydrology ();

    if (const auto* trails = use.trails ())
      m_trails = scalar_values<map::TrailInfluence> (
        spatial::get<map::trail_influence> (*trails));
    else
      m_trails.clear ();
    if (const auto* home_base = use.home_base ())
      m_home_base = scalar_values<map::HomeBaseInfluence> (
        spatial::get<map::home_base_influence> (*home_base));
    else
      m_home_base.clear ();
    if (const auto* forest = ecology.forest_cover ())
      m_forest = scalar_values<map::ForestCover> (
        spatial::get<map::forest_cover> (*forest));
    else
      m_forest.clear ();
    if (const auto* moisture = hydrology.moisture ())
      m_moisture = scalar_values<map::SurfaceMoisture> (
        spatial::get<map::surface_moisture> (*moisture));
    else
      m_moisture.clear ();
    if (const auto* waterline = hydrology.waterline ()) {
      const auto& distance = spatial::get<map::waterline_distance> (*waterline);
      m_waterline_distance.resize (distance.size ());
      for (std::size_t offset = 0; offset < distance.size (); ++offset)
        m_waterline_distance[offset] =
          distance[offset].numerical_value_in (u::m);
    } else
      m_waterline_distance.clear ();
    m_snow_support = scalar_values<map::SnowSupport> (
      spatial::get<map::snow_support> (atlas.geometry ()));

    if (const auto* channel_flux = hydrology.channel_flux ()) {
      const auto& flux = spatial::get<map::channel_flux> (*channel_flux);
      m_channel_flux.resize (2 * flux.size ());
      for (std::size_t offset = 0; offset < flux.size (); ++offset) {
        const Vec3 value = flux[offset].numerical_value_in (one);
        m_channel_flux[2 * offset] = value[0];
        m_channel_flux[2 * offset + 1] = value[2];
      }
    } else
      m_channel_flux.clear ();

    if (const auto* materials = atlas.geology ().materials ()) {
      const auto erosion = scalar_values<map::ErosionExposure> (
        spatial::get<map::erosion_exposure> (*materials));
      const auto deposition = scalar_values<map::DepositionCover> (
        spatial::get<map::deposition_cover> (*materials));
      m_geology.resize (2 * erosion.size ());
      for (std::size_t offset = 0; offset < erosion.size (); ++offset) {
        m_geology[2 * offset] = erosion[offset];
        m_geology[2 * offset + 1] = deposition[offset];
      }
    } else
      m_geology.clear ();
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
