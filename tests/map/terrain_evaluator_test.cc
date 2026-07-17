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
  bool maps_match (const moppe::map::RandomHeightMap& left,
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

  void evaluate (moppe::map::RandomHeightMap& target,
                 const moppe::terrain::TerrainProgram& program) {
    moppe::map::TerrainEvaluator (target).evaluate (program);
  }
}

MOPPE_TEST (heightmap_materializes_an_arbitrary_scalar_field) {
  using namespace moppe;
  using namespace moppe::terrain;
  map::RandomHeightMap map (3, 3, Vec3 (1, 1, 1), 0);

  map.materialize (coordinate_x () + 2.0f * coordinate_y ());

  MOPPE_CHECK_NEAR (map.get (0, 0), 0.0f, 1e-6f);
  MOPPE_CHECK_NEAR (map.get (1, 1), 1.5f, 1e-6f);
  MOPPE_CHECK_NEAR (map.get (2, 2), 3.0f, 1e-6f);
}

MOPPE_TEST (orogeny_evaluation_is_deterministic) {
  using namespace moppe;
  using namespace moppe::terrain;
  TerrainProgram program =
    make_orogeny_program (77, TerrainGenerationProfile::Fast);
  auto& orogeny = std::get<OrogenyEvolution> (program.transforms.front ());
  orogeny.evolution.duration = 100000.0f * mp_units::astronomy::Julian_year;
  orogeny.evolution.time_step = 50000.0f * mp_units::astronomy::Julian_year;
  map::RandomHeightMap first (33, 33, Vec3 (640, 650, 640), 0, Topology::Torus);
  map::RandomHeightMap second (
    33, 33, Vec3 (640, 650, 640), 1, Topology::Torus);

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
  map::RandomHeightMap map (17, 17, Vec3 (320, 650, 320), 0, Topology::Torus);
  map::TerrainEvaluator evaluator (map);

  evaluator.begin (program);
  evaluator.apply (program.transforms.front ());
  const map::TerrainCheckpoint checkpoint = evaluator.checkpoint ();
  MOPPE_CHECK (checkpoint.channel_tangents.size () == 16 * 16);
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
  TerrainProgram program = make_geological_program (77);
  program.transforms.emplace_back (PowerHeights { 1.15f });
  program.transforms.emplace_back (
    ThermalErosion { iteration_count (2), 0.003f });
  map::RandomHeightMap replayed (
    33, 33, Vec3 (100, 20, 100), 0, Topology::Torus);
  map::RandomHeightMap resumed (
    33, 33, Vec3 (100, 20, 100), 1, Topology::Torus);

  evaluate (replayed, program);
  map::TerrainEvaluator evaluator (resumed);
  evaluator.begin (program);
  evaluator.apply (program.transforms[0]);
  evaluator.apply (program.transforms[1]);
  const map::TerrainCheckpoint checkpoint = evaluator.checkpoint ();
  evaluator.apply (ThermalErosion { iteration_count (1), 0.02f });
  evaluator.restore (checkpoint);
  evaluator.apply (program.transforms[2]);

  MOPPE_CHECK (maps_match (replayed, resumed));
}

MOPPE_TEST (periodic_program_preserves_height_and_normal_seams) {
  using namespace moppe;
  using namespace moppe::terrain;
  map::RandomHeightMap map (33, 33, Vec3 (5000, 650, 3000), 0, Topology::Torus);
  TerrainProgram program = make_geological_program (123);
  program.transforms.emplace_back (PowerHeights { 1.15f });
  program.transforms.emplace_back (
    ThermalErosion { iteration_count (2), 0.003f });

  evaluate (map, program);
  map.recompute_normals ();

  for (int i = 0; i < map.width (); ++i) {
    MOPPE_CHECK (map.get (i, 0) == map.get (i, map.height () - 1));
    MOPPE_CHECK (map.get (0, i) == map.get (map.width () - 1, i));
    const Vec3 row_a = map.normal (i, 0);
    const Vec3 row_b = map.normal (i, map.height () - 1);
    const Vec3 column_a = map.normal (0, i);
    const Vec3 column_b = map.normal (map.width () - 1, i);
    for (int axis = 0; axis < 3; ++axis) {
      MOPPE_CHECK_NEAR (row_a[axis], row_b[axis], 1e-6f);
      MOPPE_CHECK_NEAR (column_a[axis], column_b[axis], 1e-6f);
    }
  }
}

MOPPE_TEST (orogeny_reports_each_geological_step) {
  using namespace moppe;
  using namespace moppe::terrain;
  TerrainProgram program = make_orogeny_program (2468);
  auto& orogeny = std::get<OrogenyEvolution> (program.transforms.front ());
  orogeny.evolution.duration = 200000.0f * mp_units::astronomy::Julian_year;
  orogeny.evolution.time_step = 50000.0f * mp_units::astronomy::Julian_year;
  map::RandomHeightMap map (33, 33, Vec3 (640, 650, 640), 0, Topology::Torus);
  std::vector<int> completed;

  map::TerrainEvaluator (map).evaluate (
    program,
    {},
    [&completed] (std::size_t, const TerrainTransform&, int done, int total) {
      MOPPE_CHECK (total == 4);
      completed.push_back (done);
    });

  MOPPE_CHECK (completed == std::vector<int> ({ 1, 2, 3, 4 }));
}

MOPPE_TEST (orogeny_seed_separates_land_and_bathymetric_relief) {
  using namespace moppe;
  using namespace moppe::terrain;
  const TerrainProgram program = make_orogeny_program (731);
  map::RandomHeightMap map (33, 33, Vec3 (640, 650, 640), 0, Topology::Torus);

  map::TerrainEvaluator (map).begin (program);

  const float land_relief =
    meters_value (program.source.initial_land_relief) / 650.0f;
  const float bathymetric_relief =
    meters_value (program.source.initial_bathymetric_relief) / 650.0f;
  MOPPE_CHECK (map.min_value () < program.source.sea_level);
  MOPPE_CHECK (map.max_value () > program.source.sea_level);
  MOPPE_CHECK (map.max_value () - program.source.sea_level <=
               land_relief + 1e-6f);
  MOPPE_CHECK (program.source.sea_level - map.min_value () <=
               bathymetric_relief + 1e-6f);
}
