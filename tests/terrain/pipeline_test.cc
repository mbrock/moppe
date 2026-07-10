#include <moppe/terrain/pipeline.hh>

#include <tests/test.hh>

#include <stdexcept>
#include <variant>

using namespace moppe::terrain;

MOPPE_TEST (geological_pipeline_has_an_explicit_normalization_stage) {
  const TerrainPipeline pipeline = make_geological_pipeline
    (123, GeologicalLayer::Mountains);

  MOPPE_CHECK (pipeline.layer == GeologicalLayer::Mountains);
  MOPPE_CHECK (pipeline.randomness.seed == 123);
  MOPPE_CHECK (pipeline.randomness.offset == 3);
  MOPPE_CHECK (pipeline.stages.size () == 1);
  MOPPE_CHECK (std::holds_alternative<NormalizeHeights>
    (pipeline.stages[0]));
}

MOPPE_TEST (default_world_pipeline_records_every_transform) {
  const TerrainPipeline pipeline = make_default_world_pipeline (123);

  MOPPE_CHECK (pipeline.stages.size () == 4);
  MOPPE_CHECK (pipeline_stage_id (pipeline.stages[0]) == "normalize");
  MOPPE_CHECK (pipeline_stage_id (pipeline.stages[1]) == "power");
  MOPPE_CHECK (pipeline_stage_id (pipeline.stages[2]) == "hydraulic");
  MOPPE_CHECK (pipeline_stage_id (pipeline.stages[3]) == "thermal");
  MOPPE_CHECK
    (std::get<HydraulicErosion> (pipeline.stages[2]).droplets
     == 1500000);
  MOPPE_CHECK
    (std::get<HydraulicErosion> (pipeline.stages[2]).batch_size == 256);
}

MOPPE_TEST (pipeline_validation_rejects_invalid_stage_parameters) {
  TerrainPipeline pipeline = make_geological_pipeline (123);
  pipeline.stages.emplace_back (HydraulicErosion { -1 });
  bool threw = false;
  try {
    validate_pipeline (pipeline);
  } catch (const std::invalid_argument&) {
    threw = true;
  }
  MOPPE_CHECK (threw);

  pipeline = make_geological_pipeline (123);
  pipeline.stages.emplace_back (HydraulicErosion {
    .droplets = 10,
    .batch_size = 0
  });
  threw = false;
  try {
    validate_pipeline (pipeline);
  } catch (const std::invalid_argument&) {
    threw = true;
  }
  MOPPE_CHECK (threw);
}
