#include <moppe/map/surface.hh>
#include <moppe/terrain/domain.hh>

#include <tests/test.hh>

#include <algorithm>
#include <bit>
#include <cstdint>
#include <vector>

namespace {
  bool maps_match (const moppe::map::SurfaceGeometry& left,
                   const moppe::map::SurfaceGeometry& right) {
    if (static_cast<int> (left.domain ().width ()) !=
          static_cast<int> (right.domain ().width ()) ||
        static_cast<int> (left.domain ().height ()) !=
          static_cast<int> (right.domain ().height ()))
      return false;
    for (int y = 0; y < static_cast<int> (left.domain ().height ()); ++y)
      for (int x = 0; x < static_cast<int> (left.domain ().width ()); ++x)
        if (std::bit_cast<std::uint32_t> (terrain::surface_elevation_value (
              spatial::get<terrain::surface_elevation> (
                left[terrain::TerrainIndex {
                  static_cast<std::size_t> (x),
                  static_cast<std::size_t> (y) }]))) !=
            std::bit_cast<std::uint32_t> (terrain::surface_elevation_value (
              spatial::get<terrain::surface_elevation> (
                right[terrain::TerrainIndex {
                  static_cast<std::size_t> (x),
                  static_cast<std::size_t> (y) }]))))
          return false;
    return true;
  }

  void generate (moppe::map::SurfaceGeometry& target,
                 moppe::terrain::Seed seed,
                 moppe::terrain::StreamPowerEvolution evolution) {
    const auto uplift = moppe::map::initialize_terrain (
      target, seed, 50.0f * mp_units::si::metre);
    moppe::map::evolve_terrain (target, uplift, evolution);
  }
}

MOPPE_TEST (terrain_generation_is_deterministic) {
  using namespace moppe;
  using namespace moppe::terrain;
  StreamPowerEvolution evolution;
  evolution.duration = 100000.0f * mp_units::astronomy::Julian_year;
  evolution.time_step = 50000.0f * mp_units::astronomy::Julian_year;
  map::SurfaceGeometry first = map::SurfaceGeometry (terrain::TerrainDomain (
    33, 33, spatial_extent_in_metres (Vec3 (640, 0, 640))));
  map::SurfaceGeometry second = map::SurfaceGeometry (terrain::TerrainDomain (
    33, 33, spatial_extent_in_metres (Vec3 (640, 0, 640))));

  generate (first, Seed { 77 }, evolution);
  generate (second, Seed { 77 }, evolution);

  MOPPE_CHECK (maps_match (first, second));
}

MOPPE_TEST (generated_terrain_wraps_continuously) {
  using namespace moppe;
  using namespace moppe::terrain;
  map::SurfaceGeometry surface = map::SurfaceGeometry (terrain::TerrainDomain (
    32, 32, spatial_extent_in_metres (Vec3 (5000, 0, 3000))));
  StreamPowerEvolution evolution;
  evolution.duration = 100000.0f * mp_units::astronomy::Julian_year;
  evolution.time_step = 50000.0f * mp_units::astronomy::Julian_year;

  generate (surface, Seed { 123 }, evolution);
  map::rebuild_geometry (surface);

  // The lattice is seamless: sampling one period apart reads the same
  // ground in both axes.
  const Vec3 period =
    Vec3 ((surface.domain ().period_x ()).numerical_value_in (moppe::u::m),
          0.0f,
          (surface.domain ().period_z ()).numerical_value_in (moppe::u::m));
  for (const float t : { 3.7f, 611.2f, 2499.9f }) {
    MOPPE_CHECK_NEAR (
      terrain::surface_elevation_value (
        spatial::sample<terrain::surface_elevation> (
          surface, moppe::position (Vec3 (t, 0.0f, t * 0.4f)))),
      terrain::surface_elevation_value (
        spatial::sample<terrain::surface_elevation> (
          surface, moppe::position (Vec3 (t + period[0], 0.0f, t * 0.4f)))),
      1e-3f);
    MOPPE_CHECK_NEAR (
      terrain::surface_elevation_value (
        spatial::sample<terrain::surface_elevation> (
          surface, moppe::position (Vec3 (t, 0.0f, t * 0.4f)))),
      terrain::surface_elevation_value (
        spatial::sample<terrain::surface_elevation> (
          surface, moppe::position (Vec3 (t, 0.0f, t * 0.4f + period[2])))),
      1e-3f);
  }
}

MOPPE_TEST (terrain_evolution_reports_each_geological_step) {
  using namespace moppe;
  using namespace moppe::terrain;
  StreamPowerEvolution evolution;
  evolution.duration = 200000.0f * mp_units::astronomy::Julian_year;
  evolution.time_step = 50000.0f * mp_units::astronomy::Julian_year;
  map::SurfaceGeometry surface = map::SurfaceGeometry (terrain::TerrainDomain (
    33, 33, spatial_extent_in_metres (Vec3 (640, 0, 640))));
  std::vector<terrain::IterationCount> completed;
  std::vector<std::vector<terrain::SurfaceElevation>> snapshots;

  const auto uplift =
    map::initialize_terrain (surface, Seed { 2468 }, 50.0f * u::m);
  map::evolve_terrain (
    surface,
    uplift,
    evolution,
    [&] (terrain::IterationCount done,
         terrain::IterationCount total,
         std::span<const terrain::SurfaceElevation> heights) {
      MOPPE_CHECK (total == iteration_count (4));
      completed.push_back (done);
      snapshots.emplace_back (heights.begin (), heights.end ());
    });

  MOPPE_CHECK (completed ==
               std::vector<terrain::IterationCount> ({ iteration_count (1),
                                                       iteration_count (2),
                                                       iteration_count (3),
                                                       iteration_count (4) }));
  MOPPE_CHECK (snapshots.size () == 4);
  MOPPE_CHECK (snapshots.front () != snapshots.back ());
  MOPPE_CHECK (snapshots.back () ==
               spatial::get<terrain::surface_elevation> (surface));
}

MOPPE_TEST (seeded_geology_separates_land_and_bathymetric_relief) {
  using namespace moppe;
  using namespace moppe::terrain;
  map::SurfaceGeometry surface = map::SurfaceGeometry (terrain::TerrainDomain (
    33, 33, spatial_extent_in_metres (Vec3 (640, 0, 640))));

  map::initialize_terrain (surface, Seed { 731 }, 50.0f * u::m);

  const auto& elevations = spatial::get<terrain::surface_elevation> (surface);
  const auto [minimum, maximum] =
    std::ranges::minmax (elevations, {}, surface_elevation_value);
  const float minimum_m = surface_elevation_value (minimum);
  const float maximum_m = surface_elevation_value (maximum);
  MOPPE_CHECK (minimum_m < 50.0f);
  MOPPE_CHECK (maximum_m > 50.0f);
  MOPPE_CHECK (maximum_m - 50.0f <= 20.0f + 1e-5f);
  MOPPE_CHECK (50.0f - minimum_m <= 240.0f + 1e-5f);
}
