#include <moppe/game/forest.hh>

#include <moppe/gfx/signal.hh>
#include <moppe/profile.hh>

#include <algorithm>
#include <cmath>
#include <iostream>
#include <vector>

namespace moppe::game {
  namespace {
    // moppe_forest_hash from shaders/metal/forest_medium.h: trunk colliders
    // must draw the same per-individual lean and girth the shader does.
    std::uint32_t forest_mix (std::uint32_t value) {
      value ^= value >> 16;
      value *= 0x7feb352du;
      value ^= value >> 15;
      value *= 0x846ca68bu;
      value ^= value >> 16;
      return value;
    }

    float forest_hash (std::uint32_t seed, std::uint32_t lane) {
      return static_cast<float> (forest_mix (seed ^ lane * 0x9e3779b9u) &
                                 0x00ffffffu) /
             static_cast<float> (0x01000000u);
    }

    // The trunk of forest_trunks.metal's trunk_tree: nearly vertical with a
    // small lean, girth proportional to height. The collider runs up into
    // the crown, where a rider or walker can no longer reach anyway.
    mov::Trunk trunk_of (const render::ForestInstance& tree) {
      const Vec3 ground = tree.ground_normal.numerical_value_in (mp_units::one);
      const float turn = 6.2831853f * forest_hash (tree.seed, 3u);
      const float lean = 0.035f * forest_hash (tree.seed, 4u);
      const bool conifer = tree.species == render::ForestSpecies::Conifer;
      const float height = tree.height.numerical_value_in (u::m);
      return {
        .root = position_value (tree.root),
        .axis = normalized (
          Vec3 (0.0f, 1.0f, 0.0f) + Vec3 (ground[0], 0.0f, ground[2]) * 0.08f +
          Vec3 (std::cos (turn), 0.0f, std::sin (turn)) * lean),
        .height = height * (conifer ? 0.9f : 0.7f),
        .radius = height * (conifer ? 0.0078f : 0.0085f) *
                  (0.85f + 0.3f * forest_hash (tree.seed, 6u)),
      };
    }

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
      // The procedural style has only its conifer construction, so it
      // presents every individual as spruce.
      const bool conifer = site.form == ForestForm::conifer ||
                           style == render::ForestStyle::Procedural;
      // The trunk forest stands at the height of a mature stand -- most
      // conifers 20 to 35 metres -- with stout spruce cones and broad
      // broadleaf crowns lifted on clear trunks.
      const bool trunks = style == render::ForestStyle::Trunks;
      const float scale = trunks ? 1.5f : 1.0f;
      const meters_t height = scale * size * (conifer ? 15.0f : 13.4f) *
                              (0.82f + 0.30f * cover + 0.26f * moisture) * u::m;
      const float crown_share =
        trunks ? (conifer ? 0.19f : 0.19f) : (conifer ? 0.23f : 0.25f);
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

  namespace {
    // Autumn comes first to the uplands. Broadleaves in roughly the upper
    // half of the forest's own elevation range have turned, earlier or later
    // by grove and by individual, so riding uphill passes from green valleys
    // into golden birch standing among dark spruce.
    void turn_autumn (std::vector<render::ForestInstance>& instances,
                      spatial_extent_t period) {
      if (instances.empty ())
        return;
      std::vector<float> heights;
      heights.reserve (instances.size ());
      for (const render::ForestInstance& tree : instances)
        heights.push_back (position_value (tree.root)[1]);
      const auto quantile = [&] (float q) {
        auto at = heights.begin () +
                  static_cast<std::ptrdiff_t> (q * (heights.size () - 1));
        std::nth_element (heights.begin (), at, heights.end ());
        return *at;
      };
      const float low = quantile (0.35f);
      const float high = quantile (0.70f);
      const float span = std::max (high - low, 1.0f);
      const Vec3 extent = extent_value (period);
      const auto laps = [] (float metres) {
        return std::max<std::uint32_t> (
          1, static_cast<std::uint32_t> (std::round (metres / 300.0f)));
      };
      for (render::ForestInstance& tree : instances) {
        if (tree.species == render::ForestSpecies::Conifer)
          continue;
        const Vec3 root = position_value (tree.root);
        const float grove =
          periodic_noise ((root[0] / extent[0]) * mp_units::one,
                          (root[2] / extent[2]) * mp_units::one,
                          laps (extent[0]),
                          0xa07ae1u)
            .numerical_value_in (mp_units::one);
        const float relative = (root[1] - low) / span +
                               0.25f * (forest_hash (tree.seed, 8u) - 0.5f) +
                               0.45f * (grove - 0.5f);
        tree.autumn =
          smoothstep (0.0f, 1.0f, std::clamp (relative, 0.0f, 1.0f)) *
          mp_units::one;
      }
    }
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
    if (style == render::ForestStyle::Trunks)
      turn_autumn (instances, plan.period);
    renderer.set_forest ({ .period = plan.period, .style = style }, instances);
    m_trunks.clear ();
    if (style == render::ForestStyle::Trunks) {
      m_trunks.reserve (instances.size ());
      for (const render::ForestInstance& tree : instances)
        m_trunks.push_back (trunk_of (tree));
    }
    m_tree_count = instances.size ();
    const auto birches =
      std::ranges::count_if (plan.sites, [] (const ForestSite& site) {
        return site.form == ForestForm::broadleaf;
      });
    std::cerr << "moppe: forest plan: " << plan.sites.size () << " sites, "
              << birches << " birch" << std::endl;
    m_resident_bytes = instances.size () * sizeof (render::ForestInstance);
  }

  void ForestLandscape::draw (render::Renderer& renderer) const {
    if (m_tree_count)
      renderer.draw_forest ();
  }
}
