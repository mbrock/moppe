#ifndef MOPPE_TERRAIN_PROGRAM_HH
#define MOPPE_TERRAIN_PROGRAM_HH

#include <moppe/terrain/geological.hh>
#include <moppe/terrain/erosion.hh>

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
    int max_steps = 64;
    float minimum_water = 0.0f;
    SedimentDisposition sediment_at_termination =
      SedimentDisposition::Discard;
    CarvingRule carving_rule = CarvingRule::PathMonotone;
  };

  struct ThermalErosion {
    int iterations;
    float talus;
  };

  using TerrainTransform = std::variant
    <NormalizeHeights, PowerHeights, AnalyticalErosion,
     HydraulicErosion, ThermalErosion, ChannelCarving>;

  using TerrainTransformReport = std::variant
    <std::monostate, AnalyticalErosionReport, HydraulicErosionReport,
     ChannelCarvingReport>;

  // These two axes describe what an evaluator must observe, without
  // prescribing whether it uses a CPU loop, a GPU kernel, or something
  // else.  In more abstract language they distinguish pointwise maps,
  // local context, and operations whose answer depends on the whole terrain.
  enum class SpatialScope {
    Pointwise,
    Neighborhood,
    Global
  };

  enum class EvaluationOrder {
    Direct,
    Reduction,
    Iterative
  };

  struct TransformSemantics {
    SpatialScope spatial_scope;
    EvaluationOrder evaluation_order;
  };

  // Serializable position in the pseudorandom stream used by stateful
  // stages.  The offset preserves the historical sequence after the
  // geological source consumed its three component seeds.
  struct RandomSequence {
    std::uint32_t seed;
    std::uint64_t offset;
  };

  // A source is a value in its own right, rather than an implicit prelude to
  // the transform list.  The geological source retains its recipe so tools
  // can edit meaningful parameters and expand it into a ScalarField on demand.
  struct GeologicalSource {
    GeologicalRecipe recipe;
    GeologicalLayer layer;
  };

  struct TerrainProgram {
    GeologicalSource source;
    RandomSequence randomness;
    std::vector<TerrainTransform> transforms;
  };

  enum class TerrainGenerationProfile {
    Fast,
    Play,
    Research
  };

  TerrainProgram make_geological_program
    (std::uint32_t root_seed,
     GeologicalLayer layer = GeologicalLayer::Combined);
  TerrainProgram make_default_world_program (std::uint32_t root_seed);
  TerrainProgram make_world_program
    (std::uint32_t root_seed, TerrainGenerationProfile profile);
  int profile_droplet_count (TerrainGenerationProfile profile) noexcept;
  int profile_stream_power_iterations
    (TerrainGenerationProfile profile) noexcept;
  std::string_view profile_id (TerrainGenerationProfile profile) noexcept;

  void validate_program (const TerrainProgram& program);
  std::string_view terrain_transform_id (const TerrainTransform& transform);
  TransformSemantics terrain_transform_semantics
    (const TerrainTransform& transform);
}

#endif
