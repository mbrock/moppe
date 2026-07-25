#include <moppe/map/surface.hh>
#include <moppe/map/terrain_generation.hh>
#include <moppe/terrain/topology.hh>

#include <tests/test.hh>

#include <algorithm>
#include <bit>
#include <cstdint>
#include <vector>

namespace {
  bool maps_match (const moppe::map::Surface& left,
                   const moppe::map::Surface& right) {
    if (left.width () != right.width () || left.height () != right.height ())
      return false;
    for (int y = 0; y < left.height (); ++y)
      for (int x = 0; x < left.width (); ++x)
        if (std::bit_cast<std::uint32_t> (
              terrain::surface_elevation_value (left.elevation_at (x, y))) !=
            std::bit_cast<std::uint32_t> (
              terrain::surface_elevation_value (right.elevation_at (x, y))))
          return false;
    return true;
  }

  void generate (moppe::map::Surface& target,
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
  map::Surface first (33, 33, Vec3 (640, 650, 640));
  map::Surface second (33, 33, Vec3 (640, 650, 640));

  generate (first, Seed { 77 }, evolution);
  generate (second, Seed { 77 }, evolution);

  MOPPE_CHECK (maps_match (first, second));
}

MOPPE_TEST (generated_terrain_wraps_continuously) {
  using namespace moppe;
  using namespace moppe::terrain;
  map::Surface map (32, 32, Vec3 (5000, 650, 3000));
  StreamPowerEvolution evolution;
  evolution.duration = 100000.0f * mp_units::astronomy::Julian_year;
  evolution.time_step = 50000.0f * mp_units::astronomy::Julian_year;

  generate (map, Seed { 123 }, evolution);
  map.recompute_normals ();

  // The lattice is seamless: sampling one period apart reads the same
  // ground in both axes.
  const Vec3 period = map.world_extent ();
  for (const float t : { 3.7f, 611.2f, 2499.9f }) {
    MOPPE_CHECK_NEAR (map.interpolated_height (t, t * 0.4f),
                      map.interpolated_height (t + period[0], t * 0.4f),
                      1e-3f);
    MOPPE_CHECK_NEAR (map.interpolated_height (t, t * 0.4f),
                      map.interpolated_height (t, t * 0.4f + period[2]),
                      1e-3f);
  }
}

MOPPE_TEST (terrain_evolution_reports_each_geological_step) {
  using namespace moppe;
  using namespace moppe::terrain;
  StreamPowerEvolution evolution;
  evolution.duration = 200000.0f * mp_units::astronomy::Julian_year;
  evolution.time_step = 50000.0f * mp_units::astronomy::Julian_year;
  map::Surface map (33, 33, Vec3 (640, 650, 640));
  std::vector<int> completed;
  std::vector<std::vector<float>> snapshots;

  const auto uplift =
    map::initialize_terrain (map, Seed { 2468 }, 50.0f * u::m);
  map::evolve_terrain (
    map,
    uplift,
    evolution,
    nullptr,
    [&] (int done, int total, std::span<const float> heights) {
      MOPPE_CHECK (total == 4);
      completed.push_back (done);
      snapshots.emplace_back (heights.begin (), heights.end ());
    });

  MOPPE_CHECK (completed == std::vector<int> ({ 1, 2, 3, 4 }));
  MOPPE_CHECK (snapshots.size () == 4);
  MOPPE_CHECK (snapshots.front () != snapshots.back ());
  std::vector<float> final;
  const auto& elevations =
    spatial::get<terrain::surface_elevation> (map.geometry ());
  final.reserve (elevations.size ());
  for (const auto elevation : elevations)
    final.push_back (surface_elevation_value (elevation));
  MOPPE_CHECK (snapshots.back () == final);
}

MOPPE_TEST (seeded_geology_separates_land_and_bathymetric_relief) {
  using namespace moppe;
  using namespace moppe::terrain;
  map::Surface map (33, 33, Vec3 (640, 650, 640));

  map::initialize_terrain (map, Seed { 731 }, 50.0f * u::m);

  const float minimum = surface_elevation_value (map.min_elevation ());
  const float maximum = surface_elevation_value (map.max_elevation ());
  MOPPE_CHECK (minimum < 50.0f);
  MOPPE_CHECK (maximum > 50.0f);
  MOPPE_CHECK (maximum - 50.0f <= 20.0f + 1e-5f);
  MOPPE_CHECK (50.0f - minimum <= 240.0f + 1e-5f);
}
