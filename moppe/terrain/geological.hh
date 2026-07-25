#ifndef MOPPE_TERRAIN_GEOLOGICAL_HH
#define MOPPE_TERRAIN_GEOLOGICAL_HH

#include <moppe/spatial/bundle.hh>
#include <moppe/terrain/domain.hh>
#include <moppe/terrain/types.hh>

#include <cstdint>
#include <functional>

namespace moppe::terrain {
  struct GeologicalSeeds {
    Seed base;
    Seed ridge;
    Seed warp;
  };

  struct FractalNoiseParameters {
    int cycles = 1;
    int octaves = 1;
    int lacunarity = 2;
    float gain = 0.5f;
  };

  struct Offset2D {
    float x;
    float y;
  };

  struct DomainWarpParameters {
    FractalNoiseParameters noise { 3, 4, 2, 0.5f };
    float amplitude = 0.15f;
    Offset2D x_offset { 11.3f, 7.7f };
    Offset2D y_offset { 91.1f, 33.9f };
  };

  struct RemappedNoiseParameters {
    FractalNoiseParameters noise;
    float scale;
    float bias;
  };

  struct GeologicalBlendParameters {
    float mask_low = 0.55f;
    float mask_high = 0.82f;
    float continent_weight = 0.55f;
    float plains_weight = 0.12f;
    float mountain_weight = 0.45f;
  };

  // A complete, copyable description of the geological field recipe.
  // Editing one of these values changes graph construction; it never
  // mutates an already-built field or raster.
  struct GeologicalRecipe {
    GeologicalSeeds seeds {};
    DomainWarpParameters warp {};
    RemappedNoiseParameters continent { { 3, 4, 2, 0.5f }, 0.5f, 0.5f };
    RemappedNoiseParameters plains { { 12, 4, 2, 0.5f }, 0.5f, 0.5f };
    FractalNoiseParameters mountains { 4, 6, 2, 0.55f };
    GeologicalBlendParameters blend {};
  };

  inline constexpr struct continent_shape
      : quantity_spec<mp_units::dimensionless, mp_units::is_kind> {
  } continent_shape;
  inline constexpr struct uplift_weight
      : quantity_spec<mp_units::dimensionless, mp_units::is_kind> {
  } uplift_weight;

  using ContinentShape = quantity<continent_shape[one], float>;
  using UpliftWeight = quantity<uplift_weight[one], float>;
  using GeologicalSections =
    spatial::Bundle<TerrainDomain, ContinentShape, UpliftWeight>;
  using GeologicalProgress =
    std::function<void (std::size_t completed_rows, std::size_t total_rows)>;

  GeologicalSeeds derive_geological_seeds (std::uint32_t root_seed);
  GeologicalRecipe make_geological_recipe (std::uint32_t root_seed);
  void validate_geological_recipe (const GeologicalRecipe& recipe);
  GeologicalSections generate_geology (TerrainDomain domain,
                                       const GeologicalRecipe& recipe,
                                       const GeologicalProgress& progress = {});
}

#endif
