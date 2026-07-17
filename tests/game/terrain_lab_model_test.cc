#include <moppe/game/terrain_lab_model.hh>
#include <moppe/map/generate.hh>
#include <moppe/map/terrain_evaluator.hh>
#include <moppe/terrain/program.hh>
#include <moppe/terrain/topology.hh>

#include <tests/test.hh>

#include <algorithm>
#include <bit>
#include <cstdint>
#include <vector>

namespace {
  bool model_maps_match (const moppe::map::RandomHeightMap& left,
                         const moppe::map::RandomHeightMap& right) {
    if (left.width () != right.width () || left.height () != right.height ())
      return false;
    for (int y = 0; y < left.height (); ++y)
      for (int x = 0; x < left.width (); ++x)
        if (std::bit_cast<std::uint32_t> (left.get (x, y)) !=
            std::bit_cast<std::uint32_t> (right.get (x, y)))
          return false;
    return true;
  }

  std::vector<float> heights_of (const moppe::map::RandomHeightMap& map) {
    const std::size_t count =
      static_cast<std::size_t> (map.width ()) * map.height ();
    return { map.raw_heights (), map.raw_heights () + count };
  }
}

MOPPE_TEST (terrain_lab_model_replays_a_program_without_a_renderer) {
  using namespace moppe;
  using namespace moppe::terrain;
  map::RandomHeightMap map (33, 33, Vec3 (640, 650, 640), 7, Topology::Torus);
  map.randomize_uniformly ();
  const std::vector<float> original = heights_of (map);
  TerrainProgram program = make_geological_program (42);
  program.transforms.emplace_back (PowerHeights { 1.15f });

  game::TerrainLabModel model;
  model.begin (map, program);

  MOPPE_CHECK (model.active ());
  MOPPE_CHECK (model.map_pristine ());
  MOPPE_CHECK (model.checkpoints ().empty ());
  MOPPE_CHECK (model.reports ().empty ());
  MOPPE_CHECK (model.progress ().total_stages == 2);

  // Entering the Lab observes the already-generated game map. The first edit
  // still needs a source materialization and checkpointed replay, even when
  // it changes the first transform.
  model.rerun_program_from (0);

  MOPPE_CHECK (!model.map_pristine ());
  MOPPE_CHECK (model.checkpoints ().size () == 2);
  MOPPE_CHECK (model.reports ().size () == 2);
  MOPPE_CHECK (!model.progress ().evaluating ());
  MOPPE_CHECK (model.progress ().completed_stages == 2);
  MOPPE_CHECK (model.progress ().total_stages == 2);

  auto& power = std::get<PowerHeights> (model.program ().transforms[1]);
  power.exponent = 1.6f;
  model.rerun_program_from (1);

  map::RandomHeightMap reference (
    33, 33, Vec3 (640, 650, 640), 8, Topology::Torus);
  map::TerrainEvaluator (reference).evaluate (model.program ());
  MOPPE_CHECK (model_maps_match (map, reference));

  model.leave ();
  MOPPE_CHECK (!model.active ());
  MOPPE_CHECK (heights_of (map) == original);
}
