#include <moppe/map/generate.hh>
#include <moppe/mov/vehicle.hh>
#include <moppe/terrain/pipeline.hh>
#include <moppe/terrain/topology.hh>

#include <tests/test.hh>

#include <bit>
#include <cstdint>

namespace {
  bool maps_match (const moppe::map::RandomHeightMap& left,
		   const moppe::map::RandomHeightMap& right) {
    if (left.width () != right.width ()
	|| left.height () != right.height ())
      return false;
    for (int y = 0; y < left.height (); ++y)
      for (int x = 0; x < left.width (); ++x)
	if (std::bit_cast<std::uint32_t> (left.get (x, y))
	    != std::bit_cast<std::uint32_t> (right.get (x, y)))
	  return false;
    return true;
  }
}

MOPPE_TEST (pipeline_replays_the_direct_generation_sequence) {
  using namespace moppe;
  using namespace moppe::terrain;
  const Vector3D size (1, 1, 1);
  map::RandomHeightMap legacy (65, 65, size, 123);
  legacy.randomize_geologically ();
  legacy.exponentiate (1.15f);
  legacy.erode_hydraulically (200);
  legacy.erode_thermally (1, 0.003f);

  TerrainPipeline pipeline = make_geological_pipeline (123);
  pipeline.stages.emplace_back (PowerHeights { 1.15f });
  pipeline.stages.emplace_back (HydraulicErosion { 200 });
  pipeline.stages.emplace_back (ThermalErosion { 1, 0.003f });
  map::RandomHeightMap replayed (65, 65, size, 999);
  replayed.run_pipeline (pipeline);

  MOPPE_CHECK (maps_match (legacy, replayed));
}

MOPPE_TEST (running_a_pipeline_twice_is_deterministic) {
  using namespace moppe;
  using namespace moppe::terrain;
  const Vector3D size (1, 1, 1);
  TerrainPipeline pipeline = make_geological_pipeline (77);
  pipeline.stages.emplace_back (HydraulicErosion { 100 });
  map::RandomHeightMap first (33, 33, size, 0);
  map::RandomHeightMap second (33, 33, size, 1);

  first.run_pipeline (pipeline);
  second.run_pipeline (pipeline);
  MOPPE_CHECK (maps_match (first, second));
}

MOPPE_TEST (periodic_pipeline_preserves_height_and_normal_seams) {
  using namespace moppe;
  using namespace moppe::terrain;
  const Vector3D size (5000, 650, 3000);
  map::RandomHeightMap map
    (65, 33, size, 123, Topology::Torus);
  TerrainPipeline pipeline = make_geological_pipeline (123);
  pipeline.stages.emplace_back (PowerHeights { 1.15f });
  pipeline.stages.emplace_back (HydraulicErosion { 400 });
  pipeline.stages.emplace_back (ThermalErosion { 2, 0.003f });
  map.run_pipeline (pipeline);
  map.recompute_normals ();

  MOPPE_CHECK (map.periodic ());
  MOPPE_CHECK_NEAR (map.size ().x, size.x, 1e-4f);
  MOPPE_CHECK_NEAR (map.size ().z, size.z, 1e-4f);
  for (int y = 0; y < map.height (); ++y) {
    MOPPE_CHECK (map.get (0, y) == map.get (map.width () - 1, y));
    const Vector3D a = map.normal (0, y);
    const Vector3D b = map.normal (map.width () - 1, y);
    MOPPE_CHECK_NEAR (a.x, b.x, 1e-6f);
    MOPPE_CHECK_NEAR (a.y, b.y, 1e-6f);
    MOPPE_CHECK_NEAR (a.z, b.z, 1e-6f);
  }
  for (int x = 0; x < map.width (); ++x) {
    MOPPE_CHECK (map.get (x, 0) == map.get (x, map.height () - 1));
    const Vector3D a = map.normal (x, 0);
    const Vector3D b = map.normal (x, map.height () - 1);
    MOPPE_CHECK_NEAR (a.x, b.x, 1e-6f);
    MOPPE_CHECK_NEAR (a.y, b.y, 1e-6f);
    MOPPE_CHECK_NEAR (a.z, b.z, 1e-6f);
  }

  constexpr float x = 1234.5f;
  constexpr float z = 987.25f;
  const float h = map.interpolated_height (x, z);
  MOPPE_CHECK_NEAR
    (map.interpolated_height (x + size.x, z - size.z), h, 1e-4f);
  MOPPE_CHECK (map.in_bounds (-100000.0f, 100000.0f));
}

MOPPE_TEST (vehicle_coordinates_remain_unwrapped_on_the_torus) {
  using namespace moppe;
  map::RandomHeightMap map
    (9, 9, Vector3D (100, 20, 100), 1,
     terrain::Topology::Torus);
  map.randomize_geologically ();
  map.recompute_normals ();
  mov::Vehicle vehicle
    (Vector3D (112.5f, 0, -7.5f), 0, map, 1000, 10000, 100);

  vehicle.update (0.0f);

  MOPPE_CHECK_NEAR (vehicle.position ().x, 112.5f, 1e-6f);
  MOPPE_CHECK_NEAR (vehicle.position ().z, -7.5f, 1e-6f);
  MOPPE_CHECK_NEAR
    (map.interpolated_height (vehicle.position ().x,
			      vehicle.position ().z),
     map.interpolated_height (12.5f, 92.5f), 1e-5f);
}
