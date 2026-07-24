#include <moppe/terrain/editor.hh>
#include <moppe/terrain/program.hh>

#include <tests/test.hh>

#include <array>
#include <stdexcept>
#include <string>
#include <variant>

using namespace moppe::terrain;

MOPPE_TEST (orogeny_program_uses_a_bathymetric_seed_and_evolution_stage) {
  const TerrainProgram program = make_orogeny_program (123);

  MOPPE_CHECK (program.seed == Seed { 123 });
  MOPPE_CHECK (program.transforms.size () == 1);
  MOPPE_CHECK (
    std::holds_alternative<OrogenyEvolution> (program.transforms.front ()));
  const auto& orogeny =
    std::get<OrogenyEvolution> (program.transforms.front ());
  MOPPE_CHECK_NEAR (
    moppe::meters_per_julian_year_value (orogeny.maximum_uplift_rate),
    0.001f,
    0.0f);
  MOPPE_CHECK_NEAR (
    orogeny.evolution.sea_level, program.source.sea_level, 0.0f);
  MOPPE_CHECK_NEAR (program.source.coastline, 0.4f, 0.0f);
  MOPPE_CHECK_NEAR (
    moppe::meters_value (program.source.initial_bathymetric_relief),
    240.0f,
    0.0f);
}

MOPPE_TEST (orogeny_profiles_calibrate_geological_duration) {
  const TerrainProgram fast =
    make_orogeny_program (123, TerrainGenerationProfile::Fast);
  const TerrainProgram play =
    make_orogeny_program (123, TerrainGenerationProfile::Play);
  const TerrainProgram research =
    make_orogeny_program (123, TerrainGenerationProfile::Research);
  const auto duration = [] (const TerrainProgram& program) {
    return moppe::julian_years_value (
      std::get<OrogenyEvolution> (program.transforms.front ())
        .evolution.duration);
  };

  MOPPE_CHECK_NEAR (duration (fast), 750000.0f, 0.0f);
  MOPPE_CHECK_NEAR (duration (play), 1500000.0f, 0.0f);
  MOPPE_CHECK_NEAR (duration (research), 2000000.0f, 0.0f);
}

MOPPE_TEST (default_world_program_forms_trails_after_research_orogeny) {
  const TerrainProgram program = make_default_world_program (123);

  MOPPE_CHECK (program.transforms.size () == 2);
  const auto& orogeny =
    std::get<OrogenyEvolution> (program.transforms.front ());
  MOPPE_CHECK_NEAR (
    moppe::julian_years_value (orogeny.evolution.duration), 2000000.0f, 0.0f);
  MOPPE_CHECK (
    std::holds_alternative<TrailFormation> (program.transforms.back ()));
  MOPPE_CHECK (terrain_transform_id (program.transforms.back ()) == "trails");
}

MOPPE_TEST (profile_ids_are_stable_recipe_names) {
  MOPPE_CHECK (profile_id (TerrainGenerationProfile::Fast) == "fast");
  MOPPE_CHECK (profile_id (TerrainGenerationProfile::Play) == "play");
  MOPPE_CHECK (profile_id (TerrainGenerationProfile::Research) == "research");
}

MOPPE_TEST (canonical_stage_semantics_describe_execution_requirements) {
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

MOPPE_TEST (canonical_stages_own_their_editable_descriptions) {
  const std::array transforms {
    TerrainTransform { OrogenyEvolution {} },
    TerrainTransform { TrailFormation {} },
  };
  const std::array<std::size_t, 2> property_counts = { 8, 14 };
  for (std::size_t i = 0; i < transforms.size (); ++i) {
    const TransformDescription description =
      terrain_transform_description (transforms[i]);
    MOPPE_CHECK (!description.id.empty ());
    MOPPE_CHECK (!description.title.empty ());
    MOPPE_CHECK (terrain_transform_property_count (transforms[i]) ==
                 property_counts[i]);
  }

  const TransformProperty trail_property =
    terrain_transform_property (TerrainTransform { TrailFormation {} }, 2);
  MOPPE_CHECK (trail_property.label == "PATH WIDTH (M)");
  MOPPE_CHECK (trail_property.domain == ParameterDomain::Continuous);
}

MOPPE_TEST (terrain_program_editor_delegates_to_typed_stage_editors) {
  TerrainProgram program =
    make_world_program (123, TerrainGenerationProfile::Fast);
  TerrainProgramEditor editor (program);
  const int original_continent_waves =
    program.source.recipe.continent.noise.cycles;

  MOPPE_CHECK (editor.source ().property_count () == 9);
  MOPPE_CHECK (editor.set_source_normalized_property (0, 0.5f));
  MOPPE_CHECK_NEAR (program.source.recipe.warp.amplitude, 0.3f, 1e-6f);
  MOPPE_CHECK (editor.adjust_source_natural_property (1, 1));
  MOPPE_CHECK (program.source.recipe.continent.noise.cycles ==
               original_continent_waves + 1);
  MOPPE_CHECK (editor.set_transform_normalized_property (0, 7, 0.75f));
  const auto& evolution = std::get<OrogenyEvolution> (program.transforms[0]);
  MOPPE_CHECK_NEAR (evolution.evolution.sea_level, 0.225f, 1e-6f);
  MOPPE_CHECK_NEAR (program.source.sea_level, 0.225f, 1e-6f);

  const TerrainProgram& read_only_program = program;
  const TerrainProgramEditor read_only (read_only_program);
  MOPPE_CHECK (read_only.transform (0).property (7).label == "SEA LEVEL");
}

MOPPE_TEST (program_validation_rejects_invalid_canonical_stages) {
  const auto validation_error = [] (TerrainProgram program) {
    try {
      validate_program (program);
    } catch (const std::invalid_argument& error) {
      return std::string (error.what ());
    }
    return std::string {};
  };

  TerrainProgram invalid_orogeny = make_orogeny_program (123);
  std::get<OrogenyEvolution> (invalid_orogeny.transforms.front ())
    .evolution.reference_area =
    0.0f * mp_units::si::metre * mp_units::si::metre;
  MOPPE_CHECK (validation_error (invalid_orogeny) ==
               "orogeny evolution parameters are invalid");

  TerrainProgram invalid_trail =
    make_world_program (123, TerrainGenerationProfile::Fast);
  auto& trails = std::get<TrailFormation> (invalid_trail.transforms.back ());
  trails.minimum_catchment_area =
    10.0f * mp_units::si::metre * mp_units::si::metre;
  trails.maximum_catchment_area =
    9.0f * mp_units::si::metre * mp_units::si::metre;
  MOPPE_CHECK (validation_error (invalid_trail) ==
               "trail formation parameters are invalid");

  TerrainProgram wrong_order =
    make_world_program (123, TerrainGenerationProfile::Fast);
  std::swap (wrong_order.transforms[0], wrong_order.transforms[1]);
  MOPPE_CHECK (
    validation_error (wrong_order) ==
    "terrain program must contain orogeny followed by optional trails");
}
