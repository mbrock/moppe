#include <moppe/terrain/program.hh>

#include <tests/test.hh>

#include <stdexcept>
#include <variant>

using namespace moppe::terrain;

MOPPE_TEST (geological_program_has_an_explicit_normalization_transform) {
  const TerrainProgram program = make_geological_program
    (123, GeologicalLayer::Mountains);

  MOPPE_CHECK (program.source.layer == GeologicalLayer::Mountains);
  MOPPE_CHECK (program.randomness.seed == 123);
  MOPPE_CHECK (program.randomness.offset == 3);
  MOPPE_CHECK (program.transforms.size () == 1);
  MOPPE_CHECK (std::holds_alternative<NormalizeHeights>
    (program.transforms[0]));
}

MOPPE_TEST (default_world_program_records_every_transform) {
  const TerrainProgram program = make_default_world_program (123);

  MOPPE_CHECK (program.transforms.size () == 4);
  MOPPE_CHECK (terrain_transform_id (program.transforms[0]) == "normalize");
  MOPPE_CHECK (terrain_transform_id (program.transforms[1]) == "power");
  MOPPE_CHECK (terrain_transform_id (program.transforms[2]) == "hydraulic");
  MOPPE_CHECK (terrain_transform_id (program.transforms[3]) == "thermal");
  MOPPE_CHECK
    (std::get<HydraulicErosion> (program.transforms[2]).droplets
     == 300000);
  MOPPE_CHECK
    (std::get<HydraulicErosion> (program.transforms[2]).batch_size == 256);
  MOPPE_CHECK
    (std::get<HydraulicErosion> (program.transforms[2]).max_steps == 512);
  MOPPE_CHECK_NEAR
    (std::get<HydraulicErosion>
     (program.transforms[2]).minimum_water, 0.01f, 0.0f);
  MOPPE_CHECK
    (std::get<HydraulicErosion>
     (program.transforms[2]).sediment_at_termination
     == SedimentDisposition::Deposit);
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

MOPPE_TEST (program_validation_rejects_invalid_transform_parameters) {
  TerrainProgram program = make_geological_program (123);
  program.transforms.emplace_back (HydraulicErosion { -1 });
  bool threw = false;
  try {
    validate_program (program);
  } catch (const std::invalid_argument&) {
    threw = true;
  }
  MOPPE_CHECK (threw);

  program = make_geological_program (123);
  program.transforms.emplace_back (HydraulicErosion {
    .droplets = 10,
    .batch_size = 0
  });
  threw = false;
  try {
    validate_program (program);
  } catch (const std::invalid_argument&) {
    threw = true;
  }
  MOPPE_CHECK (threw);

  program = make_geological_program (123);
  program.transforms.emplace_back (HydraulicErosion {
    .droplets = 10,
    .minimum_water = 1.0f
  });
  threw = false;
  try {
    validate_program (program);
  } catch (const std::invalid_argument&) {
    threw = true;
  }
  MOPPE_CHECK (threw);
}
