#include <moppe/terrain/cpu_evaluator.hh>
#include <moppe/terrain/geological.hh>

#include <tests/test.hh>

#include <array>
#include <bit>
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

MOPPE_TEST (geological_recipe_matches_historical_generator) {
  struct GoldenLayer {
    GeologicalLayer layer;
    std::uint64_t hash;
  };
  constexpr std::array golden {
    GoldenLayer { GeologicalLayer::Combined, 0xdd6de93ca7d0e2e9ull },
    GoldenLayer { GeologicalLayer::Continent, 0xac072157b08bf34aull },
    GoldenLayer { GeologicalLayer::Plains, 0xd9c3caa65fed7cb5ull },
    GoldenLayer { GeologicalLayer::Mountains, 0x1383f3e449c454adull },
    GoldenLayer { GeologicalLayer::MountainMask, 0x4f03a4bb126370e2ull },
    GoldenLayer { GeologicalLayer::WarpX, 0xbcb3154439c01f35ull },
    GoldenLayer { GeologicalLayer::WarpY, 0xc67193ae4fb03b82ull }
  };
  const GeologicalFields fields = make_geological_fields
    (derive_geological_seeds (123));
  const Domain2D domain { .width = 65, .height = 65 };

  for (const GoldenLayer& expected : golden) {
    const ScalarRaster raster = normalize
      (CpuEvaluator ().evaluate
	(geological_layer (fields, expected.layer), domain));
    MOPPE_CHECK (raster_hash (raster) == expected.hash);
  }
}

MOPPE_TEST (geological_fields_share_warp_subexpressions) {
  const GeologicalFields fields = make_geological_fields
    ({ .base = 1, .ridge = 2, .warp = 3 });
  const auto& warped_x = std::get<expression::MultiplyAdd>
    (fields.warped_x.node ()->operation);
  const auto& warped_y = std::get<expression::MultiplyAdd>
    (fields.warped_y.node ()->operation);

  MOPPE_CHECK (warped_x.multiplicand == fields.warp_x.node ());
  MOPPE_CHECK (warped_y.multiplicand == fields.warp_y.node ());
  MOPPE_CHECK (unique_node_count (fields.combined) == 56);
}

MOPPE_TEST (geological_layer_ids_round_trip) {
  constexpr GeologicalLayer layers[] = {
    GeologicalLayer::Combined,
    GeologicalLayer::Continent,
    GeologicalLayer::Plains,
    GeologicalLayer::Mountains,
    GeologicalLayer::MountainMask,
    GeologicalLayer::WarpX,
    GeologicalLayer::WarpY
  };
  for (GeologicalLayer layer : layers) {
    const auto parsed = geological_layer_from_id
      (geological_layer_id (layer));
    MOPPE_CHECK (parsed && *parsed == layer);
  }
  MOPPE_CHECK (!geological_layer_from_id ("not-a-layer"));
}

MOPPE_TEST (geological_recipe_parameters_are_first_class_values) {
  GeologicalRecipe recipe = make_geological_recipe (123);
  const GeologicalSeeds seeds = derive_geological_seeds (123);

  MOPPE_CHECK (recipe.seeds.base == seeds.base);
  MOPPE_CHECK (recipe.seeds.ridge == seeds.ridge);
  MOPPE_CHECK (recipe.seeds.warp == seeds.warp);
  MOPPE_CHECK_NEAR (recipe.warp.amplitude, 0.15f, 1e-6f);
  MOPPE_CHECK_NEAR (recipe.mountains.frequency, 6.0f, 1e-6f);

  recipe.mountains.frequency = 8.0f;
  const ScalarRaster changed = CpuEvaluator ().evaluate
    (make_geological_fields (recipe).mountains,
     { .width = 17, .height = 17 });
  const ScalarRaster original = CpuEvaluator ().evaluate
    (make_geological_fields (make_geological_recipe (123)).mountains,
     { .width = 17, .height = 17 });
  MOPPE_CHECK (changed.at (8, 8) != original.at (8, 8));
}

MOPPE_TEST (geological_recipe_validation_rejects_bad_mask_edges) {
  GeologicalRecipe recipe = make_geological_recipe (123);
  recipe.blend.mask_high = recipe.blend.mask_low;
  bool threw = false;
  try {
    (void) make_geological_fields (recipe);
  } catch (const std::invalid_argument&) {
    threw = true;
  }
  MOPPE_CHECK (threw);
}
