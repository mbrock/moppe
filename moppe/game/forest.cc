#include <moppe/correct_math.hh>
#include <moppe/game/forest.hh>

#include <moppe/gfx/signal.hh>
#include <moppe/profile.hh>

#include <algorithm>
#include <cmath>
#include <iostream>
#include <span>
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
        .axis = normalized (Vec3 (0.0f, 1.0f, 0.0f) +
                            Vec3 (ground[0], 0.0f, ground[2]) * 0.08f +
                            Vec3 (cr::cos (turn), 0.0f, cr::sin (turn)) * lean),
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

    render::ForestInstance present (const ForestSite& site) {
      const float size = site.size.numerical_value_in (mp_units::one);
      const float cover = site.cover.numerical_value_in (mp_units::one);
      const float moisture = site.moisture.numerical_value_in (mp_units::one);
      const bool conifer = site.form == ForestForm::conifer;
      // The forest stands at the height of a mature stand -- most conifers
      // 20 to 35 metres -- with stout spruce cones and broad broadleaf crowns
      // lifted on clear trunks.
      const meters_t height = 1.5f * size * (conifer ? 15.0f : 13.4f) *
                              (0.82f + 0.30f * cover + 0.26f * moisture) * u::m;
      const float crown_share = 0.19f;
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

  namespace {
    // Turned leaves fall around and a little beyond each crown. Every
    // broadleaf spreads its turned leaf area over a Gaussian footprint
    // normalised on the lattice, so a grove's carpet is proportional to the
    // leaf area that has actually turned, and closes where crowns crowd.
    std::vector<std::uint8_t>
    spread_litter (std::span<const render::ForestInstance> instances,
                   const Vec3& period,
                   std::uint32_t size) {
      std::vector<std::uint8_t> cover;
      if (size == 0 || period[0] <= 0.0f || period[2] <= 0.0f)
        return cover;
      const float step_x = period[0] / static_cast<float> (size);
      const float step_z = period[2] / static_cast<float> (size);
      const float cell_area = step_x * step_z;
      const int count = static_cast<int> (size);
      const auto wrap = [count] (int value) {
        value %= count;
        return value < 0 ? value + count : value;
      };
      const auto periodic = [] (float value, float extent) {
        return value - std::round (value / extent) * extent;
      };
      std::vector<float> depth (static_cast<std::size_t> (size) * size, 0.0f);
      bool any = false;
      for (const render::ForestInstance& tree : instances) {
        const float autumn = tree.autumn.numerical_value_in (mp_units::one);
        if (tree.species != render::ForestSpecies::Broadleaf || autumn <= 0.0f)
          continue;
        any = true;
        const Vec3 root = position_value (tree.root);
        const float radius = tree.crown_radius.numerical_value_in (u::m);
        const float spread =
          std::max (0.95f * radius, 0.45f * std::max (step_x, step_z));
        const int reach =
          std::max (1,
                    static_cast<int> (
                      std::ceil (2.4f * spread / std::min (step_x, step_z))));
        const int centre_x = static_cast<int> (std::floor (root[0] / step_x));
        const int centre_z = static_cast<int> (std::floor (root[2] / step_z));
        const auto weight = [&] (int x, int z) {
          const float dx = periodic (
            (static_cast<float> (x) + 0.5f) * step_x - root[0], period[0]);
          const float dz = periodic (
            (static_cast<float> (z) + 0.5f) * step_z - root[2], period[2]);
          return cr::exp (-0.5f * (dx * dx + dz * dz) / (spread * spread));
        };
        float sum = 0.0f;
        for (int z = centre_z - reach; z <= centre_z + reach; ++z)
          for (int x = centre_x - reach; x <= centre_x + reach; ++x)
            sum += weight (wrap (x), wrap (z));
        const float leaf_area = 0.70f * 3.14159265f * radius * radius * autumn;
        for (int z = centre_z - reach; z <= centre_z + reach; ++z)
          for (int x = centre_x - reach; x <= centre_x + reach; ++x)
            depth[static_cast<std::size_t> (wrap (z)) * size + wrap (x)] +=
              leaf_area * weight (wrap (x), wrap (z)) /
              (cell_area * std::max (sum, 0.0001f));
      }
      if (!any)
        return cover;
      cover.resize (depth.size ());
      for (std::size_t index = 0; index < depth.size (); ++index)
        cover[index] = static_cast<std::uint8_t> (
          std::lround (255.0f * (1.0f - cr::exp (-2.5f * depth[index]))));
      return cover;
    }
  }

  float ForestLandscape::litter_at (const Vec3& position) const {
    if (m_litter.empty ())
      return 0.0f;
    const float size = static_cast<float> (m_litter_size);
    float gx = position[0] / m_period[0] * size - 0.5f;
    float gz = position[2] / m_period[2] * size - 0.5f;
    gx -= std::floor (gx / size) * size;
    gz -= std::floor (gz / size) * size;
    const auto x0 = static_cast<std::uint32_t> (gx) % m_litter_size;
    const auto z0 = static_cast<std::uint32_t> (gz) % m_litter_size;
    const std::uint32_t x1 = (x0 + 1) % m_litter_size;
    const std::uint32_t z1 = (z0 + 1) % m_litter_size;
    const float fx = gx - std::floor (gx);
    const float fz = gz - std::floor (gz);
    const auto at = [&] (std::uint32_t x, std::uint32_t z) {
      return static_cast<float> (m_litter[z * m_litter_size + x]) / 255.0f;
    };
    return (at (x0, z0) * (1 - fx) + at (x1, z0) * fx) * (1 - fz) +
           (at (x0, z1) * (1 - fx) + at (x1, z1) * fx) * fz;
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
    m_period = extent_value (plan.period);
    m_litter.clear ();
    m_litter_size = 0;
    turn_autumn (instances, plan.period);
    // About four metres a texel: two samples across a birch crown.
    const auto size = static_cast<std::uint32_t> (std::clamp (
      std::ceil (std::max (m_period[0], m_period[2]) / 4.0f), 64.0f, 1024.0f));
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
        return site.form == ForestForm::broadleaf;
      });
    std::cerr << "moppe: forest plan: " << plan.sites.size () << " sites, "
              << birches << " birch" << std::endl;
    m_resident_bytes = instances.size () * sizeof (render::ForestInstance);
  }

  void ForestLandscape::draw (render::Renderer& renderer) const {
    if (m_tree_count)
      renderer.draw_forest ();
    if (m_tree_count && !m_trunks.empty ())
      renderer.draw_falling_leaves ();
  }
}
