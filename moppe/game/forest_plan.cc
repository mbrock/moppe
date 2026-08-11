#include <moppe/game/forest_plan.hh>

#include <moppe/gfx/signal.hh>

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
    constexpr std::uint32_t forest_plan_version = 6;

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
    const std::uint32_t columns =
      std::max (1U,
                static_cast<std::uint32_t> (
                  std::ceil ((width / spacing).numerical_value_in (one))));
    const std::uint32_t rows =
      std::max (1U,
                static_cast<std::uint32_t> (
                  std::ceil ((depth / spacing).numerical_value_in (one))));
    const meters_t cell_x = width / static_cast<float> (columns);
    const meters_t cell_z = depth / static_cast<float> (rows);
    plan.sites.reserve (static_cast<std::size_t> (columns) * rows / 4);

    for (std::uint32_t row = 0; row < rows; ++row)
      for (std::uint32_t column = 0; column < columns; ++column) {
        const std::uint32_t identity = lattice_hash (column, row, seed);
        const meters_t x = (static_cast<float> (column) + 0.02f +
                            0.96f * hash_lane (identity, 0)) *
                           cell_x;
        const meters_t z =
          (static_cast<float> (row) + 0.02f + 0.96f * hash_lane (identity, 1)) *
          cell_z;
        const map::ForestCover cover = cover_at (readings, x, z);
        const proportion_t population = band (0.08f * map::forest_cover[one],
                                              0.62f * map::forest_cover[one],
                                              cover);
        if (cover < 0.06f * map::forest_cover[one] ||
            hash_lane (identity, 2) >
              population.numerical_value_in (one) * 0.96f)
          continue;
        const terrain::SurfaceElevation elevation =
          elevation_at (surface, x, z);
        // A boreal stand: spruce IS the forest. The broadleaf construction
        // is a placeholder blob that has received none of the conifer's
        // assembly work, so it stays out of the world until it earns its
        // place.
        const ForestAge age = age_from_identity (identity);
        plan.sites.push_back ({ .position = forest_position (x, elevation, z),
                                .normal = normal_at (surface, x, z),
                                .cover = cover,
                                .moisture = moisture_at (readings, x, z),
                                .size = size_for_age (age, identity),
                                .seed = identity,
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
