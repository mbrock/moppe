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
    generate_geology (test_domain (17), Seed { 123 });
  MOPPE_CHECK (geology.size () == 17 * 17);
  MOPPE_CHECK (std::ranges::all_of (
    spatial::get<uplift_weight> (geology), [] (UpliftWeight value) {
      const float weight = value.numerical_value_in (one);
      return weight >= 0.0f && weight <= 1.0f;
    }));
}

MOPPE_TEST (periodic_geology_is_bit_deterministic) {
  const GeologicalSections first =
    generate_geology (test_domain (65), Seed { 123 });
  const GeologicalSections second =
    generate_geology (test_domain (65), Seed { 123 });

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

MOPPE_TEST (geology_reports_completed_rows) {
  std::size_t completed = 0;
  std::size_t total = 0;
  (void)generate_geology (test_domain (17),
                          Seed { 123 },
                          [&] (std::size_t rows, std::size_t row_count) {
                            completed = std::max (completed, rows);
                            total = row_count;
                          });
  MOPPE_CHECK (completed == 17);
  MOPPE_CHECK (total == 17);
}
