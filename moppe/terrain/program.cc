#include <moppe/terrain/program.hh>

#include <cmath>
#include <stdexcept>
#include <type_traits>

namespace moppe::terrain {
  TerrainProgram make_geological_program
    (std::uint32_t root_seed, GeologicalLayer layer) {
    return {
      .source = {
	.recipe = make_geological_recipe (root_seed),
	.layer = layer
      },
      .randomness = { .seed = root_seed, .offset = 3 },
      .transforms = { NormalizeHeights { } }
    };
  }

  TerrainProgram make_default_world_program
    (std::uint32_t root_seed) {
    TerrainProgram program = make_geological_program (root_seed);
    // Slight lowland squash; roughly 10-15% becomes ocean.
    program.transforms.emplace_back (PowerHeights { 1.15f });
    program.transforms.emplace_back (HydraulicErosion {
      .droplets = 300000,
      .batch_size = 256,
      .max_steps = 512,
      .minimum_water = 0.01f,
      .sediment_at_termination = SedimentDisposition::Deposit
    });
    // Talus angle is about 40 degrees at 2.4 m cells and 650 m height.
    program.transforms.emplace_back (ThermalErosion { 2, 0.003f });
    return program;
  }

  void validate_program (const TerrainProgram& program) {
    validate_geological_recipe (program.source.recipe);
    for (const TerrainTransform& transform : program.transforms) {
      std::visit ([] (const auto& operation) {
	using T = std::decay_t<decltype (operation)>;
	if constexpr (std::is_same_v<T, PowerHeights>) {
	  if (!std::isfinite (operation.exponent)
	      || operation.exponent <= 0.0f)
	    throw std::invalid_argument
	      ("height exponent must be positive and finite");
	} else if constexpr (std::is_same_v<T, HydraulicErosion>) {
	  if (operation.droplets < 0 || operation.batch_size <= 0
	      || operation.max_steps <= 0
	      || !std::isfinite (operation.minimum_water)
	      || operation.minimum_water < 0.0f
	      || operation.minimum_water >= 1.0f)
	    throw std::invalid_argument
	      ("hydraulic erosion needs a non-negative droplet count "
	       "and positive batch size and lifetime");
	} else if constexpr (std::is_same_v<T, ThermalErosion>) {
	  if (operation.iterations < 0
	      || !std::isfinite (operation.talus)
	      || operation.talus < 0.0f)
	    throw std::invalid_argument
	      ("thermal erosion parameters are invalid");
	}
      }, transform);
    }
  }

  std::string_view terrain_transform_id
    (const TerrainTransform& transform) {
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
    }, transform);
  }

  TransformSemantics terrain_transform_semantics
    (const TerrainTransform& transform) {
    return std::visit ([] (const auto& operation) -> TransformSemantics {
      using T = std::decay_t<decltype (operation)>;
      if constexpr (std::is_same_v<T, PowerHeights>)
	return { SpatialScope::Pointwise, EvaluationOrder::Direct };
      else if constexpr (std::is_same_v<T, NormalizeHeights>)
	return { SpatialScope::Global, EvaluationOrder::Reduction };
      else if constexpr (std::is_same_v<T, ThermalErosion>)
	return { SpatialScope::Neighborhood, EvaluationOrder::Iterative };
      else
	return { SpatialScope::Global, EvaluationOrder::Iterative };
    }, transform);
  }
}
