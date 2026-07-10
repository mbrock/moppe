#include <moppe/terrain/geological.hh>

#include <cmath>
#include <random>
#include <stdexcept>
#include <string>

namespace moppe::terrain {
  namespace {
    void validate_noise (const FractalNoiseParameters& noise,
			 const char* name) {
      if (!std::isfinite (noise.frequency) || noise.frequency <= 0.0f
	  || noise.octaves <= 0
	  || !std::isfinite (noise.lacunarity)
	  || noise.lacunarity <= 0.0f
	  || !std::isfinite (noise.gain) || noise.gain <= 0.0f)
	throw std::invalid_argument
	  (std::string (name) + " noise parameters are invalid");
    }

    void require_finite (float value, const char* name) {
      if (!std::isfinite (value))
	throw std::invalid_argument
	  (std::string (name) + " must be finite");
    }
  }

  GeologicalSeeds derive_geological_seeds (std::uint32_t root_seed) {
    std::mt19937 rng (root_seed);
    return {
      .base = rng (),
      .ridge = rng (),
      .warp = rng ()
    };
  }

  GeologicalRecipe make_geological_recipe (std::uint32_t root_seed) {
    return { .seeds = derive_geological_seeds (root_seed) };
  }

  void validate_geological_recipe (const GeologicalRecipe& recipe) {
    validate_noise (recipe.warp.noise, "warp");
    validate_noise (recipe.continent.noise, "continent");
    validate_noise (recipe.plains.noise, "plains");
    validate_noise (recipe.mountains, "mountains");
    require_finite (recipe.warp.amplitude, "warp amplitude");
    require_finite (recipe.warp.x_offset.x, "warp X offset X");
    require_finite (recipe.warp.x_offset.y, "warp X offset Y");
    require_finite (recipe.warp.y_offset.x, "warp Y offset X");
    require_finite (recipe.warp.y_offset.y, "warp Y offset Y");
    require_finite (recipe.continent.scale, "continent scale");
    require_finite (recipe.continent.bias, "continent bias");
    require_finite (recipe.plains.scale, "plains scale");
    require_finite (recipe.plains.bias, "plains bias");
    require_finite (recipe.blend.continent_weight, "continent weight");
    require_finite (recipe.blend.plains_weight, "plains weight");
    require_finite (recipe.blend.mountain_weight, "mountain weight");
    if (!std::isfinite (recipe.blend.mask_low)
	|| !std::isfinite (recipe.blend.mask_high)
	|| recipe.blend.mask_high <= recipe.blend.mask_low)
      throw std::invalid_argument ("mountain mask edges must increase");
  }

  GeologicalFields make_geological_fields
    (const GeologicalRecipe& recipe) {
    validate_geological_recipe (recipe);
    const ScalarField u = coordinate_x ();
    const ScalarField v = coordinate_y ();

    const ScalarField warp_x = fbm_noise
      (recipe.seeds.warp,
       u * recipe.warp.noise.frequency + recipe.warp.x_offset.x,
       v * recipe.warp.noise.frequency + recipe.warp.x_offset.y,
       recipe.warp.noise.octaves, recipe.warp.noise.lacunarity,
       recipe.warp.noise.gain);
    const ScalarField warp_y = fbm_noise
      (recipe.seeds.warp,
       u * recipe.warp.noise.frequency + recipe.warp.y_offset.x,
       v * recipe.warp.noise.frequency + recipe.warp.y_offset.y,
       recipe.warp.noise.octaves, recipe.warp.noise.lacunarity,
       recipe.warp.noise.gain);
    const ScalarField warped_x = multiply_add
      (constant (recipe.warp.amplitude), warp_x, u);
    const ScalarField warped_y = multiply_add
      (constant (recipe.warp.amplitude), warp_y, v);

    const ScalarField continent = multiply_add
      (fbm_noise
      (recipe.seeds.base,
       warped_x * recipe.continent.noise.frequency,
       warped_y * recipe.continent.noise.frequency,
       recipe.continent.noise.octaves,
       recipe.continent.noise.lacunarity,
       recipe.continent.noise.gain),
       constant (recipe.continent.scale),
       constant (recipe.continent.bias));
    const ScalarField plains = multiply_add
      (fbm_noise
      (recipe.seeds.base,
       warped_x * recipe.plains.noise.frequency,
       warped_y * recipe.plains.noise.frequency,
       recipe.plains.noise.octaves,
       recipe.plains.noise.lacunarity,
       recipe.plains.noise.gain),
       constant (recipe.plains.scale),
       constant (recipe.plains.bias));
    const ScalarField mountains = ridged_noise
      (recipe.seeds.ridge,
       warped_x * recipe.mountains.frequency,
       warped_y * recipe.mountains.frequency,
       recipe.mountains.octaves, recipe.mountains.lacunarity,
       recipe.mountains.gain);
    const ScalarField mountain_mask = smoothstep
      (recipe.blend.mask_low, recipe.blend.mask_high, continent);

    const ScalarField lowland = plains * recipe.blend.plains_weight
      * (1.0f - mountain_mask);
    const ScalarField combined = multiply_add
      (mountains * recipe.blend.mountain_weight, mountain_mask,
       multiply_add
	 (continent, constant (recipe.blend.continent_weight), lowland));

    return {
      .warp_x = warp_x,
      .warp_y = warp_y,
      .warped_x = warped_x,
      .warped_y = warped_y,
      .continent = continent,
      .plains = plains,
      .mountains = mountains,
      .mountain_mask = mountain_mask,
      .combined = combined
    };
  }

  GeologicalFields make_geological_fields
    (const GeologicalSeeds& seeds) {
    GeologicalRecipe recipe;
    recipe.seeds = seeds;
    return make_geological_fields (recipe);
  }

  ScalarField geological_layer (const GeologicalFields& fields,
				 GeologicalLayer layer) {
    switch (layer) {
    case GeologicalLayer::Combined:     return fields.combined;
    case GeologicalLayer::Continent:    return fields.continent;
    case GeologicalLayer::Plains:       return fields.plains;
    case GeologicalLayer::Mountains:    return fields.mountains;
    case GeologicalLayer::MountainMask: return fields.mountain_mask;
    case GeologicalLayer::WarpX:        return fields.warp_x;
    case GeologicalLayer::WarpY:        return fields.warp_y;
    }
    return fields.combined;
  }

  const char* geological_layer_name (GeologicalLayer layer) {
    switch (layer) {
    case GeologicalLayer::Combined:     return "combined terrain";
    case GeologicalLayer::Continent:    return "continent field";
    case GeologicalLayer::Plains:       return "plains detail";
    case GeologicalLayer::Mountains:    return "ridged mountains";
    case GeologicalLayer::MountainMask: return "mountain mask";
    case GeologicalLayer::WarpX:        return "domain warp X";
    case GeologicalLayer::WarpY:        return "domain warp Y";
    }
    return "unknown";
  }

  std::string_view geological_layer_id (GeologicalLayer layer) {
    switch (layer) {
    case GeologicalLayer::Combined:     return "combined";
    case GeologicalLayer::Continent:    return "continent";
    case GeologicalLayer::Plains:       return "plains";
    case GeologicalLayer::Mountains:    return "mountains";
    case GeologicalLayer::MountainMask: return "mask";
    case GeologicalLayer::WarpX:        return "warp-x";
    case GeologicalLayer::WarpY:        return "warp-y";
    }
    return "unknown";
  }

  std::optional<GeologicalLayer> geological_layer_from_id
    (std::string_view id) {
    constexpr GeologicalLayer layers[] = {
      GeologicalLayer::Combined,
      GeologicalLayer::Continent,
      GeologicalLayer::Plains,
      GeologicalLayer::Mountains,
      GeologicalLayer::MountainMask,
      GeologicalLayer::WarpX,
      GeologicalLayer::WarpY
    };
    for (GeologicalLayer layer : layers)
      if (geological_layer_id (layer) == id)
	return layer;
    return std::nullopt;
  }
}
