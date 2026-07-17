#include <atelier/space.hh>

#include <moppe/spatial/bundle.hh>

#include <tests/test.hh>

#include <cstddef>
#include <tuple>

using namespace atelier;
using namespace mp_units::si::unit_symbols;
using moppe::spatial::Bundle;
using moppe::spatial::bundle_values;
using moppe::spatial::BundleFocus;
using moppe::spatial::extend_into;
using moppe::spatial::get;
using moppe::spatial::laplacian;

namespace {
  struct AtelierFourSites {
    using index_type = std::size_t;

    [[nodiscard]] constexpr std::size_t size () const {
      return 4;
    }

    [[nodiscard]] constexpr std::size_t offset (index_type index) const {
      return index;
    }

    [[nodiscard]] constexpr index_type index (std::size_t offset) const {
      return offset;
    }
  };

  struct AtelierThreeSiteRing {
    using index_type = std::size_t;

    [[nodiscard]] constexpr std::size_t size () const {
      return 3;
    }

    [[nodiscard]] constexpr std::size_t offset (index_type index) const {
      return index;
    }

    [[nodiscard]] constexpr index_type index (std::size_t offset) const {
      return offset;
    }

    template <typename Visitor>
    constexpr void visit_neighbourhood (index_type index,
                                        Visitor&& visitor) const {
      visitor ((index + 2) % 3, 0.25f);
      visitor ((index + 1) % 3, 0.75f);
    }
  };

  struct AtelierSquaredInfluence {
    template <typename Domain, typename Index, typename Visitor>
    void
    operator() (const Domain& domain, Index index, Visitor&& visitor) const {
      domain.visit_neighbourhood (index, [&] (auto neighbour, auto influence) {
        visitor (neighbour, influence * influence);
      });
    }
  };

  struct AtelierEuclideanSpace {};

  using AtelierTestBundle =
    Bundle<AtelierFourSites, NormalDisplacement, NormalVelocity>;
  using HeightPoint =
    mp_units::quantity_point<isq::height[m],
                             mp_units::default_point_origin (isq::height[m]),
                             Real>;

  template <auto QS, typename B>
  concept HasColumn = requires (B& bundle) { get<QS> (bundle); };

  static_assert (std::tuple_size_v<AtelierTestBundle> == 2);
  static_assert (moppe::spatial::FiniteDomain<AtelierFourSites>);
  static_assert (!moppe::spatial::NeighbourhoodDomain<AtelierFourSites>);
  static_assert (moppe::spatial::NeighbourhoodDomain<AtelierThreeSiteRing>);
  static_assert (moppe::spatial::NeighbourhoodPolicy<AtelierSquaredInfluence,
                                                     AtelierThreeSiteRing>);
  static_assert (!moppe::spatial::FiniteDomain<AtelierEuclideanSpace>);
  static_assert (HasColumn<normal_displacement, AtelierTestBundle>);
  static_assert (HasColumn<normal_velocity, AtelierTestBundle>);
  static_assert (!HasColumn<isq::mass, AtelierTestBundle>);
}

MOPPE_TEST (bundle_exposes_quantity_columns_by_position_and_specification) {
  AtelierTestBundle bundle;
  auto& [displacement, velocity] = bundle;

  displacement[2] = 3.0f * m;
  velocity[2] = 4.0f * m / s;

  MOPPE_CHECK_NEAR (
    get<normal_displacement> (bundle)[2].numerical_value_in (m), 3.0f, 0.001f);
  MOPPE_CHECK_NEAR (
    get<normal_velocity> (bundle)[2].numerical_value_in (m / s), 4.0f, 0.001f);
}

MOPPE_TEST (bundle_row_is_a_non_materialized_quantity_tuple) {
  AtelierTestBundle bundle;
  auto site = bundle[1];
  get<normal_displacement> (site) = 1.5f * m;
  get<normal_velocity> (site) = 2.5f * m / s;

  auto& [displacement, velocity] = site;
  MOPPE_CHECK_NEAR (displacement.numerical_value_in (m), 1.5f, 0.001f);
  MOPPE_CHECK_NEAR (velocity.numerical_value_in (m / s), 2.5f, 0.001f);
}

MOPPE_TEST (bundle_preserves_affine_quantity_point_columns) {
  Bundle<AtelierFourSites, HeightPoint, NormalVelocity> bundle;
  auto& height = get<isq::height> (bundle);
  height[3] = HeightPoint (12.0f * m);

  const auto difference = height[3] - HeightPoint (7.0f * m);
  MOPPE_CHECK_NEAR (difference.numerical_value_in (m), 5.0f, 0.001f);
}

MOPPE_TEST (bundle_extend_applies_a_topological_rule_at_every_focus) {
  Bundle<AtelierThreeSiteRing, NormalDisplacement> input;
  Bundle<AtelierThreeSiteRing, NormalDisplacement> output;
  auto& displacement = get<normal_displacement> (input);
  displacement[0] = 1.0f * m;
  displacement[1] = 4.0f * m;
  displacement[2] = 10.0f * m;

  extend_into (output, input, [] (const auto& site) {
    return bundle_values (get<normal_displacement> (site) +
                          laplacian<normal_displacement> (site));
  });

  const auto& result = get<normal_displacement> (output);
  MOPPE_CHECK_NEAR (result[0].numerical_value_in (m), 5.5f, 0.001f);
  MOPPE_CHECK_NEAR (result[1].numerical_value_in (m), 7.75f, 0.001f);
  MOPPE_CHECK_NEAR (result[2].numerical_value_in (m), 1.75f, 0.001f);
}

MOPPE_TEST (bundle_operation_can_supply_its_own_neighbourhood_policy) {
  Bundle<AtelierThreeSiteRing, NormalDisplacement> field;
  auto& displacement = get<normal_displacement> (field);
  displacement[0] = 1.0f * m;
  displacement[1] = 4.0f * m;
  displacement[2] = 10.0f * m;

  const auto result = laplacian<normal_displacement> (
    BundleFocus (field, std::size_t { 0 }), AtelierSquaredInfluence {});
  MOPPE_CHECK_NEAR (result.numerical_value_in (m), 2.25f, 0.001f);
}

MOPPE_TEST (bundle_laplacian_preserves_vector_quantity_representation) {
  Bundle<AtelierThreeSiteRing, Displacement> field;
  auto& displacement = get<isq::displacement> (field);
  displacement[0] = Displacement (Vec3 { 1, 2, 3 } * m);
  displacement[1] = Displacement (Vec3 { 4, 6, 8 } * m);
  displacement[2] = Displacement (Vec3 { 10, 12, 14 } * m);

  const auto result =
    laplacian<isq::displacement> (BundleFocus (field, std::size_t { 0 }));
  const auto vector = result.numerical_value_in (m);
  MOPPE_CHECK_NEAR (vector[0], 4.5f, 0.001f);
  MOPPE_CHECK_NEAR (vector[1], 5.5f, 0.001f);
  MOPPE_CHECK_NEAR (vector[2], 6.5f, 0.001f);
}

MOPPE_TEST (bundle_laplacian_turns_affine_points_into_differences) {
  Bundle<AtelierThreeSiteRing, HeightPoint> field;
  auto& height = get<isq::height> (field);
  height[0] = HeightPoint (1.0f * m);
  height[1] = HeightPoint (4.0f * m);
  height[2] = HeightPoint (10.0f * m);

  const auto result =
    laplacian<isq::height> (BundleFocus (field, std::size_t { 0 }));
  static_assert (mp_units::Quantity<decltype (result)>);
  MOPPE_CHECK_NEAR (result.numerical_value_in (m), 4.5f, 0.001f);
}
