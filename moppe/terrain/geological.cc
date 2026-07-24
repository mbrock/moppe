#include <moppe/terrain/geological.hh>

#include <cmath>
#include <random>
#include <stdexcept>
#include <string>

namespace moppe::terrain {
  namespace {
    void validate_noise (const FractalNoiseParameters& noise,
                         const char* name) {
      if (noise.cycles <= 0 || noise.octaves <= 0 || noise.lacunarity <= 0 ||
          !std::isfinite (noise.gain) || noise.gain <= 0.0f)
        throw std::invalid_argument (std::string (name) +
                                     " noise parameters are invalid");

      int octave_cycles = noise.cycles;
      for (int octave = 1; octave < noise.octaves; ++octave) {
        if (octave_cycles > 256 / noise.lacunarity)
          throw std::invalid_argument (std::string (name) +
                                       " noise exceeds the 256-cell lattice");
        octave_cycles *= noise.lacunarity;
      }
    }

    void require_finite (float value, const char* name) {
      if (!std::isfinite (value))
        throw std::invalid_argument (std::string (name) + " must be finite");
    }
  }

  GeologicalSeeds derive_geological_seeds (std::uint32_t root_seed) {
    std::mt19937 rng (root_seed);
    return { .base = Seed { rng () },
             .ridge = Seed { rng () },
             .warp = Seed { rng () } };
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
    if (!std::isfinite (recipe.blend.mask_low) ||
        !std::isfinite (recipe.blend.mask_high) ||
        recipe.blend.mask_high <= recipe.blend.mask_low)
      throw std::invalid_argument ("mountain mask edges must increase");
  }

  GeologicalFields make_geological_fields (const GeologicalRecipe& recipe) {
    validate_geological_recipe (recipe);
    const CoordinateField u = coordinate_u ();
    const CoordinateField v = coordinate_v ();

    const int warp_cycles = recipe.warp.noise.cycles;
    const NoiseField warp_x = periodic_fbm_noise (
      recipe.seeds.warp,
      u * static_cast<float> (warp_cycles) + recipe.warp.x_offset.x,
      v * static_cast<float> (warp_cycles) + recipe.warp.x_offset.y,
      warp_cycles,
      warp_cycles,
      recipe.warp.noise.octaves,
      recipe.warp.noise.lacunarity,
      recipe.warp.noise.gain);
    const NoiseField warp_y = periodic_fbm_noise (
      recipe.seeds.warp,
      u * static_cast<float> (warp_cycles) + recipe.warp.y_offset.x,
      v * static_cast<float> (warp_cycles) + recipe.warp.y_offset.y,
      warp_cycles,
      warp_cycles,
      recipe.warp.noise.octaves,
      recipe.warp.noise.lacunarity,
      recipe.warp.noise.gain);
    // The warp amplitude is a coordinate displacement per unit of
    // noise; declaring it with the coordinate kind is what lets the
    // pure-number warp noise land back in coordinate space.
    const CoordinateField warp_amplitude =
      constant<field_coordinate> (recipe.warp.amplitude);
    const CoordinateField warped_x = multiply_add (warp_amplitude, warp_x, u);
    const CoordinateField warped_y = multiply_add (warp_amplitude, warp_y, v);

    const int continent_cycles = recipe.continent.noise.cycles;
    const RelativeElevationField continent =
      field_cast<relative_elevation> (multiply_add (
        periodic_fbm_noise (recipe.seeds.base,
                            warped_x * static_cast<float> (continent_cycles),
                            warped_y * static_cast<float> (continent_cycles),
                            continent_cycles,
                            continent_cycles,
                            recipe.continent.noise.octaves,
                            recipe.continent.noise.lacunarity,
                            recipe.continent.noise.gain),
        constant<> (recipe.continent.scale),
        constant<moppe::noise_signal> (recipe.continent.bias)));
    const int plains_cycles = recipe.plains.noise.cycles;
    const RelativeElevationField plains =
      field_cast<relative_elevation> (multiply_add (
        periodic_fbm_noise (recipe.seeds.base,
                            warped_x * static_cast<float> (plains_cycles),
                            warped_y * static_cast<float> (plains_cycles),
                            plains_cycles,
                            plains_cycles,
                            recipe.plains.noise.octaves,
                            recipe.plains.noise.lacunarity,
                            recipe.plains.noise.gain),
        constant<> (recipe.plains.scale),
        constant<moppe::noise_signal> (recipe.plains.bias)));
    const int mountain_cycles = recipe.mountains.cycles;
    const RelativeElevationField mountains = field_cast<relative_elevation> (
      periodic_ridged_noise (recipe.seeds.ridge,
                             warped_x * static_cast<float> (mountain_cycles),
                             warped_y * static_cast<float> (mountain_cycles),
                             mountain_cycles,
                             mountain_cycles,
                             recipe.mountains.octaves,
                             recipe.mountains.lacunarity,
                             recipe.mountains.gain));
    const ProportionField mountain_mask =
      smoothstep (recipe.blend.mask_low, recipe.blend.mask_high, continent);

    const RelativeElevationField lowland =
      plains * recipe.blend.plains_weight * (1.0f - mountain_mask);
    const RelativeElevationField combined = multiply_add (
      mountains * recipe.blend.mountain_weight,
      mountain_mask,
      multiply_add (
        continent, constant<> (recipe.blend.continent_weight), lowland));
    // Orogeny reuses the recipe's spatial language but changes its meaning:
    // this bounded pattern scales a physical uplift-rate amplitude. Ridges
    // locate harder-rising crust; they are no longer initial mountain relief.
    const RelativeUpliftField uplift =
      field_cast<relative_uplift> (smoothstep (0.0f, 1.0f, combined));

    return { .warp_x = warp_x,
             .warp_y = warp_y,
             .warped_x = warped_x,
             .warped_y = warped_y,
             .continent = continent,
             .plains = plains,
             .mountains = mountains,
             .mountain_mask = mountain_mask,
             .combined = combined,
             .uplift = uplift };
  }

}
