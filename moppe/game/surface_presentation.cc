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
    m_trails = scalar_values<map::TrailInfluence> (
      surface.section<map::trail_influence> ());
    m_home_base = scalar_values<map::HomeBaseInfluence> (
      surface.section<map::home_base_influence> ());
    m_forest =
      scalar_values<map::ForestCover> (surface.section<map::forest_cover> ());
    m_snow_support =
      scalar_values<map::SnowSupport> (surface.section<map::snow_support> ());

    const auto& flux = surface.section<map::channel_flux> ();
    m_channel_flux.resize (2 * flux.size ());
    for (std::size_t offset = 0; offset < flux.size (); ++offset) {
      const Vec3 value = flux[offset].numerical_value_in (one);
      m_channel_flux[2 * offset] = value[0];
      m_channel_flux[2 * offset + 1] = value[2];
    }
  }

  void SurfacePresentation::upload (render::Renderer& renderer,
                                    bool include_forest) const {
    MOPPE_PROFILE_ZONE ("surface.upload_presentation");
    if (include_forest)
      renderer.set_terrain_forest (m_forest);
    renderer.set_terrain_snow_support (m_snow_support);
    renderer.set_terrain_channel_flux (m_channel_flux);
    renderer.set_terrain_paths (m_trails, m_home_base);
  }
}
