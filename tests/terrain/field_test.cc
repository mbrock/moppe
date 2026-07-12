#include <moppe/terrain/cpu_evaluator.hh>
#include <moppe/terrain/field.hh>

#include <tests/test.hh>

#include <concepts>
#include <utility>
#include <variant>

using namespace moppe::terrain;

// The typed field algebra: kinds are phantom types over the same
// float DAG, checked entirely at compile time.
namespace {
  template <typename A, typename B>
  concept addable = requires (A a, B b) { a + b; };
  template <typename A, typename B>
  concept subtractable = requires (A a, B b) { a - b; };
  template <typename F>
  concept noise_coordinate =
    requires (F f) { perlin_noise (moppe::terrain::Seed { 1 }, f, f); };

  // Adding or subtracting across kinds is rejected: a mask is not a
  // coordinate.
  static_assert (!addable<CoordinateField, DimensionlessField>);
  static_assert (!subtractable<CoordinateField, DimensionlessField>);
  static_assert (addable<CoordinateField, CoordinateField>);

  // Bare numbers scale within a kind.
  static_assert (
    std::same_as<decltype (std::declval<CoordinateField> () * 2.0f),
                 CoordinateField>);
  static_assert (
    std::same_as<decltype (0.5f + std::declval<CoordinateField> ()),
                 CoordinateField>);

  // Dimensionless is the identity of the kind algebra: scaling a
  // coordinate by a pure-number field stays in coordinate space.
  static_assert (std::same_as<decltype (std::declval<CoordinateField> () *
                                        std::declval<DimensionlessField> ()),
                              CoordinateField>);

  // Proportions are operators, not factors: weighting a field of any
  // kind leaves its kind alone, the complement of a weight is a
  // weight, and a weight of a weight is a weight.
  static_assert (std::same_as<decltype (std::declval<DimensionlessField> () *
                                        std::declval<ProportionField> ()),
                              DimensionlessField>);
  static_assert (std::same_as<decltype (std::declval<CoordinateField> () *
                                        std::declval<ProportionField> ()),
                              CoordinateField>);
  static_assert (
    std::same_as<decltype (1.0f - std::declval<ProportionField> ()),
                 ProportionField>);
  static_assert (std::same_as<decltype (std::declval<ProportionField> () *
                                        std::declval<ProportionField> ()),
                              ProportionField>);

  // But a weight still cannot be *added* to what it weights.
  static_assert (!addable<DimensionlessField, ProportionField>);

  // Noise demands coordinates, not arbitrary samples.
  static_assert (!noise_coordinate<DimensionlessField>);
  static_assert (!noise_coordinate<NoiseField>);
  static_assert (noise_coordinate<CoordinateField>);

  static_assert (!addable<NoiseField, CoordinateField>);
  static_assert (std::same_as<decltype (std::declval<CoordinateField> () *
                                        std::declval<NoiseField> ()),
                              CoordinateField>);
  static_assert (
    std::same_as<decltype (std::declval<RelativeElevationField> () *
                           std::declval<ProportionField> ()),
                 RelativeElevationField>);
}

MOPPE_TEST (typed_fields_share_the_untyped_dag) {
  const CoordinateField u = coordinate_u ();
  const CoordinateField shifted = u * 3.0f + 0.25f;
  const NoiseField noise =
    perlin_noise (moppe::terrain::Seed { 7 }, shifted, coordinate_v ());

  // The phantom kind adds no nodes: the DAG below is the same
  // expression the untyped combinators would have built.
  MOPPE_CHECK (unique_node_count (noise.untyped ()) == 7);
  MOPPE_CHECK (
    std::holds_alternative<expression::PerlinNoise> (noise.node ()->operation));

  // field_cast crosses kinds without touching the DAG.
  const CoordinateField as_offset = field_cast<field_coordinate> (noise);
  MOPPE_CHECK (as_offset.node () == noise.node ());
}

MOPPE_TEST (typed_materialization_preserves_meaning_until_normalization) {
  const RelativeElevationField relief =
    field_cast<relative_elevation> (perlin_noise (
      moppe::terrain::Seed { 9 }, coordinate_u (), coordinate_v ()));
  const RelativeElevationRaster raster =
    materialize (CpuEvaluator (), relief, { .width = 4, .height = 3 });

  static_assert (
    mp_units::QuantityOf<decltype (raster.sample (0, 0)), relative_elevation>);
  MOPPE_CHECK (raster.values ().size () == 12);

  const NormalizedRaster normalized = normalize (raster);
  static_assert (mp_units::QuantityOf<decltype (normalized.sample (0, 0)),
                                      normalized_sample>);
  MOPPE_CHECK (normalized.untyped ().min_value () == 0.0f);
  MOPPE_CHECK (normalized.untyped ().max_value () == 1.0f);
}

MOPPE_TEST (reused_fields_form_a_dag) {
  const ScalarField x = coordinate_x ();
  const ScalarField doubled = x + x;
  const auto& add = std::get<expression::Add> (doubled.node ()->operation);

  MOPPE_CHECK (add.left == x.node ());
  MOPPE_CHECK (add.right == x.node ());
  MOPPE_CHECK (unique_node_count (doubled) == 2);
}

MOPPE_TEST (arithmetic_builds_runtime_node_variants) {
  const ScalarField x = coordinate_x ();
  const ScalarField expression = 0.5f + 2.0f * x;

  MOPPE_CHECK (
    std::holds_alternative<expression::Add> (expression.node ()->operation));
  MOPPE_CHECK (unique_node_count (expression) == 5);
}

MOPPE_TEST (terrain_operations_are_explicit_runtime_variants) {
  const ScalarField x = coordinate_x ();
  const ScalarField difference = x - 0.25f;
  const ScalarField fused = multiply_add (x, constant (2.0f), x);
  const ScalarField mask = smoothstep (0.2f, 0.8f, x);

  MOPPE_CHECK (std::holds_alternative<expression::Subtract> (
    difference.node ()->operation));
  MOPPE_CHECK (
    std::holds_alternative<expression::MultiplyAdd> (fused.node ()->operation));
  MOPPE_CHECK (
    std::holds_alternative<expression::Smoothstep> (mask.node ()->operation));
}
