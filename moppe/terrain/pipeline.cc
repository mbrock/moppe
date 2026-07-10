#include <moppe/terrain/pipeline.hh>

#include <cmath>
#include <stdexcept>
#include <type_traits>

namespace moppe::terrain {
  TerrainPipeline make_geological_pipeline
    (std::uint32_t root_seed, GeologicalLayer layer) {
    return {
      .recipe = make_geological_recipe (root_seed),
      .layer = layer,
      .randomness = { .seed = root_seed, .offset = 3 },
      .stages = { NormalizeHeights { } }
    };
  }

  TerrainPipeline make_default_world_pipeline
    (std::uint32_t root_seed) {
    TerrainPipeline pipeline = make_geological_pipeline (root_seed);
    // Slight lowland squash; roughly 10-15% becomes ocean.
    pipeline.stages.emplace_back (PowerHeights { 1.15f });
    pipeline.stages.emplace_back (HydraulicErosion { 1500000 });
    // Talus angle is about 40 degrees at 2.4 m cells and 650 m height.
    pipeline.stages.emplace_back (ThermalErosion { 2, 0.003f });
    return pipeline;
  }

  void validate_pipeline (const TerrainPipeline& pipeline) {
    validate_geological_recipe (pipeline.recipe);
    for (const PipelineStage& stage : pipeline.stages) {
      std::visit ([] (const auto& operation) {
	using T = std::decay_t<decltype (operation)>;
	if constexpr (std::is_same_v<T, PowerHeights>) {
	  if (!std::isfinite (operation.exponent)
	      || operation.exponent <= 0.0f)
	    throw std::invalid_argument
	      ("height exponent must be positive and finite");
	} else if constexpr (std::is_same_v<T, HydraulicErosion>) {
	  if (operation.droplets < 0)
	    throw std::invalid_argument
	      ("hydraulic droplet count cannot be negative");
	} else if constexpr (std::is_same_v<T, ThermalErosion>) {
	  if (operation.iterations < 0
	      || !std::isfinite (operation.talus)
	      || operation.talus < 0.0f)
	    throw std::invalid_argument
	      ("thermal erosion parameters are invalid");
	}
      }, stage);
    }
  }

  std::string_view pipeline_stage_id (const PipelineStage& stage) {
    return std::visit ([] (const auto& operation) -> std::string_view {
      using T = std::decay_t<decltype (operation)>;
      if constexpr (std::is_same_v<T, NormalizeHeights>)
	return "normalize";
      else if constexpr (std::is_same_v<T, PowerHeights>)
	return "power";
      else if constexpr (std::is_same_v<T, HydraulicErosion>)
	return "hydraulic";
      else
	return "thermal";
    }, stage);
  }
}
