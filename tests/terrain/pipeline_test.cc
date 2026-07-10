#include <moppe/terrain/program.hh>

#include <tests/test.hh>

#include <stdexcept>
#include <variant>

using namespace moppe::terrain;

MOPPE_TEST (geological_pipeline_has_an_explicit_normalization_stage) {
  const TerrainProgram pipeline = make_geological_program
    (123, GeologicalLayer::Mountains);

  MOPPE_CHECK (pipeline.layer == GeologicalLayer::Mountains);
  MOPPE_CHECK (pipeline.randomness.seed == 123);
  MOPPE_CHECK (pipeline.randomness.offset == 3);
  MOPPE_CHECK (pipeline.transforms.size () == 1);
  MOPPE_CHECK (std::holds_alternative<NormalizeHeights>
    (pipeline.transforms[0]));
}

MOPPE_TEST (default_world_pipeline_records_every_transform) {
  const TerrainProgram pipeline = make_default_world_program (123);

  MOPPE_CHECK (pipeline.transforms.size () == 4);
  MOPPE_CHECK (terrain_transform_id (pipeline.transforms[0]) == "normalize");
  MOPPE_CHECK (terrain_transform_id (pipeline.transforms[1]) == "power");
  MOPPE_CHECK (terrain_transform_id (pipeline.transforms[2]) == "hydraulic");
  MOPPE_CHECK (terrain_transform_id (pipeline.transforms[3]) == "thermal");
  MOPPE_CHECK
    (std::get<HydraulicErosion> (pipeline.transforms[2]).droplets
     == 1500000);
  MOPPE_CHECK
    (std::get<HydraulicErosion> (pipeline.transforms[2]).batch_size == 256);
}

MOPPE_TEST (transform_semantics_describe_execution_requirements) {
  MOPPE_CHECK
    (terrain_transform_semantics (PowerHeights { 1.2f }).spatial_scope
     == SpatialScope::Pointwise);
  MOPPE_CHECK
    (terrain_transform_semantics (PowerHeights { 1.2f }).evaluation_order
     == EvaluationOrder::Direct);

  MOPPE_CHECK
    (terrain_transform_semantics (NormalizeHeights { }).spatial_scope
     == SpatialScope::Global);
  MOPPE_CHECK
    (terrain_transform_semantics (NormalizeHeights { }).evaluation_order
     == EvaluationOrder::Reduction);

  MOPPE_CHECK
    (terrain_transform_semantics (ThermalErosion { 1, 0.01f })
       .spatial_scope == SpatialScope::Neighborhood);
  MOPPE_CHECK
    (terrain_transform_semantics (ThermalErosion { 1, 0.01f })
       .evaluation_order == EvaluationOrder::Iterative);

  MOPPE_CHECK
    (terrain_transform_semantics (HydraulicErosion { 10 })
       .spatial_scope == SpatialScope::Global);
  MOPPE_CHECK
    (terrain_transform_semantics (HydraulicErosion { 10 })
       .evaluation_order == EvaluationOrder::Iterative);
}

MOPPE_TEST (pipeline_validation_rejects_invalid_stage_parameters) {
  TerrainProgram pipeline = make_geological_program (123);
  pipeline.transforms.emplace_back (HydraulicErosion { -1 });
  bool threw = false;
  try {
    validate_program (pipeline);
  } catch (const std::invalid_argument&) {
    threw = true;
  }
  MOPPE_CHECK (threw);

  pipeline = make_geological_program (123);
  pipeline.transforms.emplace_back (HydraulicErosion {
    .droplets = 10,
    .batch_size = 0
  });
  threw = false;
  try {
    validate_program (pipeline);
  } catch (const std::invalid_argument&) {
    threw = true;
  }
  MOPPE_CHECK (threw);
}
