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
  bool maps_match (const moppe::map::Surface& left,
                   const moppe::map::Surface& right) {
    if (left.width () != right.width () || left.height () != right.height ())
      return false;
    for (int y = 0; y < left.height (); ++y)
      for (int x = 0; x < left.width (); ++x)
        if (std::bit_cast<std::uint32_t> (left.relative_elevation_at (x, y)) !=
            std::bit_cast<std::uint32_t> (right.relative_elevation_at (x, y)))
          return false;
    return true;
  }

  void evaluate (moppe::map::Surface& target,
                 const moppe::terrain::TerrainProgram& program) {
    moppe::map::TerrainEvaluator (target).evaluate (program);
  }
}

MOPPE_TEST (surface_materializes_an_arbitrary_scalar_field) {
  using namespace moppe;
  using namespace moppe::terrain;
  map::Surface map (4, 4, Vec3 (1, 1, 1));

  map.materialize (coordinate_x () + 2.0f * coordinate_y ());

  MOPPE_CHECK_NEAR (map.relative_elevation_at (0, 0), 0.0f, 1e-6f);
  MOPPE_CHECK_NEAR (map.relative_elevation_at (2, 2), 1.5f, 1e-6f);
  MOPPE_CHECK_NEAR (map.relative_elevation_at (3, 3), 2.25f, 1e-6f);
}

MOPPE_TEST (orogeny_evaluation_is_deterministic) {
  using namespace moppe;
  using namespace moppe::terrain;
  TerrainProgram program =
    make_orogeny_program (77, TerrainGenerationProfile::Fast);
  auto& orogeny = std::get<OrogenyEvolution> (program.transforms.front ());
  orogeny.evolution.duration = 100000.0f * mp_units::astronomy::Julian_year;
  orogeny.evolution.time_step = 50000.0f * mp_units::astronomy::Julian_year;
  map::Surface first (33, 33, Vec3 (640, 650, 640));
  map::Surface second (33, 33, Vec3 (640, 650, 640));

  evaluate (first, program);
  evaluate (second, program);

  MOPPE_CHECK (maps_match (first, second));
}

MOPPE_TEST (orogeny_channel_memory_survives_a_checkpoint) {
  using namespace moppe;
  using namespace moppe::terrain;
  TerrainProgram program =
    make_orogeny_program (91, TerrainGenerationProfile::Fast);
  auto& orogeny = std::get<OrogenyEvolution> (program.transforms.front ());
  orogeny.evolution.duration = 100000.0f * mp_units::astronomy::Julian_year;
  orogeny.evolution.time_step = 50000.0f * mp_units::astronomy::Julian_year;
  map::Surface map (17, 17, Vec3 (320, 650, 320));
  map::TerrainEvaluator evaluator (map);

  evaluator.begin (program);
  evaluator.apply (program.transforms.front ());
  const map::TerrainCheckpoint checkpoint = evaluator.checkpoint ();
  MOPPE_CHECK (checkpoint.channel_tangents.size () == 17 * 17);
  MOPPE_CHECK (std::ranges::any_of (
    checkpoint.channel_tangents, [] (ChannelTangent tangent) {
      return length2 (tangent.numerical_value_in (mp_units::one)) > 0.0f;
    }));

  evaluator.begin (program);
  MOPPE_CHECK (evaluator.channel_tangents ().empty ());
  evaluator.restore (checkpoint);
  MOPPE_CHECK (std::ranges::equal (evaluator.channel_tangents (),
                                   checkpoint.channel_tangents));
}

MOPPE_TEST (checkpoint_resume_matches_complete_replay) {
  using namespace moppe;
  using namespace moppe::terrain;
  TerrainProgram program =
    make_orogeny_program (77, TerrainGenerationProfile::Fast);
  auto& orogeny = std::get<OrogenyEvolution> (program.transforms.front ());
  orogeny.evolution.duration = 100000.0f * mp_units::astronomy::Julian_year;
  orogeny.evolution.time_step = 50000.0f * mp_units::astronomy::Julian_year;
  map::Surface replayed (33, 33, Vec3 (640, 650, 640));
  map::Surface resumed (33, 33, Vec3 (640, 650, 640));

  evaluate (replayed, program);
  map::TerrainEvaluator evaluator (resumed);
  evaluator.begin (program);
  const map::TerrainCheckpoint checkpoint = evaluator.checkpoint ();
  evaluator.apply (program.transforms.front ());
  evaluator.restore (checkpoint);
  evaluator.apply (program.transforms.front ());

  MOPPE_CHECK (maps_match (replayed, resumed));
}

MOPPE_TEST (periodic_program_wraps_continuously) {
  using namespace moppe;
  using namespace moppe::terrain;
  map::Surface map (32, 32, Vec3 (5000, 650, 3000));
  TerrainProgram program =
    make_orogeny_program (123, TerrainGenerationProfile::Fast);
  auto& orogeny = std::get<OrogenyEvolution> (program.transforms.front ());
  orogeny.evolution.duration = 100000.0f * mp_units::astronomy::Julian_year;
  orogeny.evolution.time_step = 50000.0f * mp_units::astronomy::Julian_year;

  evaluate (map, program);
  map.recompute_normals ();

  // The lattice is seamless: sampling one period apart reads the same
  // ground in both axes.
  const Vec3 period = map.size ();
  for (const float t : { 3.7f, 611.2f, 2499.9f }) {
    MOPPE_CHECK_NEAR (map.interpolated_height (t, t * 0.4f),
                      map.interpolated_height (t + period[0], t * 0.4f),
                      1e-3f);
    MOPPE_CHECK_NEAR (map.interpolated_height (t, t * 0.4f),
                      map.interpolated_height (t, t * 0.4f + period[2]),
                      1e-3f);
  }
}

MOPPE_TEST (orogeny_reports_each_geological_step) {
  using namespace moppe;
  using namespace moppe::terrain;
  TerrainProgram program = make_orogeny_program (2468);
  auto& orogeny = std::get<OrogenyEvolution> (program.transforms.front ());
  orogeny.evolution.duration = 200000.0f * mp_units::astronomy::Julian_year;
  orogeny.evolution.time_step = 50000.0f * mp_units::astronomy::Julian_year;
  map::Surface map (33, 33, Vec3 (640, 650, 640));
  std::vector<int> completed;
  std::vector<std::vector<float>> snapshots;

  map::TerrainEvaluator (map).evaluate (
    program,
    {},
    [&] (std::size_t, const TerrainTransform&, int done, int total) {
      MOPPE_CHECK (total == 4);
      completed.push_back (done);
      std::vector<float> snapshot;
      snapshot.reserve (map.relative_elevations ().size ());
      for (const auto elevation : map.relative_elevations ())
        snapshot.push_back (elevation.numerical_value_in (one));
      snapshots.push_back (std::move (snapshot));
    });

  MOPPE_CHECK (completed == std::vector<int> ({ 1, 2, 3, 4 }));
  MOPPE_CHECK (snapshots.size () == 4);
  MOPPE_CHECK (snapshots.front () != snapshots.back ());
  std::vector<float> final;
  final.reserve (map.relative_elevations ().size ());
  for (const auto elevation : map.relative_elevations ())
    final.push_back (elevation.numerical_value_in (one));
  MOPPE_CHECK (snapshots.back () == final);
}

MOPPE_TEST (orogeny_seed_separates_land_and_bathymetric_relief) {
  using namespace moppe;
  using namespace moppe::terrain;
  const TerrainProgram program = make_orogeny_program (731);
  map::Surface map (33, 33, Vec3 (640, 650, 640));

  map::TerrainEvaluator (map).begin (program);

  const float land_relief =
    meters_value (program.source.initial_land_relief) / 650.0f;
  const float bathymetric_relief =
    meters_value (program.source.initial_bathymetric_relief) / 650.0f;
  MOPPE_CHECK (map.min_relative_elevation () < program.source.sea_level);
  MOPPE_CHECK (map.max_relative_elevation () > program.source.sea_level);
  MOPPE_CHECK (map.max_relative_elevation () - program.source.sea_level <=
               land_relief + 1e-6f);
  MOPPE_CHECK (program.source.sea_level - map.min_relative_elevation () <=
               bathymetric_relief + 1e-6f);
}
