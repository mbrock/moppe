#include <moppe/terrain/geological.hh>

#include <tests/test.hh>

#include <algorithm>
#include <bit>
#include <concepts>
#include <cstdint>
#include <stdexcept>

using namespace moppe;
using namespace moppe::terrain;

namespace {
  template <typename Quantity>
  std::uint64_t column_hash (const std::vector<Quantity>& column) {
    std::uint64_t hash = 1469598103934665603ull;
    for (const Quantity value : column) {
      std::uint32_t bits =
        std::bit_cast<std::uint32_t> (value.numerical_value_in (one));
      for (int byte = 0; byte < 4; ++byte) {
        hash ^= bits & 0xffu;
        hash *= 1099511628211ull;
        bits >>= 8;
      }
    }
    return hash;
  }

  TerrainDomain test_domain (std::size_t side) {
    return TerrainDomain (side, side, 10.0f * u::m, 10.0f * u::m);
  }
}

MOPPE_TEST (geology_is_a_typed_finite_bundle) {
  static_assert (spatial::BundleContains<continent_shape, GeologicalSections>);
  static_assert (spatial::BundleContains<uplift_weight, GeologicalSections>);

  const GeologicalSections geology =
    generate_geology (test_domain (17), make_geological_recipe (123));
  MOPPE_CHECK (geology.size () == 17 * 17);
  MOPPE_CHECK (std::ranges::all_of (
    spatial::get<uplift_weight> (geology), [] (UpliftWeight value) {
      const float weight = value.numerical_value_in (one);
      return weight >= 0.0f && weight <= 1.0f;
    }));
}

MOPPE_TEST (periodic_geology_is_bit_deterministic) {
  const GeologicalRecipe recipe = make_geological_recipe (123);
  const GeologicalSections first = generate_geology (test_domain (65), recipe);
  const GeologicalSections second = generate_geology (test_domain (65), recipe);

  const std::uint64_t continent_hash =
    column_hash (spatial::get<continent_shape> (first));
  const std::uint64_t uplift_hash =
    column_hash (spatial::get<uplift_weight> (first));
  MOPPE_CHECK (continent_hash == 9660056523240721466ull);
  MOPPE_CHECK (uplift_hash == 3395522322764541502ull);
  MOPPE_CHECK (continent_hash ==
               column_hash (spatial::get<continent_shape> (second)));
  MOPPE_CHECK (column_hash (spatial::get<uplift_weight> (first)) ==
               column_hash (spatial::get<uplift_weight> (second)));
}

MOPPE_TEST (geological_recipe_parameters_are_first_class_values) {
  GeologicalRecipe changed_recipe = make_geological_recipe (123);
  const GeologicalSeeds seeds = derive_geological_seeds (123);

  MOPPE_CHECK (changed_recipe.seeds.base == seeds.base);
  MOPPE_CHECK (changed_recipe.seeds.ridge == seeds.ridge);
  MOPPE_CHECK (changed_recipe.seeds.warp == seeds.warp);
  MOPPE_CHECK_NEAR (changed_recipe.warp.amplitude, 0.15f, 1e-6f);
  MOPPE_CHECK (changed_recipe.mountains.cycles == 4);

  changed_recipe.mountains.cycles = 8;
  const GeologicalSections changed =
    generate_geology (test_domain (17), changed_recipe);
  const GeologicalSections original =
    generate_geology (test_domain (17), make_geological_recipe (123));
  MOPPE_CHECK (spatial::get<uplift_weight> (changed) !=
               spatial::get<uplift_weight> (original));
}

MOPPE_TEST (geological_recipe_validation_rejects_bad_mask_edges) {
  GeologicalRecipe recipe = make_geological_recipe (123);
  recipe.blend.mask_high = recipe.blend.mask_low;
  bool threw = false;
  try {
    (void)generate_geology (test_domain (17), recipe);
  } catch (const std::invalid_argument&) {
    threw = true;
  }
  MOPPE_CHECK (threw);
}

MOPPE_TEST (geology_reports_completed_rows) {
  std::size_t completed = 0;
  std::size_t total = 0;
  (void)generate_geology (test_domain (17),
                          make_geological_recipe (123),
                          [&] (std::size_t rows, std::size_t row_count) {
                            completed = std::max (completed, rows);
                            total = row_count;
                          });
  MOPPE_CHECK (completed == 17);
  MOPPE_CHECK (total == 17);
}
