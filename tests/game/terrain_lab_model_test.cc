#include <moppe/game/terrain_lab_model.hh>
#include <moppe/map/surface.hh>
#include <moppe/map/terrain_evaluator.hh>
#include <moppe/terrain/program.hh>
#include <moppe/terrain/topology.hh>

#include <tests/test.hh>

#include <algorithm>
#include <bit>
#include <cstdint>
#include <vector>

namespace {
  bool model_maps_match (const moppe::map::Surface& left,
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

  std::vector<float> heights_of (const moppe::map::Surface& map) {
    std::vector<float> values;
    const auto& elevations =
      moppe::spatial::get<moppe::terrain::surface_elevation> (map.geometry ());
    values.reserve (elevations.size ());
    for (const auto elevation : elevations)
      values.push_back (moppe::terrain::surface_elevation_value (elevation));
    return values;
  }
}

MOPPE_TEST (terrain_lab_model_replays_a_program_without_a_renderer) {
  using namespace moppe;
  using namespace moppe::terrain;
  map::Surface map (33, 33, Vec3 (640, 650, 640));
  for (int y = 0; y < map.height (); ++y)
    for (int x = 0; x < map.width (); ++x)
      map.set_elevation (
        x,
        y,
        moppe::terrain::surface_elevation_point (
          (static_cast<float> (x + y) / 64.0f) * 650.0f * mp_units::si::metre));
  const std::vector<float> original = heights_of (map);
  TerrainProgram program =
    make_orogeny_program (42, TerrainGenerationProfile::Fast);
  auto& orogeny = std::get<OrogenyEvolution> (program.transforms.front ());
  orogeny.evolution.duration = 100000.0f * mp_units::astronomy::Julian_year;
  orogeny.evolution.time_step = 50000.0f * mp_units::astronomy::Julian_year;

  game::TerrainLabModel model;
  model.begin (map, program);

  MOPPE_CHECK (model.active ());
  MOPPE_CHECK (model.map_pristine ());
  MOPPE_CHECK (model.checkpoints ().empty ());
  MOPPE_CHECK (model.reports ().empty ());
  MOPPE_CHECK (model.progress ().total_stages == 1);

  // Entering the Lab observes the already-generated game map. The first edit
  // still needs a source materialization and checkpointed replay, even when
  // it changes the first transform.
  model.rerun_program_from (0);

  MOPPE_CHECK (!model.map_pristine ());
  MOPPE_CHECK (model.checkpoints ().size () == 1);
  MOPPE_CHECK (model.reports ().size () == 1);
  MOPPE_CHECK (!model.progress ().evaluating ());
  MOPPE_CHECK (model.progress ().completed_stages == 1);
  MOPPE_CHECK (model.progress ().total_stages == 1);

  auto& edited =
    std::get<OrogenyEvolution> (model.program ().transforms.front ());
  edited.maximum_uplift_rate =
    0.0012f * mp_units::si::metre / mp_units::astronomy::Julian_year;
  model.rerun_program_from (0);

  map::Surface reference (33, 33, Vec3 (640, 650, 640));
  map::TerrainEvaluator (reference).evaluate (model.program ());
  MOPPE_CHECK (model_maps_match (map, reference));

  model.leave ();
  MOPPE_CHECK (!model.active ());
  MOPPE_CHECK (heights_of (map) == original);
}
