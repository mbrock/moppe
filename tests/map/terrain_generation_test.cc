#include <moppe/map/surface.hh>
#include <moppe/map/terrain_generation.hh>
#include <moppe/terrain/topology.hh>

#include <tests/test.hh>

#include <algorithm>
#include <bit>
#include <cstdint>
#include <vector>

namespace {
  bool maps_match (const moppe::map::SurfaceGeometry& left,
                   const moppe::map::SurfaceGeometry& right) {
    if (map::width (left) != map::width (right) ||
        map::height (left) != map::height (right))
      return false;
    for (int y = 0; y < map::height (left); ++y)
      for (int x = 0; x < map::width (left); ++x)
        if (std::bit_cast<std::uint32_t> (terrain::surface_elevation_value (
              map::elevation_at (left, x, y))) !=
            std::bit_cast<std::uint32_t> (terrain::surface_elevation_value (
              map::elevation_at (right, x, y))))
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
  map::SurfaceGeometry first = map::make_surface (33, 33, Vec3 (640, 650, 640));
  map::SurfaceGeometry second =
    map::make_surface (33, 33, Vec3 (640, 650, 640));

  generate (first, Seed { 77 }, evolution);
  generate (second, Seed { 77 }, evolution);

  MOPPE_CHECK (maps_match (first, second));
}

MOPPE_TEST (generated_terrain_wraps_continuously) {
  using namespace moppe;
  using namespace moppe::terrain;
  map::SurfaceGeometry surface =
    map::make_surface (32, 32, Vec3 (5000, 650, 3000));
  StreamPowerEvolution evolution;
  evolution.duration = 100000.0f * mp_units::astronomy::Julian_year;
  evolution.time_step = 50000.0f * mp_units::astronomy::Julian_year;

  generate (surface, Seed { 123 }, evolution);
  map::recompute_normals (surface);

  // The lattice is seamless: sampling one period apart reads the same
  // ground in both axes.
  const Vec3 period = map::world_period (surface);
  for (const float t : { 3.7f, 611.2f, 2499.9f }) {
    MOPPE_CHECK_NEAR (
      map::interpolated_height (surface, t, t * 0.4f),
      map::interpolated_height (surface, t + period[0], t * 0.4f),
      1e-3f);
    MOPPE_CHECK_NEAR (
      map::interpolated_height (surface, t, t * 0.4f),
      map::interpolated_height (surface, t, t * 0.4f + period[2]),
      1e-3f);
  }
}

MOPPE_TEST (terrain_evolution_reports_each_geological_step) {
  using namespace moppe;
  using namespace moppe::terrain;
  StreamPowerEvolution evolution;
  evolution.duration = 200000.0f * mp_units::astronomy::Julian_year;
  evolution.time_step = 50000.0f * mp_units::astronomy::Julian_year;
  map::SurfaceGeometry surface =
    map::make_surface (33, 33, Vec3 (640, 650, 640));
  std::vector<int> completed;
  std::vector<std::vector<float>> snapshots;

  const auto uplift =
    map::initialize_terrain (surface, Seed { 2468 }, 50.0f * u::m);
  map::evolve_terrain (
    surface,
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
  const auto& elevations = spatial::get<terrain::surface_elevation> (surface);
  final.reserve (elevations.size ());
  for (const auto elevation : elevations)
    final.push_back (surface_elevation_value (elevation));
  MOPPE_CHECK (snapshots.back () == final);
}

MOPPE_TEST (seeded_geology_separates_land_and_bathymetric_relief) {
  using namespace moppe;
  using namespace moppe::terrain;
  map::SurfaceGeometry surface =
    map::make_surface (33, 33, Vec3 (640, 650, 640));

  map::initialize_terrain (surface, Seed { 731 }, 50.0f * u::m);

  const float minimum = surface_elevation_value (map::min_elevation (surface));
  const float maximum = surface_elevation_value (map::max_elevation (surface));
  MOPPE_CHECK (minimum < 50.0f);
  MOPPE_CHECK (maximum > 50.0f);
  MOPPE_CHECK (maximum - 50.0f <= 20.0f + 1e-5f);
  MOPPE_CHECK (50.0f - minimum <= 240.0f + 1e-5f);
}
