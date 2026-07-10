#ifndef MOPPE_TERRAIN_PIPELINE_HH
#define MOPPE_TERRAIN_PIPELINE_HH

#include <moppe/terrain/geological.hh>

#include <cstddef>
#include <cstdint>
#include <string_view>
#include <variant>
#include <vector>

namespace moppe::terrain {
  struct NormalizeHeights { };

  struct PowerHeights {
    float exponent;
  };

  struct HydraulicErosion {
    int droplets;
    int batch_size = 256;
  };

  struct ThermalErosion {
    int iterations;
    float talus;
  };

  using PipelineStage = std::variant
    <NormalizeHeights, PowerHeights, HydraulicErosion, ThermalErosion>;

  // Serializable position in the pseudorandom stream used by stateful
  // stages.  The offset preserves the historical sequence after the
  // geological source consumed its three component seeds.
  struct RandomSequence {
    std::uint32_t seed;
    std::uint64_t offset;
  };

  struct TerrainPipeline {
    GeologicalRecipe recipe;
    GeologicalLayer layer;
    RandomSequence randomness;
    std::vector<PipelineStage> stages;
  };

  TerrainPipeline make_geological_pipeline
    (std::uint32_t root_seed,
     GeologicalLayer layer = GeologicalLayer::Combined);
  TerrainPipeline make_default_world_pipeline (std::uint32_t root_seed);

  void validate_pipeline (const TerrainPipeline& pipeline);
  std::string_view pipeline_stage_id (const PipelineStage& stage);
}

#endif
