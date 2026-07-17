#include <moppe/terrain/program.hh>

#include <tests/test.hh>

#include <stdexcept>
#include <variant>

using namespace moppe::terrain;

MOPPE_TEST (geological_program_has_an_explicit_normalization_transform) {
  const TerrainProgram program =
    make_geological_program (123, GeologicalLayer::Mountains);

  MOPPE_CHECK (program.source.layer == GeologicalLayer::Mountains);
  MOPPE_CHECK (program.seed == Seed { 123 });
  MOPPE_CHECK (program.transforms.size () == 1);
  MOPPE_CHECK (
    std::holds_alternative<NormalizeHeights> (program.transforms[0]));
}

MOPPE_TEST (orogeny_program_uses_a_bathymetric_seed_and_evolution_stage) {
  const TerrainProgram program = make_orogeny_program (123);

  MOPPE_CHECK (program.source.mode == GeologicalSource::Mode::Orogeny);
  MOPPE_CHECK (program.transforms.size () == 1);
  MOPPE_CHECK (
    std::holds_alternative<OrogenyEvolution> (program.transforms.front ()));
  const auto& orogeny =
    std::get<OrogenyEvolution> (program.transforms.front ());
  MOPPE_CHECK_NEAR (
    meters_per_julian_year_value (orogeny.maximum_uplift_rate), 0.001f, 0.0f);
  MOPPE_CHECK_NEAR (
    orogeny.evolution.sea_level, program.source.sea_level, 0.0f);
  MOPPE_CHECK_NEAR (program.source.coastline, 0.4f, 0.0f);
  MOPPE_CHECK_NEAR (
    meters_value (program.source.initial_bathymetric_relief), 240.0f, 0.0f);
}

MOPPE_TEST (orogeny_profiles_calibrate_geological_duration) {
  const TerrainProgram fast =
    make_orogeny_program (123, TerrainGenerationProfile::Fast);
  const TerrainProgram play =
    make_orogeny_program (123, TerrainGenerationProfile::Play);
  const TerrainProgram research =
    make_orogeny_program (123, TerrainGenerationProfile::Research);
  const auto duration = [] (const TerrainProgram& program) {
    return julian_years_value (
      std::get<OrogenyEvolution> (program.transforms.front ())
        .evolution.duration);
  };

  MOPPE_CHECK_NEAR (duration (fast), 200000.0f, 0.0f);
  MOPPE_CHECK_NEAR (duration (play), 500000.0f, 0.0f);
  MOPPE_CHECK_NEAR (duration (research), 1000000.0f, 0.0f);
}

MOPPE_TEST (default_world_program_forms_trails_after_research_orogeny) {
  const TerrainProgram program = make_default_world_program (123);

  MOPPE_CHECK (program.source.mode == GeologicalSource::Mode::Orogeny);
  MOPPE_CHECK (program.transforms.size () == 2);
  const auto& orogeny =
    std::get<OrogenyEvolution> (program.transforms.front ());
  MOPPE_CHECK_NEAR (
    julian_years_value (orogeny.evolution.duration), 1000000.0f, 0.0f);
  MOPPE_CHECK (
    std::holds_alternative<TrailFormation> (program.transforms.back ()));
  MOPPE_CHECK (terrain_transform_id (program.transforms.back ()) == "trails");
}

MOPPE_TEST (profile_ids_are_stable_recipe_names) {
  MOPPE_CHECK (profile_id (TerrainGenerationProfile::Fast) == "fast");
  MOPPE_CHECK (profile_id (TerrainGenerationProfile::Play) == "play");
  MOPPE_CHECK (profile_id (TerrainGenerationProfile::Research) == "research");
}

MOPPE_TEST (transform_semantics_describe_execution_requirements) {
  MOPPE_CHECK (
    terrain_transform_semantics (PowerHeights { 1.2f }).spatial_scope ==
    SpatialScope::Pointwise);
  MOPPE_CHECK (
    terrain_transform_semantics (PowerHeights { 1.2f }).evaluation_order ==
    EvaluationOrder::Direct);

  MOPPE_CHECK (
    terrain_transform_semantics (NormalizeHeights {}).spatial_scope ==
    SpatialScope::Global);
  MOPPE_CHECK (
    terrain_transform_semantics (NormalizeHeights {}).evaluation_order ==
    EvaluationOrder::Reduction);

  MOPPE_CHECK (
    terrain_transform_semantics (ThermalErosion { iteration_count (1), 0.01f })
      .spatial_scope == SpatialScope::Neighborhood);
  MOPPE_CHECK (
    terrain_transform_semantics (ThermalErosion { iteration_count (1), 0.01f })
      .evaluation_order == EvaluationOrder::Iterative);

  MOPPE_CHECK (
    terrain_transform_semantics (AnalyticalErosion {}).spatial_scope ==
    SpatialScope::Global);
  MOPPE_CHECK (
    terrain_transform_semantics (AnalyticalErosion {}).evaluation_order ==
    EvaluationOrder::Iterative);
  MOPPE_CHECK (
    terrain_transform_semantics (OrogenyEvolution {}).spatial_scope ==
    SpatialScope::Global);
  MOPPE_CHECK (
    terrain_transform_semantics (OrogenyEvolution {}).evaluation_order ==
    EvaluationOrder::Iterative);
  MOPPE_CHECK (terrain_transform_semantics (TrailFormation {}).spatial_scope ==
               SpatialScope::Global);
  MOPPE_CHECK (
    terrain_transform_semantics (TrailFormation {}).evaluation_order ==
    EvaluationOrder::Iterative);
}

MOPPE_TEST (program_validation_rejects_invalid_transform_parameters) {
  TerrainProgram program = make_geological_program (123);
  program.transforms.emplace_back (AnalyticalErosion { .erodibility = 0.0f });
  bool threw = false;
  try {
    validate_program (program);
  } catch (const std::invalid_argument&) {
    threw = true;
  }
  MOPPE_CHECK (threw);

  program = make_orogeny_program (123);
  std::get<OrogenyEvolution> (program.transforms.front ())
    .evolution.reference_area =
    0.0f * mp_units::si::metre * mp_units::si::metre;
  threw = false;
  try {
    validate_program (program);
  } catch (const std::invalid_argument&) {
    threw = true;
  }
  MOPPE_CHECK (threw);

  program = make_geological_program (123);
  program.transforms.emplace_back (TrailFormation {
    .minimum_catchment_area = 10.0f * mp_units::si::metre * mp_units::si::metre,
    .maximum_catchment_area =
      9.0f * mp_units::si::metre * mp_units::si::metre });
  threw = false;
  try {
    validate_program (program);
  } catch (const std::invalid_argument&) {
    threw = true;
  }
  MOPPE_CHECK (threw);

  program = make_geological_program (123);
  program.transforms.emplace_back (
    TrailFormation { .maximum_grade = 0.1f * terrain_slope[mp_units::one],
                     .designed_grade = 0.2f * terrain_slope[mp_units::one] });
  threw = false;
  try {
    validate_program (program);
  } catch (const std::invalid_argument&) {
    threw = true;
  }
  MOPPE_CHECK (threw);

  program = make_geological_program (123);
  program.transforms.emplace_back (
    TrailFormation { .crossfall = 0.2f * terrain_slope[mp_units::one] });
  threw = false;
  try {
    validate_program (program);
  } catch (const std::invalid_argument&) {
    threw = true;
  }
  MOPPE_CHECK (threw);

  program = make_geological_program (123);
  program.transforms.emplace_back (TrailFormation {
    .highland_preference_height_above_sea = 300.0f * mp_units::si::metre,
    .alpine_avoidance_height_above_sea = 200.0f * mp_units::si::metre });
  threw = false;
  try {
    validate_program (program);
  } catch (const std::invalid_argument&) {
    threw = true;
  }
  MOPPE_CHECK (threw);
}
