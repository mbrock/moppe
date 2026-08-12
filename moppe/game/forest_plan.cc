#include <moppe/game/forest_plan.hh>

#include <moppe/gfx/signal.hh>

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <limits>
#include <stdexcept>
#include <type_traits>

namespace moppe::game {
  namespace {
    constexpr std::uint64_t forest_plan_magic = 0x4d4f505045465253ULL;
    constexpr std::uint32_t forest_plan_version = 9;

    // Marginal woodland stays close to the old proposal density while the
    // most suitable habitat can form a genuinely closed spruce stand. The
    // hard-core pass remains the physical upper bound. Proposals themselves
    // are a uniform deterministic stream, not one draw per square, so no
    // planting lattice exists to survive the rejection.
    constexpr float forest_proposal_scale_min = 0.55f;
    constexpr float forest_proposal_scale_max = 0.95f;
    constexpr float forest_exclusion_ratio = 0.40f;

    struct ForestPlanHeader {
      std::uint64_t magic;
      std::uint32_t version;
      std::uint32_t seed;
      std::array<float, 3> period;
      std::uint32_t reserved;
      std::uint64_t site_count;
    };

    struct ForestSiteRecord {
      std::array<float, 3> position;
      std::array<float, 3> normal;
      float cover;
      float moisture;
      float size;
      std::uint32_t seed;
      std::uint32_t form;
      std::uint32_t age;
    };

    static_assert (std::is_trivially_copyable_v<ForestPlanHeader>);
    static_assert (std::is_trivially_copyable_v<ForestSiteRecord>);
    static_assert (sizeof (ForestPlanHeader) == 40);
    static_assert (sizeof (ForestSiteRecord) == 48);

    struct ForestCandidate {
      float x;
      float z;
      float cover;
      float priority;
      std::uint32_t identity;
    };

    position_t sample_position (meters_t x, meters_t z) {
      return position (
        Vec3 (x.numerical_value_in (u::m), 0, z.numerical_value_in (u::m)));
    }

    terrain::SurfaceElevation
    elevation_at (const map::SurfaceGeometry& surface, meters_t x, meters_t z) {
      return spatial::sample<terrain::surface_elevation> (
        surface, sample_position (x, z));
    }

    terrain::TerrainNormal
    normal_at (const map::SurfaceGeometry& surface, meters_t x, meters_t z) {
      return spatial::sample<terrain::terrain_normal> (surface,
                                                       sample_position (x, z));
    }

    map::ForestCover
    cover_at (const map::SurfaceReadings& readings, meters_t x, meters_t z) {
      return spatial::sample<map::forest_cover> (readings,
                                                 sample_position (x, z));
    }

    map::SurfaceMoisture
    moisture_at (const map::SurfaceReadings& readings, meters_t x, meters_t z) {
      return spatial::sample<map::surface_moisture> (readings,
                                                     sample_position (x, z));
    }

    position_t forest_position (meters_t x,
                                terrain::SurfaceElevation elevation,
                                meters_t z) {
      return position (
        Vec3 (x.numerical_value_in (u::m),
              elevation.quantity_from_zero ().numerical_value_in (u::m),
              z.numerical_value_in (u::m)));
    }

    bool same_extent (const std::array<float, 3>& stored,
                      const spatial_extent_t& expected) {
      const Vec3 value = extent_value (expected);
      return stored[0] == value[0] && stored[1] == value[1] &&
             stored[2] == value[2];
    }

    template <typename T>
    void write_record (std::ofstream& output, const T& value) {
      output.write (reinterpret_cast<const char*> (&value), sizeof (value));
      if (!output)
        throw std::runtime_error ("could not write forest plan");
    }

    ForestAge age_from_identity (std::uint32_t identity) {
      const float draw = hash_lane (identity, 6);
      if (draw < 0.18f)
        return ForestAge::sapling;
      if (draw < 0.46f)
        return ForestAge::young;
      if (draw < 0.94f)
        return ForestAge::mature;
      return ForestAge::ancient;
    }

    TreeSizeFactor size_for_age (ForestAge age, std::uint32_t identity) {
      const float draw = hash_lane (identity, 4);
      switch (age) {
      case ForestAge::sapling:
        return (0.18f + 0.27f * draw) * tree_size_factor[one];
      case ForestAge::young:
        return (0.48f + 0.32f * draw) * tree_size_factor[one];
      case ForestAge::mature:
        return (0.78f + 0.42f * draw) * tree_size_factor[one];
      case ForestAge::ancient:
        return (1.25f + 0.45f * draw) * tree_size_factor[one];
      }
      return 1.0f * tree_size_factor[one];
    }
  }

  ForestPlan plan_global_forest (const map::SurfaceGeometry& surface,
                                 const map::SurfaceReadings& readings,
                                 std::uint32_t seed,
                                 meters_t spacing) {
    if (spacing <= 0.0f * u::m)
      throw std::invalid_argument ("Forest spacing must be positive");
    const terrain::TerrainDomain& domain = surface.domain ();
    ForestPlan plan;
    const meters_t width = domain.period_x ();
    const meters_t depth = domain.period_z ();
    plan.period = spatial_extent_in_metres (Vec3 (
      width.numerical_value_in (u::m), 0, depth.numerical_value_in (u::m)));
    // Draw a deterministic uniform proposal stream over the whole torus, then
    // use habitat-weighted selection and Matérn-style priority thinning. The
    // count matches the earlier fine proposal density, but positions have no
    // cell boundaries or preferred row and column for the hard-core pass to
    // inherit.
    const meters_t proposal_spacing = spacing / std::sqrt (2.0f);
    const std::uint64_t proposal_count = std::max<std::uint64_t> (
      1,
      static_cast<std::uint64_t> (
        std::ceil ((width / proposal_spacing).numerical_value_in (one) *
                   (depth / proposal_spacing).numerical_value_in (one))));
    std::vector<ForestCandidate> candidates;
    candidates.reserve (static_cast<std::size_t> (proposal_count / 12));

    for (std::uint64_t proposal = 0; proposal < proposal_count; ++proposal) {
      const std::uint32_t identity =
        lattice_hash (static_cast<std::uint32_t> (proposal),
                      static_cast<std::uint32_t> (proposal >> 32),
                      seed);
      const meters_t x = hash_lane (identity, 0) * width;
      const meters_t z = hash_lane (identity, 1) * depth;
      const map::ForestCover cover = cover_at (readings, x, z);
      const proportion_t population = band (
        0.08f * map::forest_cover[one], 0.62f * map::forest_cover[one], cover);
      const float population_value = population.numerical_value_in (one);
      const float proposal_scale = std::lerp (
        forest_proposal_scale_min, forest_proposal_scale_max, population_value);
      if (cover < 0.06f * map::forest_cover[one] ||
          hash_lane (identity, 2) > population_value * proposal_scale)
        continue;
      candidates.push_back ({ .x = x.numerical_value_in (u::m),
                              .z = z.numerical_value_in (u::m),
                              .cover = cover.numerical_value_in (one),
                              .priority = hash_lane (identity, 3),
                              .identity = identity });
    }

    std::ranges::sort (
      candidates, [] (const ForestCandidate& a, const ForestCandidate& b) {
        return a.priority < b.priority ||
               (a.priority == b.priority && a.identity < b.identity);
      });
    const float width_m = width.numerical_value_in (u::m);
    const float depth_m = depth.numerical_value_in (u::m);
    const float exclusion =
      spacing.numerical_value_in (u::m) * forest_exclusion_ratio;
    const std::uint32_t bins_x = std::max (
      1U, static_cast<std::uint32_t> (std::ceil (width_m / exclusion)));
    const std::uint32_t bins_z = std::max (
      1U, static_cast<std::uint32_t> (std::ceil (depth_m / exclusion)));
    const float bin_x = width_m / static_cast<float> (bins_x);
    const float bin_z = depth_m / static_cast<float> (bins_z);
    const int reach_x = static_cast<int> (std::ceil (exclusion / bin_x));
    const int reach_z = static_cast<int> (std::ceil (exclusion / bin_z));
    std::vector<std::int32_t> heads (static_cast<std::size_t> (bins_x) * bins_z,
                                     -1);
    std::vector<std::int32_t> next;
    std::vector<ForestCandidate> accepted;
    next.reserve (candidates.size ());
    accepted.reserve (candidates.size ());
    const auto wrap = [] (int value, std::uint32_t period) {
      const int extent = static_cast<int> (period);
      value %= extent;
      return static_cast<std::uint32_t> (value < 0 ? value + extent : value);
    };
    const auto periodic_delta = [] (float value, float period) {
      return value - std::round (value / period) * period;
    };
    const float exclusion_squared = exclusion * exclusion;

    for (const ForestCandidate& candidate : candidates) {
      const std::uint32_t bx =
        std::min (static_cast<std::uint32_t> (candidate.x / bin_x), bins_x - 1);
      const std::uint32_t bz =
        std::min (static_cast<std::uint32_t> (candidate.z / bin_z), bins_z - 1);
      bool separated = true;
      for (int dz = -reach_z; separated && dz <= reach_z; ++dz)
        for (int dx = -reach_x; separated && dx <= reach_x; ++dx) {
          const std::uint32_t nx = wrap (static_cast<int> (bx) + dx, bins_x);
          const std::uint32_t nz = wrap (static_cast<int> (bz) + dz, bins_z);
          std::int32_t index =
            heads[static_cast<std::size_t> (nz) * bins_x + nx];
          while (index >= 0) {
            const ForestCandidate& neighbour =
              accepted[static_cast<std::size_t> (index)];
            const float delta_x =
              periodic_delta (candidate.x - neighbour.x, width_m);
            const float delta_z =
              periodic_delta (candidate.z - neighbour.z, depth_m);
            if (delta_x * delta_x + delta_z * delta_z < exclusion_squared) {
              separated = false;
              break;
            }
            index = next[static_cast<std::size_t> (index)];
          }
        }
      if (!separated)
        continue;
      const std::size_t bin = static_cast<std::size_t> (bz) * bins_x + bx;
      next.push_back (heads[bin]);
      heads[bin] = static_cast<std::int32_t> (accepted.size ());
      accepted.push_back (candidate);
    }

    plan.sites.reserve (accepted.size ());
    for (const ForestCandidate& candidate : accepted) {
      const meters_t x = candidate.x * u::m;
      const meters_t z = candidate.z * u::m;
      const map::ForestCover cover = candidate.cover * map::forest_cover[one];
      const terrain::SurfaceElevation elevation = elevation_at (surface, x, z);
      // A boreal stand: spruce IS the forest. The broadleaf construction
      // is a placeholder blob that has received none of the conifer's
      // assembly work, so it stays out of the world until it earns its
      // place.
      const ForestAge age = age_from_identity (candidate.identity);
      plan.sites.push_back ({ .position = forest_position (x, elevation, z),
                              .normal = normal_at (surface, x, z),
                              .cover = cover,
                              .moisture = moisture_at (readings, x, z),
                              .size = size_for_age (age, candidate.identity),
                              .seed = candidate.identity,
                              .form = ForestForm::conifer,
                              .age = age });
    }
    return plan;
  }

  void save_forest_plan (const ForestPlan& plan,
                         std::uint32_t seed,
                         const std::string& path) {
    const Vec3 period = extent_value (plan.period);
    const ForestPlanHeader header {
      .magic = forest_plan_magic,
      .version = forest_plan_version,
      .seed = seed,
      .period = { period[0], period[1], period[2] },
      .reserved = 0,
      .site_count = plan.sites.size (),
    };
    std::ofstream output (path, std::ios::binary | std::ios::trunc);
    if (!output)
      throw std::runtime_error ("could not create forest plan: " + path);
    write_record (output, header);
    for (const ForestSite& site : plan.sites) {
      const Vec3 point = position_value (site.position);
      const Vec3 normal = site.normal.numerical_value_in (one);
      const ForestSiteRecord record {
        .position = { point[0], point[1], point[2] },
        .normal = { normal[0], normal[1], normal[2] },
        .cover = site.cover.numerical_value_in (one),
        .moisture = site.moisture.numerical_value_in (one),
        .size = site.size.numerical_value_in (one),
        .seed = site.seed,
        .form = static_cast<std::uint32_t> (site.form),
        .age = static_cast<std::uint32_t> (site.age),
      };
      write_record (output, record);
    }
  }

  std::optional<ForestPlan>
  try_load_forest_plan (const std::string& path,
                        std::uint32_t seed,
                        const spatial_extent_t& expected_period) {
    std::ifstream input (path, std::ios::binary);
    ForestPlanHeader header {};
    input.read (reinterpret_cast<char*> (&header), sizeof (header));
    if (!input || header.magic != forest_plan_magic ||
        header.version != forest_plan_version || header.seed != seed ||
        !same_extent (header.period, expected_period) ||
        header.site_count > 10'000'000)
      return std::nullopt;
    const std::uintmax_t expected_bytes =
      sizeof (header) + header.site_count * sizeof (ForestSiteRecord);
    std::error_code error;
    if (std::filesystem::file_size (path, error) != expected_bytes || error)
      return std::nullopt;

    ForestPlan plan;
    plan.period = expected_period;
    plan.sites.reserve (static_cast<std::size_t> (header.site_count));
    for (std::uint64_t i = 0; i < header.site_count; ++i) {
      ForestSiteRecord record {};
      input.read (reinterpret_cast<char*> (&record), sizeof (record));
      if (!input ||
          record.form > static_cast<std::uint32_t> (ForestForm::conifer) ||
          record.age > static_cast<std::uint32_t> (ForestAge::ancient))
        return std::nullopt;
      plan.sites.push_back (
        { .position = position (
            Vec3 (record.position[0], record.position[1], record.position[2])),
          .normal =
            Vec3 (record.normal[0], record.normal[1], record.normal[2]) *
            terrain::terrain_normal[one],
          .cover = record.cover * map::forest_cover[one],
          .moisture = record.moisture * map::surface_moisture[one],
          .size = record.size * tree_size_factor[one],
          .seed = record.seed,
          .form = static_cast<ForestForm> (record.form),
          .age = static_cast<ForestAge> (record.age) });
    }
    return plan;
  }
}
