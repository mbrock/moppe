#ifndef MOPPE_TERRAIN_PROGRAM_HH
#define MOPPE_TERRAIN_PROGRAM_HH

#include <moppe/terrain/erosion.hh>
#include <moppe/terrain/geological.hh>
#include <moppe/terrain/stream_power_evolution.hh>
#include <moppe/terrain/trail.hh>
#include <moppe/terrain/transform.hh>
#include <moppe/terrain/types.hh>

#include <cstddef>
#include <cstdint>
#include <string_view>
#include <variant>
#include <vector>

namespace moppe::terrain {
  struct NormalizeHeights {
    void validate () const;
    TransformDescription description () const noexcept;
    std::string detail () const;
    std::size_t property_count () const noexcept;
    TransformProperty property (std::size_t index) const;
    float normalized_property (std::size_t index) const;
    bool set_normalized_property (std::size_t index, float value);
    bool adjust_natural_property (std::size_t index, int direction);
  };

  struct PowerHeights {
    float exponent;

    void validate () const;
    TransformDescription description () const noexcept;
    std::string detail () const;
    std::size_t property_count () const noexcept;
    TransformProperty property (std::size_t index) const;
    float normalized_property (std::size_t index) const;
    bool set_normalized_property (std::size_t index, float value);
    bool adjust_natural_property (std::size_t index, int direction);
  };

  struct ThermalErosion {
    IterationCount iterations;
    float talus;

    void validate () const;
    TransformDescription description () const noexcept;
    std::string detail () const;
    std::size_t property_count () const noexcept;
    TransformProperty property (std::size_t index) const;
    float normalized_property (std::size_t index) const;
    bool set_normalized_property (std::size_t index, float value);
    bool adjust_natural_property (std::size_t index, int direction);
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

    void validate () const;
    TransformDescription description () const noexcept;
    std::string detail () const;
    std::size_t property_count () const noexcept;
    TransformProperty property (std::size_t index) const;
    float normalized_property (std::size_t index) const;
    bool set_normalized_property (std::size_t index, float value);
    bool adjust_natural_property (std::size_t index, int direction);
    float source_sea_level () const noexcept {
      return evolution.sea_level;
    }
  };

  using TerrainTransform = std::variant<NormalizeHeights,
                                        PowerHeights,
                                        AnalyticalErosion,
                                        OrogenyEvolution,
                                        ThermalErosion,
                                        TrailFormation,
                                        HillslopeDiffusion>;

  using TerrainTransformReport = std::variant<std::monostate,
                                              AnalyticalErosionReport,
                                              StreamPowerEvolutionReport,
                                              TrailFormationReport,
                                              HillslopeDiffusionReport>;

  // A source is a value in its own right, rather than an implicit prelude to
  // the transform list.  The geological source retains its recipe so tools
  // can edit meaningful parameters and expand it into a ScalarField on demand.
  struct GeologicalSource {
    GeologicalRecipe recipe;
    GeologicalLayer layer;
    enum class Mode { Relief, Orogeny } mode = Mode::Relief;
    // Orogeny begins from a continent-shaped seed with separate emergent and
    // submerged scales. Mountain relief must then be earned by uplift against
    // erosion, while the fixed ocean boundary retains real bathymetry.
    float sea_level = 50.0f / 650.0f;
    float coastline = 0.4f;
    meters_t initial_land_relief = 20.0f * mp_units::si::metre;
    meters_t initial_bathymetric_relief = 240.0f * mp_units::si::metre;

    std::size_t property_count () const noexcept;
    TransformProperty property (std::size_t index) const;
    float normalized_property (std::size_t index) const;
    bool set_normalized_property (std::size_t index, float value);
    bool adjust_natural_property (std::size_t index, int direction);
  };

  struct TerrainProgram {
    GeologicalSource source;
    Seed seed;
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
  std::string_view profile_id (TerrainGenerationProfile profile) noexcept;

  void validate_program (const TerrainProgram& program);
  void validate_transform (const TerrainTransform& transform);
  std::string_view terrain_transform_id (const TerrainTransform& transform);
  TransformSemantics
  terrain_transform_semantics (const TerrainTransform& transform);
  TransformDescription
  terrain_transform_description (const TerrainTransform& transform);
  std::string terrain_transform_detail (const TerrainTransform& transform);
  std::size_t
  terrain_transform_property_count (const TerrainTransform& transform);
  TransformProperty
  terrain_transform_property (const TerrainTransform& transform,
                              std::size_t index);
}

#endif
