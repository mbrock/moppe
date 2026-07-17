#include <moppe/terrain/editor.hh>
#include <moppe/terrain/program.hh>

#include <tests/test.hh>

#include <array>
#include <stdexcept>
#include <string>
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
    moppe::julian_years_value (orogeny.evolution.duration), 1000000.0f, 0.0f);
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

MOPPE_TEST (transform_values_own_their_editable_descriptions) {
  const PowerHeights power { 1.2f };
  const TransformDescription power_description = power.description ();
  MOPPE_CHECK (power_description.id == "power");
  MOPPE_CHECK (power_description.title == "POWER CURVE");
  MOPPE_CHECK (power_description.semantics.spatial_scope ==
               SpatialScope::Pointwise);
  MOPPE_CHECK (power.detail () == "height ^ 1.20");
  MOPPE_CHECK (power.property_count () == 1);
  const TransformProperty power_property = power.property (0);
  MOPPE_CHECK (power_property.label == "EXPONENT");
  MOPPE_CHECK (power_property.value == "1.20");
  MOPPE_CHECK (power_property.domain == ParameterDomain::Continuous);

  const std::array transforms {
    TerrainTransform { NormalizeHeights {} },
    TerrainTransform { PowerHeights { 1.2f } },
    TerrainTransform { AnalyticalErosion {} },
    TerrainTransform { OrogenyEvolution {} },
    TerrainTransform { ThermalErosion { iteration_count (1), 0.01f } },
    TerrainTransform { TrailFormation {} },
    TerrainTransform { HillslopeDiffusion {} },
  };
  const std::array<std::size_t, 7> property_counts = { 0, 1, 6, 8, 2, 14, 2 };
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

MOPPE_TEST (terrain_program_editor_delegates_to_typed_transform_editors) {
  TerrainProgram relief = make_geological_program (123);
  relief.transforms.emplace_back (PowerHeights { 1.2f });
  TerrainProgramEditor relief_editor (relief);
  const int original_continent_waves =
    relief.source.recipe.continent.noise.cycles;

  MOPPE_CHECK (relief_editor.source ().property_count () == 9);
  MOPPE_CHECK (relief_editor.set_source_normalized_property (0, 0.5f));
  MOPPE_CHECK_NEAR (relief.source.recipe.warp.amplitude, 0.3f, 1e-6f);
  MOPPE_CHECK (relief_editor.adjust_source_natural_property (1, 1));
  MOPPE_CHECK (relief.source.recipe.continent.noise.cycles ==
               original_continent_waves + 1);
  MOPPE_CHECK (relief_editor.set_transform_normalized_property (1, 0, 0.5f));
  MOPPE_CHECK_NEAR (
    std::get<PowerHeights> (relief.transforms[1]).exponent, 2.05f, 1e-6f);

  TerrainProgram orogeny = make_orogeny_program (123);
  TerrainProgramEditor orogeny_editor (orogeny);
  MOPPE_CHECK (orogeny_editor.set_transform_normalized_property (0, 7, 0.75f));
  const auto& evolution = std::get<OrogenyEvolution> (orogeny.transforms[0]);
  MOPPE_CHECK_NEAR (evolution.evolution.sea_level, 0.225f, 1e-6f);
  MOPPE_CHECK_NEAR (orogeny.source.sea_level, 0.225f, 1e-6f);

  const TerrainProgram& read_only_program = orogeny;
  const TerrainProgramEditor read_only (read_only_program);
  MOPPE_CHECK (read_only.transform (0).property (7).label == "SEA LEVEL");
}

MOPPE_TEST (program_validation_rejects_invalid_transform_parameters) {
  const auto validation_error = [] (TerrainTransform transform) {
    TerrainProgram program = make_geological_program (123);
    program.transforms.emplace_back (std::move (transform));
    try {
      validate_program (program);
    } catch (const std::invalid_argument& error) {
      return std::string (error.what ());
    }
    return std::string {};
  };

  MOPPE_CHECK (validation_error (PowerHeights { 0.0f }) ==
               "height exponent must be positive and finite");
  MOPPE_CHECK (validation_error (AnalyticalErosion { .erodibility = 0.0f }) ==
               "analytical erosion parameters are invalid");
  MOPPE_CHECK (validation_error (OrogenyEvolution {
                 .evolution = { .reference_area = 0.0f * mp_units::si::metre *
                                                  mp_units::si::metre } }) ==
               "orogeny evolution parameters are invalid");
  MOPPE_CHECK (
    validation_error (ThermalErosion { iteration_count (-1), 0.0f }) ==
    "thermal erosion parameters are invalid");
  MOPPE_CHECK (validation_error (TrailFormation {
                 .minimum_catchment_area =
                   10.0f * mp_units::si::metre * mp_units::si::metre,
                 .maximum_catchment_area =
                   9.0f * mp_units::si::metre * mp_units::si::metre }) ==
               "trail formation parameters are invalid");
  MOPPE_CHECK (validation_error (HillslopeDiffusion {
                 .duration = -1.0f * mp_units::astronomy::Julian_year }) ==
               "hillslope diffusion parameters are invalid");
}
