#include <moppe/map/generate.hh>
#include <moppe/terrain/pipeline.hh>

#include <tests/test.hh>

#include <bit>
#include <cstdint>

namespace {
  std::uint64_t map_hash (const moppe::map::RandomHeightMap& map) {
    std::uint64_t hash = 1469598103934665603ull;
    for (int y = 0; y < map.height (); ++y)
      for (int x = 0; x < map.width (); ++x) {
	const std::uint32_t bits = std::bit_cast<std::uint32_t>
	  (map.get (x, y));
	for (int byte = 0; byte < 4; ++byte) {
	  hash ^= (bits >> (byte * 8)) & 0xff;
	  hash *= 1099511628211ull;
	}
      }
    return hash;
  }

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

MOPPE_TEST (pipeline_replays_the_legacy_generation_sequence) {
  using namespace moppe;
  using namespace moppe::terrain;
  const Vector3D size (1, 1, 1);
  map::RandomHeightMap legacy (65, 65, size, 123);
  legacy.randomize_geologically ();
  MOPPE_CHECK (map_hash (legacy) == 0xdd6de93ca7d0e2e9ull);
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
