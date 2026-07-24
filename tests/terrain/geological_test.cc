#include <moppe/terrain/cpu_evaluator.hh>
#include <moppe/terrain/geological.hh>

#include <tests/test.hh>

#include <array>
#include <bit>
#include <concepts>
#include <cstdint>
#include <stdexcept>
#include <variant>

using namespace moppe::terrain;

namespace {
  std::uint64_t raster_hash (const ScalarRaster& raster) {
    std::uint64_t hash = 1469598103934665603ull;
    for (float value : raster.values ()) {
      const std::uint32_t bits = std::bit_cast<std::uint32_t> (value);
      for (int byte = 0; byte < 4; ++byte) {
        hash ^= (bits >> (byte * 8)) & 0xff;
        hash *= 1099511628211ull;
      }
    }
    return hash;
  }
}

MOPPE_TEST (periodic_geological_recipe_has_stable_output) {
  constexpr std::array golden {
    0x863524e29ef4a927ull, 0x5e8c75981b887e30ull, 0xf45bb04923b0c0b4ull,
    0x05dc0a7fb3a5cd60ull, 0x6dad6253ffe20c62ull, 0x6c928009ffd5c17aull,
    0x3f8851268f877c20ull,
  };
  const GeologicalFields geological =
    make_geological_fields (make_geological_recipe (123));
  const std::array fields {
    geological.combined.untyped (),      geological.continent.untyped (),
    geological.plains.untyped (),        geological.mountains.untyped (),
    geological.mountain_mask.untyped (), geological.warp_x.untyped (),
    geological.warp_y.untyped ()
  };
  const FieldSamplingGrid2D domain { .width = 65, .height = 65 };

  for (std::size_t i = 0; i < fields.size (); ++i)
    MOPPE_CHECK (raster_hash (normalize (
                   CpuEvaluator ().evaluate (fields[i], domain))) == golden[i]);
}

MOPPE_TEST (geological_fields_share_warp_subexpressions) {
  GeologicalRecipe recipe;
  recipe.seeds = { .base = Seed { 1 },
                   .ridge = Seed { 2 },
                   .warp = Seed { 3 } };
  const GeologicalFields fields = make_geological_fields (recipe);
  static_assert (std::same_as<decltype (fields.warp_x), NoiseField>);
  static_assert (
    std::same_as<decltype (fields.combined), RelativeElevationField>);
  static_assert (std::same_as<decltype (fields.uplift), RelativeUpliftField>);
  const auto& warped_x =
    std::get<expression::MultiplyAdd> (fields.warped_x.node ()->operation);
  const auto& warped_y =
    std::get<expression::MultiplyAdd> (fields.warped_y.node ()->operation);

  MOPPE_CHECK (warped_x.multiplicand == fields.warp_x.node ());
  MOPPE_CHECK (warped_y.multiplicand == fields.warp_y.node ());
  // 55: the warp amplitude constant is a single shared node feeding
  // both warped coordinates (it was duplicated before the typed
  // recipe hoisted it).
  MOPPE_CHECK (unique_node_count (fields.combined.untyped ()) == 55);
}

MOPPE_TEST (geological_recipe_parameters_are_first_class_values) {
  GeologicalRecipe recipe = make_geological_recipe (123);
  const GeologicalSeeds seeds = derive_geological_seeds (123);

  MOPPE_CHECK (recipe.seeds.base == seeds.base);
  MOPPE_CHECK (recipe.seeds.ridge == seeds.ridge);
  MOPPE_CHECK (recipe.seeds.warp == seeds.warp);
  MOPPE_CHECK_NEAR (recipe.warp.amplitude, 0.15f, 1e-6f);
  MOPPE_CHECK (recipe.mountains.cycles == 4);

  recipe.mountains.cycles = 8;
  const RelativeElevationRaster changed =
    materialize (CpuEvaluator (),
                 make_geological_fields (recipe).mountains,
                 { .width = 17, .height = 17 });
  const RelativeElevationRaster original = materialize (
    CpuEvaluator (),
    make_geological_fields (make_geological_recipe (123)).mountains,
    { .width = 17, .height = 17 });
  MOPPE_CHECK (changed.sample (8, 8) != original.sample (8, 8));
}

MOPPE_TEST (geological_recipe_validation_rejects_bad_mask_edges) {
  GeologicalRecipe recipe = make_geological_recipe (123);
  recipe.blend.mask_high = recipe.blend.mask_low;
  bool threw = false;
  try {
    (void)make_geological_fields (recipe);
  } catch (const std::invalid_argument&) {
    threw = true;
  }
  MOPPE_CHECK (threw);
}

MOPPE_TEST (every_geological_layer_is_periodic) {
  const GeologicalFields geological =
    make_geological_fields (make_geological_recipe (123));
  const std::array fields {
    geological.combined.untyped (),      geological.continent.untyped (),
    geological.plains.untyped (),        geological.mountains.untyped (),
    geological.mountain_mask.untyped (), geological.warp_x.untyped (),
    geological.warp_y.untyped ()
  };

  for (const ScalarField& field : fields) {
    const ScalarRaster raster =
      CpuEvaluator ().evaluate (field, { .width = 65, .height = 65 });
    for (std::size_t i = 0; i < 65; ++i) {
      MOPPE_CHECK_NEAR (raster.at (0, i), raster.at (64, i), 1e-5f);
      MOPPE_CHECK_NEAR (raster.at (i, 0), raster.at (i, 64), 1e-5f);
    }
  }
}
