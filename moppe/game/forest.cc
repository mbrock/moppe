#include <moppe/game/forest.hh>

#include <moppe/profile.hh>

#include <vector>

namespace moppe::game {
  namespace {
    render::ForestAge presented_age (ForestAge age) {
      switch (age) {
      case ForestAge::sapling:
        return render::ForestAge::Sapling;
      case ForestAge::young:
        return render::ForestAge::Young;
      case ForestAge::mature:
        return render::ForestAge::Mature;
      case ForestAge::ancient:
        return render::ForestAge::Ancient;
      }
      return render::ForestAge::Mature;
    }

    render::ForestInstance present (const ForestSite& site,
                                    render::ForestStyle style) {
      const float size = site.size.numerical_value_in (mp_units::one);
      const float cover = site.cover.numerical_value_in (mp_units::one);
      const float moisture = site.moisture.numerical_value_in (mp_units::one);
      const bool conifer = site.form == ForestForm::conifer;
      // The trunk forest stands at the height of a mature stand -- most
      // conifers 20 to 35 metres -- with stout spruce cones and broad
      // broadleaf crowns lifted on clear trunks.
      const bool trunks = style == render::ForestStyle::Trunks;
      const float scale = trunks ? 1.5f : 1.0f;
      const meters_t height = scale * size * (conifer ? 15.0f : 13.4f) *
                              (0.82f + 0.30f * cover + 0.26f * moisture) * u::m;
      const float crown_share =
        trunks ? (conifer ? 0.19f : 0.24f) : (conifer ? 0.23f : 0.25f);
      return {
        .root = site.position,
        .ground_normal = site.normal,
        .height = height,
        .crown_radius = crown_share * height,
        .canopy_cover = cover * mp_units::one,
        .moisture = moisture * mp_units::one,
        .seed = site.seed,
        .species = conifer ? render::ForestSpecies::Conifer
                           : render::ForestSpecies::Broadleaf,
        .age = presented_age (site.age),
      };
    }
  }

  void ForestLandscape::rebuild (render::Renderer& renderer,
                                 const map::SurfaceGeometry& surface,
                                 const map::SurfaceReadings& readings,
                                 std::uint32_t seed) {
    MOPPE_PROFILE_ZONE ("ForestLandscape::rebuild");
    rebuild (renderer, plan_global_forest (surface, readings, seed));
  }

  void ForestLandscape::rebuild (render::Renderer& renderer,
                                 const ForestPlan& plan,
                                 render::ForestStyle style) {
    MOPPE_PROFILE_ZONE ("ForestLandscape::upload_instances");
    std::vector<render::ForestInstance> instances;
    instances.reserve (plan.sites.size ());
    for (const ForestSite& site : plan.sites) {
      // Larger individuals need more room: the trunk forest keeps a stable
      // share of the plan rather than every planted site.
      if (style == render::ForestStyle::Trunks &&
          (site.seed * 2654435761u >> 8) % 1000u >= 550u)
        continue;
      instances.push_back (present (site, style));
    }
    renderer.set_forest ({ .period = plan.period, .style = style }, instances);
    m_tree_count = instances.size ();
    m_resident_bytes = instances.size () * sizeof (render::ForestInstance);
  }

  void ForestLandscape::draw (render::Renderer& renderer) const {
    if (m_tree_count)
      renderer.draw_forest ();
  }
}
