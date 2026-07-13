#ifndef MOPPE_TERRAIN_PROGRAM_HH
#define MOPPE_TERRAIN_PROGRAM_HH

#include <moppe/terrain/erosion.hh>
#include <moppe/terrain/geological.hh>
#include <moppe/terrain/stream_power_evolution.hh>
#include <moppe/terrain/types.hh>

#include <cstddef>
#include <cstdint>
#include <string_view>
#include <variant>
#include <vector>

namespace moppe::terrain {
  struct NormalizeHeights {};

  struct PowerHeights {
    float exponent;
  };

  struct HydraulicErosion {
    DropletCount droplets;
    BatchSize batch_size = terrain::batch_size (256);
    StepCount max_steps = step_count (64);
    float minimum_water = 0.0f;
    SedimentDisposition sediment_at_termination = SedimentDisposition::Discard;
    CarvingRule carving_rule = CarvingRule::PathMonotone;
  };

  struct ThermalErosion {
    IterationCount iterations;
    float talus;
  };

  // A geological uplift pattern scaled into physical tectonic velocity and
  // evolved against stream-power incision plus interleaved soil diffusion.
  struct OrogenyEvolution {
    meters_per_julian_year_t maximum_uplift_rate =
      0.001f * mp_units::si::metre / mp_units::astronomy::Julian_year;
    StreamPowerEvolution evolution { .diffusivity =
                                       0.0001f * mp_units::si::metre *
                                       mp_units::si::metre /
                                       mp_units::astronomy::Julian_year };
  };

  using TerrainTransform = std::variant<NormalizeHeights,
                                        PowerHeights,
                                        AnalyticalErosion,
                                        OrogenyEvolution,
                                        HydraulicErosion,
                                        ThermalErosion,
                                        ChannelCarving,
                                        HillslopeDiffusion>;

  using TerrainTransformReport = std::variant<std::monostate,
                                              AnalyticalErosionReport,
                                              StreamPowerEvolutionReport,
                                              HydraulicErosionReport,
                                              ChannelCarvingReport,
                                              HillslopeDiffusionReport>;

  // These two axes describe what an evaluator must observe, without
  // prescribing whether it uses a CPU loop, a GPU kernel, or something
  // else.  In more abstract language they distinguish pointwise maps,
  // local context, and operations whose answer depends on the whole terrain.
  enum class SpatialScope { Pointwise, Neighborhood, Global };

  enum class EvaluationOrder { Direct, Reduction, Iterative };

  struct TransformSemantics {
    SpatialScope spatial_scope;
    EvaluationOrder evaluation_order;
  };

  // Serializable position in the pseudorandom stream used by stateful
  // stages.  The offset preserves the historical sequence after the
  // geological source consumed its three component seeds.
  struct RandomSequence {
    Seed seed;
    SequenceOffset offset;
  };

  // A source is a value in its own right, rather than an implicit prelude to
  // the transform list.  The geological source retains its recipe so tools
  // can edit meaningful parameters and expand it into a ScalarField on demand.
  struct GeologicalSource {
    GeologicalRecipe recipe;
    GeologicalLayer layer;
    enum class Mode { Relief, Orogeny } mode = Mode::Relief;
    // Orogeny begins from a continent-shaped seed only deep enough to
    // establish land, ocean, and initial drainage. Mountain relief must then
    // be earned by uplift against erosion.
    float sea_level = 50.0f / 650.0f;
    float coastline = 0.5f;
    meters_t initial_relief = 20.0f * mp_units::si::metre;
  };

  struct TerrainProgram {
    GeologicalSource source;
    RandomSequence randomness;
    std::vector<TerrainTransform> transforms;
  };

  enum class TerrainGenerationProfile { Fast, Play, Research };

  TerrainProgram
  make_geological_program (std::uint32_t root_seed,
                           GeologicalLayer layer = GeologicalLayer::Combined);
  TerrainProgram make_default_world_program (std::uint32_t root_seed);
  TerrainProgram make_orogeny_program (
    std::uint32_t root_seed,
    TerrainGenerationProfile profile = TerrainGenerationProfile::Research);
  TerrainProgram make_world_program (std::uint32_t root_seed,
                                     TerrainGenerationProfile profile);
  int profile_droplet_count (TerrainGenerationProfile profile) noexcept;
  int profile_stream_power_iterations (
    TerrainGenerationProfile profile) noexcept;
  std::string_view profile_id (TerrainGenerationProfile profile) noexcept;

  void validate_program (const TerrainProgram& program);
  std::string_view terrain_transform_id (const TerrainTransform& transform);
  TransformSemantics
  terrain_transform_semantics (const TerrainTransform& transform);
}

#endif
