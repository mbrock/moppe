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
    if (surface.has_section<map::trail_influence> ())
      m_trails = scalar_values<map::TrailInfluence> (
        surface.section<map::trail_influence> ());
    else
      m_trails.clear ();
    if (surface.has_section<map::home_base_influence> ())
      m_home_base = scalar_values<map::HomeBaseInfluence> (
        surface.section<map::home_base_influence> ());
    else
      m_home_base.clear ();
    if (surface.has_section<map::forest_cover> ())
      m_forest =
        scalar_values<map::ForestCover> (surface.section<map::forest_cover> ());
    else
      m_forest.clear ();
    if (surface.has_section<map::surface_moisture> ())
      m_moisture = scalar_values<map::SurfaceMoisture> (
        surface.section<map::surface_moisture> ());
    else
      m_moisture.clear ();
    if (surface.has_section<map::waterline_distance> ()) {
      const auto& distance = surface.section<map::waterline_distance> ();
      m_waterline_distance.resize (distance.size ());
      for (std::size_t offset = 0; offset < distance.size (); ++offset)
        m_waterline_distance[offset] =
          distance[offset].numerical_value_in (u::m);
    } else
      m_waterline_distance.clear ();
    m_snow_support =
      scalar_values<map::SnowSupport> (surface.section<map::snow_support> ());

    if (surface.has_section<map::channel_flux> ()) {
      const auto& flux = surface.section<map::channel_flux> ();
      m_channel_flux.resize (2 * flux.size ());
      for (std::size_t offset = 0; offset < flux.size (); ++offset) {
        const Vec3 value = flux[offset].numerical_value_in (one);
        m_channel_flux[2 * offset] = value[0];
        m_channel_flux[2 * offset + 1] = value[2];
      }
    } else
      m_channel_flux.clear ();

    if (surface.has_section<map::erosion_exposure> ()) {
      const auto erosion = scalar_values<map::ErosionExposure> (
        surface.section<map::erosion_exposure> ());
      const auto deposition = scalar_values<map::DepositionCover> (
        surface.section<map::deposition_cover> ());
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
