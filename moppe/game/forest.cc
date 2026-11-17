#include <moppe/correct_math.hh>
#include <moppe/game/forest.hh>

#include <moppe/gfx/signal.hh>
#include <moppe/profile.hh>
#include <moppe/terrain/torus.hh>

#include <algorithm>
#include <cmath>
#include <iostream>
#include <span>
#include <vector>

namespace moppe::game {
  namespace {
    using LeafAreaIndex =
      mp_units::quantity<leaf_area_index[mp_units::one], float>;
  }

  // The forest stands at the height of a mature stand -- most conifers 20
  // to 35 metres.
  meters_t tree_height (const ForestSite& site) noexcept {
    const meters_t species = site.form == ForestForm::Conifer ? 15.0f * u::m
                             : site.form == ForestForm::Snag  ? 15.0f * u::m
                             : site.form == ForestForm::Pine  ? 14.0f * u::m
                                                              : 13.4f * u::m;
    // Cover and moisture both favour a taller stand.
    const scale_factor_t vigour =
      (0.82f + 0.30f * site.cover.numerical_value_in (one) +
       0.26f * site.moisture.numerical_value_in (one)) *
      scale_factor[one];
    return 1.5f * site.size.numerical_value_in (one) * species *
           vigour.numerical_value_in (one);
  }

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

    // A uniform draw from [0, 1): how far along that range this
    // individual's lane falls.
    proportion_t forest_hash (std::uint32_t seed, std::uint32_t lane) {
      return static_cast<float> (forest_mix (seed ^ lane * 0x9e3779b9u) &
                                 0x00ffffffu) /
             static_cast<float> (0x01000000u) * proportion[one];
    }

    // The trunk of forest_trunks.metal's trunk_tree: nearly vertical with a
    // small lean, girth proportional to height. The collider runs up into
    // the crown, where a rider or walker can no longer reach anyway.
    mov::Trunk trunk_of (const render::ForestInstance& tree) {
      const Vec3 ground = tree.ground_normal.numerical_value_in (one);
      const heading_t turn =
        6.2831853f * forest_hash (tree.seed, 3u).numerical_value_in (one) *
        heading[si::radian];
      // Small enough that the offset across the axis is the angle itself.
      const inclination_t lean =
        0.035f * forest_hash (tree.seed, 4u).numerical_value_in (one) *
        inclination[si::radian];

      const bool conifer = tree.species == render::ForestSpecies::Conifer;
      const bool pine = tree.species == render::ForestSpecies::Pine;
      const bool fell = tree.species == render::ForestSpecies::FellBirch;
      // A snag stands its whole broken height, a spruce's girth.
      const bool snag = tree.species == render::ForestSpecies::Snag;
      const meters_t height = tree.height;
      return {
        .root = tree.root,
        .axis = unit_direction (normalized (
          Vec3 (0.0f, 1.0f, 0.0f) + Vec3 (ground[0], 0.0f, ground[2]) * 0.08f +
          Vec3 (cr::cos (radians_value (turn)),
                0.0f,
                cr::sin (radians_value (turn))) *
            lean.numerical_value_in (si::radian))),
        // A fell birch's main stem: short and stout for its height. Its
        // other stems spring from the same base and are not obstacles.
        .height = (height * (conifer ? 0.9f
                             : snag  ? 0.95f
                             : fell  ? 0.55f
                                     : 0.7f)),
        .radius =
          (height *
           (conifer || snag ? 0.0078f
            : fell          ? 0.014f
                            : 0.0085f) *
           (0.85f +
            0.3f * forest_hash (tree.seed, 6u).numerical_value_in (one))),
        // The crown's base, as the forest program's grow-tree places it:
        // low on an open-grown tree, higher in a closed stand, and far
        // higher on a birch than on a spruce.
        .clear =
          (height *
           (std::lerp (conifer ? 0.04f
                       : pine  ? 0.50f
                       : fell  ? 0.10f
                       : snag  ? 0.25f
                               : 0.40f,
                       conifer ? 0.20f
                       : pine  ? 0.66f
                       : fell  ? 0.18f
                       : snag  ? 0.40f
                               : 0.60f,
                       band (0.12f * proportion[one],
                             0.42f * proportion[one],
                             tree.canopy_cover)
                         .numerical_value_in (one)) +
            0.08f *
              (forest_hash (tree.seed, 5u).numerical_value_in (one) - 0.5f))),
      };
    }

    render::ForestInstance present (const ForestSite& site) {
      // The forest stands at the height of a mature stand -- most conifers
      // 20 to 35 metres -- with stout spruce cones and broad broadleaf crowns
      // lifted on clear trunks.
      const meters_t height = tree_height (site);
      // A fell birch spreads as wide as it stands tall.
      const scale_factor_t crown_share =
        (site.form == ForestForm::FellBirch ? 0.34f : 0.19f) *
        scale_factor[one];
      return {
        .root = site.position,
        .ground_normal = site.normal,
        .height = height,
        .crown_radius = crown_share.numerical_value_in (one) * height,
        .canopy_cover = site.cover,
        .moisture = site.moisture,
        .seed = site.seed,
        .species = site.form,
        .age = site.age,
      };
    }
  }

  void ForestLandscape::rebuild (render::Renderer& renderer,
                                 const map::SurfaceGeometry& surface,
                                 const map::SurfaceReadings& readings,
                                 const terrain::WaterSheets& water,
                                 std::uint32_t seed) {
    MOPPE_PROFILE_ZONE ("ForestLandscape::rebuild");
    rebuild (renderer, plan_global_forest (surface, readings, water, seed));
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
      const auto standing = [] (const render::ForestInstance& tree) {
        return position_value (tree.root)[1] * u::m;
      };
      std::vector<meters_t> heights;
      heights.reserve (instances.size ());
      for (const render::ForestInstance& tree : instances)
        heights.push_back (standing (tree));
      const auto quantile = [&] (proportion_t q) {
        auto at = heights.begin () +
                  static_cast<std::ptrdiff_t> (q.numerical_value_in (one) *
                                               (heights.size () - 1));
        std::nth_element (heights.begin (), at, heights.end ());
        return *at;
      };
      const meters_t low = quantile (0.35f * proportion[one]);
      const meters_t high = quantile (0.70f * proportion[one]);
      const meters_t span = std::max (high - low, 1.0f * u::m);
      const meters_t width = extent_value (period)[0] * u::m;
      const meters_t depth = extent_value (period)[2] * u::m;
      // Groves some three hundred metres across.
      const auto laps = [] (meters_t extent) {
        return std::max<std::uint32_t> (
          1,
          static_cast<std::uint32_t> (
            std::round ((extent / (300.0f * u::m)).numerical_value_in (one))));
      };
      for (render::ForestInstance& tree : instances) {
        if (tree.species != render::ForestSpecies::Broadleaf &&
            tree.species != render::ForestSpecies::FellBirch)
          continue;
        const plan_position_t at = plan (tree.root);
        const noise_signal_t grove = periodic_noise (
          x_of (at) / width, z_of (at) / depth, laps (width), 0xa07ae1u);
        // How far up the forest's own band of elevation, by grove and by
        // individual.
        const proportion_t relative =
          std::clamp (
            ((standing (tree) - low) / span).numerical_value_in (one) +
              0.25f *
                (forest_hash (tree.seed, 8u).numerical_value_in (one) - 0.5f) +
              0.45f * (grove.numerical_value_in (one) - 0.5f),
            0.0f,
            1.0f) *
          proportion[one];
        tree.autumn =
          band (0.0f * proportion[one], 1.0f * proportion[one], relative);
      }
    }
  }

  namespace {
    // Turned leaves fall around and a little beyond each crown. Every
    // broadleaf spreads its turned leaf area over a Gaussian footprint
    // normalised on the lattice, so a grove's carpet is proportional to the
    // leaf area that has actually turned, and closes where crowns crowd.
    std::vector<std::uint8_t>
    spread_litter (std::span<const render::ForestInstance> instances,
                   const spatial_extent_t& period,
                   std::uint32_t size) {
      std::vector<std::uint8_t> cover;
      const meters_t period_x = extent_value (period)[0] * u::m;
      const meters_t period_z = extent_value (period)[2] * u::m;
      if (size == 0 || period_x <= 0.0f * u::m || period_z <= 0.0f * u::m)
        return cover;
      const meters_t step_x = period_x / static_cast<float> (size);
      const meters_t step_z = period_z / static_cast<float> (size);
      const square_meters_t cell_area = step_x * step_z;
      const int count = static_cast<int> (size);
      const auto wrap = [count] (int value) {
        value %= count;
        return value < 0 ? value + count : value;
      };
      // Turned leaf area over each texel's ground.
      std::vector<LeafAreaIndex> depth (static_cast<std::size_t> (size) * size,
                                        0.0f * leaf_area_index[one]);
      bool any = false;
      for (const render::ForestInstance& tree : instances) {
        if ((tree.species != render::ForestSpecies::Broadleaf &&
             tree.species != render::ForestSpecies::FellBirch) ||
            tree.autumn <= 0.0f * proportion[one])
          continue;
        any = true;
        const plan_position_t root = plan (tree.root);
        const meters_t radius = tree.crown_radius;
        const meters_t spread =
          std::max (0.95f * radius, 0.45f * std::max (step_x, step_z));
        const int reach =
          std::max (1,
                    static_cast<int> (
                      std::ceil ((2.4f * spread / std::min (step_x, step_z))
                                   .numerical_value_in (one))));
        const int centre_x = static_cast<int> (
          std::floor ((x_of (root) / step_x).numerical_value_in (one)));
        const int centre_z = static_cast<int> (
          std::floor ((z_of (root) / step_z).numerical_value_in (one)));
        // The Gaussian footprint's height over a texel, one at the trunk.
        const auto weight = [&] (int x, int z) {
          const meters_t dx = terrain::minimum_image_delta (
            (static_cast<float> (x) + 0.5f) * step_x - x_of (root), period_x);
          const meters_t dz = terrain::minimum_image_delta (
            (static_cast<float> (z) + 0.5f) * step_z - z_of (root), period_z);
          return cr::exp ((-0.5f * (dx * dx + dz * dz) / (spread * spread))
                            .numerical_value_in (one)) *
                 scale_factor[one];
        };
        scale_factor_t sum = 0.0f * scale_factor[one];
        for (int z = centre_z - reach; z <= centre_z + reach; ++z)
          for (int x = centre_x - reach; x <= centre_x + reach; ++x)
            sum += weight (wrap (x), wrap (z));
        const square_meters_t leaf_area = 0.70f * 3.14159265f * radius *
                                          radius *
                                          tree.autumn.numerical_value_in (one);
        const scale_factor_t least = 0.0001f * scale_factor[one];
        for (int z = centre_z - reach; z <= centre_z + reach; ++z)
          for (int x = centre_x - reach; x <= centre_x + reach; ++x)
            depth[static_cast<std::size_t> (wrap (z)) * size + wrap (x)] +=
              (leaf_area * weight (wrap (x), wrap (z)) /
               (cell_area * std::max (sum, least)))
                .numerical_value_in (one) *
              leaf_area_index[one];
      }
      if (!any)
        return cover;
      cover.resize (depth.size ());
      for (std::size_t index = 0; index < depth.size (); ++index)
        cover[index] = static_cast<std::uint8_t> (std::lround (
          255.0f *
          (1.0f - cr::exp (-2.5f * depth[index].numerical_value_in (one)))));
      return cover;
    }
  }

  proportion_t ForestLandscape::litter_at (const plan_position_t& at) const {
    if (m_litter.empty ())
      return 0.0f * proportion[one];
    // Where on the lattice, in texels.
    const float size = static_cast<float> (m_litter_size);
    float gx = (x_of (at) / (extent_value (m_period)[0] * u::m))
                   .numerical_value_in (one) *
                 size -
               0.5f;
    float gz = (z_of (at) / (extent_value (m_period)[2] * u::m))
                   .numerical_value_in (one) *
                 size -
               0.5f;
    gx -= std::floor (gx / size) * size;
    gz -= std::floor (gz / size) * size;
    const auto x0 = static_cast<std::uint32_t> (gx) % m_litter_size;
    const auto z0 = static_cast<std::uint32_t> (gz) % m_litter_size;
    const std::uint32_t x1 = (x0 + 1) % m_litter_size;
    const std::uint32_t z1 = (z0 + 1) % m_litter_size;
    const float fx = gx - std::floor (gx);
    const float fz = gz - std::floor (gz);
    const auto texel = [&] (std::uint32_t x, std::uint32_t z) {
      return static_cast<float> (m_litter[z * m_litter_size + x]) / 255.0f *
             proportion[one];
    };
    return (texel (x0, z0) * (1 - fx) + texel (x1, z0) * fx) * (1 - fz) +
           (texel (x0, z1) * (1 - fx) + texel (x1, z1) * fx) * fz;
  }

  void ForestLandscape::rebuild (render::Renderer& renderer,
                                 const ForestPlan& plan) {
    MOPPE_PROFILE_ZONE ("ForestLandscape::upload_instances");
    std::vector<render::ForestInstance> instances;
    instances.reserve (plan.sites.size ());
    for (const ForestSite& site : plan.sites) {
      // Larger individuals need more room: the forest keeps a stable share
      // of the plan rather than every planted site.
      if ((site.seed * 2654435761u >> 8) % 1000u >= 550u)
        continue;
      instances.push_back (present (site));
    }
    m_period = plan.period;
    m_litter.clear ();
    m_litter_size = 0;
    turn_autumn (instances, plan.period);
    // About four metres a texel: two samples across a birch crown.
    const meters_t longest =
      std::max (extent_value (m_period)[0], extent_value (m_period)[2]) * u::m;
    const auto size = static_cast<std::uint32_t> (std::clamp (
      std::ceil ((longest / (4.0f * u::m)).numerical_value_in (one)),
      64.0f,
      1024.0f));
    m_litter = spread_litter (instances, m_period, size);
    m_litter_size = m_litter.empty () ? 0 : size;
    renderer.set_forest ({ .period = plan.period,
                           .litter = m_litter,
                           .litter_size = m_litter_size },
                         instances);
    m_trunks.clear ();
    m_trunks.reserve (instances.size ());
    for (const render::ForestInstance& tree : instances)
      m_trunks.push_back (trunk_of (tree));
    m_tree_count = instances.size ();
    const auto birches =
      std::ranges::count_if (plan.sites, [] (const ForestSite& site) {
        return site.form == ForestForm::Broadleaf;
      });
    const auto pines =
      std::ranges::count_if (plan.sites, [] (const ForestSite& site) {
        return site.form == ForestForm::Pine;
      });
    const auto fell_birches =
      std::ranges::count_if (plan.sites, [] (const ForestSite& site) {
        return site.form == ForestForm::FellBirch;
      });
    const auto snags =
      std::ranges::count_if (plan.sites, [] (const ForestSite& site) {
        return site.form == ForestForm::Snag;
      });
    std::cerr << "moppe: forest plan: " << plan.sites.size () << " sites, "
              << birches << " birch, " << pines << " pine, " << fell_birches
              << " fell birch, " << snags << " snags" << std::endl;
    m_resident_bytes = instances.size () * sizeof (render::ForestInstance);
  }

  void ForestLandscape::draw (render::Renderer& renderer) const {
    if (m_tree_count)
      renderer.draw_forest ();
    if (m_tree_count && !m_trunks.empty ())
      renderer.draw_falling_leaves ();
  }
}
