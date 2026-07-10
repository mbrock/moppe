#ifndef MOPPE_TERRAIN_GEOLOGICAL_HH
#define MOPPE_TERRAIN_GEOLOGICAL_HH

#include <moppe/terrain/field.hh>

#include <cstdint>
#include <optional>
#include <string_view>

namespace moppe::terrain {
  enum class GeologicalLayer {
    Combined,
    Continent,
    Plains,
    Mountains,
    MountainMask,
    WarpX,
    WarpY
  };

  struct GeologicalSeeds {
    std::uint32_t base;
    std::uint32_t ridge;
    std::uint32_t warp;
  };

  struct FractalNoiseParameters {
    float frequency = 1.0f;
    int octaves = 1;
    float lacunarity = 2.0f;
    float gain = 0.5f;
  };

  struct Offset2D {
    float x;
    float y;
  };

  struct DomainWarpParameters {
    FractalNoiseParameters noise { 3.0f, 4, 2.0f, 0.5f };
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
    float mask_low = 0.45f;
    float mask_high = 0.75f;
    float continent_weight = 0.55f;
    float plains_weight = 0.12f;
    float mountain_weight = 0.65f;
  };

  // A complete, copyable description of the geological field recipe.
  // Editing one of these values changes graph construction; it never
  // mutates an already-built field or raster.
  struct GeologicalRecipe {
    GeologicalSeeds seeds { };
    DomainWarpParameters warp { };
    RemappedNoiseParameters continent {
      { 2.5f, 4, 2.0f, 0.5f }, 0.5f, 0.5f
    };
    RemappedNoiseParameters plains {
      { 12.0f, 4, 2.0f, 0.5f }, 0.5f, 0.5f
    };
    FractalNoiseParameters mountains { 6.0f, 6, 2.05f, 0.55f };
    GeologicalBlendParameters blend { };
  };

  struct GeologicalFields {
    ScalarField warp_x;
    ScalarField warp_y;
    ScalarField warped_x;
    ScalarField warped_y;
    ScalarField continent;
    ScalarField plains;
    ScalarField mountains;
    ScalarField mountain_mask;
    ScalarField combined;
  };

  GeologicalSeeds derive_geological_seeds (std::uint32_t root_seed);
  GeologicalRecipe make_geological_recipe (std::uint32_t root_seed);
  void validate_geological_recipe (const GeologicalRecipe& recipe);
  GeologicalFields make_geological_fields (const GeologicalRecipe& recipe);
  GeologicalFields make_geological_fields (const GeologicalSeeds& seeds);
  ScalarField geological_layer (const GeologicalFields& fields,
				 GeologicalLayer layer);
  const char* geological_layer_name (GeologicalLayer layer);
  std::string_view geological_layer_id (GeologicalLayer layer);
  std::optional<GeologicalLayer> geological_layer_from_id
    (std::string_view id);
}

#endif
