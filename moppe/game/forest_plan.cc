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
    constexpr std::uint32_t forest_plan_version = 13;

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
      float moisture;
      float grove;
      bool pioneer; // admitted by birch's own open-ground population
      float priority;
      std::uint32_t identity;
    };

    // Where birch may dominate: a coherent field of patches from roughly
    // eighty to two hundred metres across, so birches arrive as groves
    // rather than as a sprinkle through the spruce.
    float birch_grove (meters_t x,
                       meters_t z,
                       meters_t width,
                       meters_t depth,
                       std::uint32_t seed) {
      const proportion_t along_x = x / width;
      const proportion_t along_z = z / depth;
      const auto laps = [] (meters_t extent, float patch_metres) {
        return std::max<std::uint32_t> (
          1,
          static_cast<std::uint32_t> (
            std::round (extent.numerical_value_in (u::m) / patch_metres)));
      };
      const float field =
        0.65f *
          periodic_noise (along_x, along_z, laps (width, 200.0f), seed ^ 0x5b1e)
            .numerical_value_in (one) +
        0.35f *
          periodic_noise (along_x, along_z, laps (depth, 80.0f), seed ^ 0x8c3d)
            .numerical_value_in (one);
      return smoothstep (0.54f, 0.68f, field);
    }

    float wetness (float moisture) {
      return smoothstep (0.30f, 0.70f, moisture);
    }

    // Birch is the pioneer: it holds the edges of the spruce, wet ground,
    // and open land inside its groves. A few birches stand anywhere.
    bool is_birch (const ForestCandidate& candidate) {
      if (candidate.pioneer)
        return true;
      const float edge = 1.0f - smoothstep (0.15f, 0.55f, candidate.cover);
      const float odds =
        0.03f +
        candidate.grove *
          (0.30f +
           0.70f * std::max (edge, 0.8f * wetness (candidate.moisture)));
      return hash_lane (candidate.identity, 7) < odds;
    }

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

    // A tree needs dry roots: the trunk's foot and a ring about its root
    // plate must all stand above the painted water. The ring keeps trunks
    // off a bank so steep that their crowns would lean out over the stream.
    bool roots_dry (const map::SurfaceGeometry& surface,
                    const terrain::WaterSheets& water,
                    meters_t x,
                    meters_t z) {
      constexpr float root_plate_m = 1.6f;
      constexpr float wet_depth_m = 0.02f;
      constexpr std::array<std::array<float, 2>, 7> probes { {
        { 0.0f, 0.0f },
        { 1.0f, 0.0f },
        { 0.5f, 0.866f },
        { -0.5f, 0.866f },
        { -1.0f, 0.0f },
        { -0.5f, -0.866f },
        { 0.5f, -0.866f },
      } };
      for (const auto& [dx, dz] : probes) {
        const meters_t px = x + root_plate_m * dx * u::m;
        const meters_t pz = z + root_plate_m * dz * u::m;
        const position_t where = sample_position (px, pz);
        const float ground = terrain::surface_elevation_value (
          spatial::sample<terrain::surface_elevation> (surface, where));
        const float level = terrain::surface_elevation_value (
          spatial::sample<terrain::surface_elevation> (water, where));
        if (level - ground > wet_depth_m)
          return false;
      }
      return true;
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
                                 const terrain::WaterSheets& water,
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
      float population_value = population.numerical_value_in (one);
      const float grove = birch_grove (x, z, width, depth, seed);
      // Inside a birch grove, wet open ground carries a sparse stand of its
      // own: spread-out birches over a meadow the spruce never reaches. Only
      // the spruce mosaic is waived; habitat, routes, and settlements still
      // keep their ground clear.
      float moisture = 0.0f;
      bool pioneer = false;
      if (grove > 0.0f) {
        const auto read = [&] (auto quantity) {
          return spatial::sample<quantity> (readings, sample_position (x, z))
            .numerical_value_in (one);
        };
        const float habitat =
          smoothstep (0.25f, 0.60f, read (map::tree_habitat));
        const float open = (1.0f - read (map::trail_influence)) *
                           (1.0f - read (map::home_base_influence));
        moisture = moisture_at (readings, x, z).numerical_value_in (one);
        const float pioneer_population = 0.22f * grove * wetness (moisture) *
                                         habitat *
                                         std::clamp (open, 0.0f, 1.0f);
        pioneer = pioneer_population > population_value;
        population_value = std::max (population_value, pioneer_population);
      }
      const float proposal_scale = std::lerp (
        forest_proposal_scale_min, forest_proposal_scale_max, population_value);
      if ((cover < 0.06f * map::forest_cover[one] &&
           population_value <= 0.0f) ||
          hash_lane (identity, 2) > population_value * proposal_scale ||
          !roots_dry (surface, water, x, z))
        continue;
      candidates.push_back ({ .x = x.numerical_value_in (u::m),
                              .z = z.numerical_value_in (u::m),
                              .cover = cover.numerical_value_in (one),
                              .moisture = moisture,
                              .grove = grove,
                              .pioneer = pioneer,
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
      // A boreal landscape: spruce is the forest, birch its pioneer.
      ForestCandidate site = candidate;
      if (site.grove > 0.0f && site.moisture == 0.0f)
        site.moisture = moisture_at (readings, x, z).numerical_value_in (one);
      const ForestAge age = age_from_identity (candidate.identity);
      plan.sites.push_back (
        { .position = forest_position (x, elevation, z),
          .normal = normal_at (surface, x, z),
          .cover = cover,
          .moisture = moisture_at (readings, x, z),
          .size = size_for_age (age, candidate.identity),
          .seed = candidate.identity,
          .form = is_birch (site) ? ForestForm::broadleaf : ForestForm::conifer,
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
