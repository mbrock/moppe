#include <moppe/spatial/bundle_operations.hh>

#include <moppe/gfx/math.hh>

#include <tests/test.hh>

#include <concepts>
#include <cstddef>
#include <stdexcept>
#include <utility>

using namespace moppe;

namespace {
  struct ThreeSiteRing {
    using index_type = std::size_t;

    constexpr std::size_t size () const {
      return 3;
    }
    constexpr std::size_t offset (index_type index) const {
      return index;
    }
    constexpr index_type index (std::size_t offset) const {
      return offset;
    }

    template <typename Visitor>
    void visit_neighbourhood (index_type index, Visitor&& visitor) const {
      visitor ((index + 2) % 3, 0.25f);
      visitor ((index + 1) % 3, 0.75f);
    }
  };

  // A ring whose site count belongs to its identity, so two of them can
  // describe different domains.
  struct SizedRing {
    using index_type = std::size_t;

    std::size_t sites = 3;

    std::size_t size () const {
      return sites;
    }
    std::size_t offset (index_type index) const {
      return index;
    }
    index_type index (std::size_t offset) const {
      return offset;
    }

    friend bool operator== (const SizedRing&, const SizedRing&) = default;
  };

  QUANTITY_SPEC (test_displacement, mp_units::isq::length, mp_units::is_kind);
  inline constexpr auto test_velocity =
    test_displacement / mp_units::isq::duration;
  QUANTITY_SPEC (test_density, mp_units::dimensionless);

  using TestDisplacement = quantity<test_displacement[u::m], float>;
  using TestVelocity = quantity<test_velocity[u::m / u::s], float>;
  using TestDensity = quantity<test_density[one], float>;
  using TestBundle =
    spatial::Bundle<ThreeSiteRing, TestDisplacement, TestVelocity>;
}

MOPPE_TEST (bundle_exposes_quantity_columns_and_rows_by_specification) {
  TestBundle bundle (ThreeSiteRing {});
  auto& [displacement, velocity] = bundle;
  displacement[1] = 2.0f * test_displacement[u::m];
  velocity[1] = 3.0f * test_velocity[u::m / u::s];

  auto row = bundle[1];
  MOPPE_CHECK_NEAR (
    spatial::get<test_displacement> (row).numerical_value_in (u::m),
    2.0f,
    1e-6f);
  MOPPE_CHECK_NEAR (
    spatial::get<test_velocity> (row).numerical_value_in (u::m / u::s),
    3.0f,
    1e-6f);
}

MOPPE_TEST (bundle_extend_applies_a_typed_rule_at_every_focus) {
  using DisplacementBundle = spatial::Bundle<ThreeSiteRing, TestDisplacement>;
  DisplacementBundle input (ThreeSiteRing {});
  DisplacementBundle output (ThreeSiteRing {});
  auto& displacement = spatial::get<test_displacement> (input);
  displacement[0] = 1.0f * test_displacement[u::m];
  displacement[1] = 4.0f * test_displacement[u::m];
  displacement[2] = 10.0f * test_displacement[u::m];

  spatial::extend_into (output, input, [] (const auto& site) {
    return spatial::bundle_values (
      spatial::get<test_displacement> (site) +
      spatial::laplacian<test_displacement> (site));
  });

  const auto& result = spatial::get<test_displacement> (output);
  MOPPE_CHECK_NEAR (result[0].numerical_value_in (u::m), 5.5f, 1e-6f);
  MOPPE_CHECK_NEAR (result[1].numerical_value_in (u::m), 7.75f, 1e-6f);
  MOPPE_CHECK_NEAR (result[2].numerical_value_in (u::m), 1.75f, 1e-6f);
}

MOPPE_TEST (joining_bundles_carries_every_column_over_one_domain) {
  spatial::Bundle<SizedRing, TestDisplacement> displacement (SizedRing {});
  spatial::Bundle<SizedRing, TestVelocity> velocity (SizedRing {});
  spatial::Bundle<SizedRing, TestDensity> density (SizedRing {});
  spatial::get<test_displacement> (displacement)[1] =
    2.0f * test_displacement[u::m];
  spatial::get<test_velocity> (velocity)[1] = 3.0f * test_velocity[u::m / u::s];
  spatial::get<test_density> (density)[1] = 4.0f * test_density[one];

  const auto joined = spatial::join (
    std::move (displacement), std::move (velocity), std::move (density));

  static_assert (
    std::same_as<
      std::remove_const_t<decltype (joined)>,
      spatial::Bundle<SizedRing, TestDisplacement, TestVelocity, TestDensity>>);
  MOPPE_CHECK (joined.size () == 3);
  const auto row = joined[1];
  MOPPE_CHECK_NEAR (
    spatial::get<test_displacement> (row).numerical_value_in (u::m),
    2.0f,
    1e-6f);
  MOPPE_CHECK_NEAR (
    spatial::get<test_velocity> (row).numerical_value_in (u::m / u::s),
    3.0f,
    1e-6f);
  MOPPE_CHECK_NEAR (
    spatial::get<test_density> (row).numerical_value_in (one), 4.0f, 1e-6f);
}

MOPPE_TEST (joining_bundles_refuses_domains_that_disagree) {
  spatial::Bundle<SizedRing, TestDisplacement> displacement (SizedRing { 3 });
  spatial::Bundle<SizedRing, TestVelocity> velocity (SizedRing { 4 });

  bool refused = false;
  try {
    spatial::join (std::move (displacement), std::move (velocity));
  } catch (const std::invalid_argument&) {
    refused = true;
  }
  MOPPE_CHECK (refused);
}
