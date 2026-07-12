#ifndef MOPPE_TERRAIN_FIELD_HH
#define MOPPE_TERRAIN_FIELD_HH

#include <moppe/quantities.hh>

#include <mp-units/framework.h>

#include <cstddef>
#include <cstdint>
#include <memory>
#include <type_traits>
#include <utility>
#include <variant>

namespace moppe::terrain {
  namespace expression {
    struct Node;
    using NodePtr = std::shared_ptr<const Node>;

    struct Constant {
      float value;
    };

    struct CoordinateX {};
    struct CoordinateY {};

    struct Add {
      NodePtr left;
      NodePtr right;
    };

    struct Subtract {
      NodePtr left;
      NodePtr right;
    };

    struct Multiply {
      NodePtr left;
      NodePtr right;
    };

    struct MultiplyAdd {
      NodePtr multiplier;
      NodePtr multiplicand;
      NodePtr addend;
    };

    struct Sine {
      NodePtr operand;
    };

    struct Smoothstep {
      float edge0;
      float edge1;
      NodePtr operand;
    };

    struct PerlinNoise {
      std::uint32_t seed;
      NodePtr x;
      NodePtr y;
    };

    struct FbmNoise {
      std::uint32_t seed;
      NodePtr x;
      NodePtr y;
      int octaves;
      float lacunarity;
      float gain;
    };

    struct RidgedNoise {
      std::uint32_t seed;
      NodePtr x;
      NodePtr y;
      int octaves;
      float lacunarity;
      float gain;
    };

    struct PeriodicFbmNoise {
      std::uint32_t seed;
      NodePtr x;
      NodePtr y;
      int period_x;
      int period_y;
      int octaves;
      int lacunarity;
      float gain;
    };

    struct PeriodicRidgedNoise {
      std::uint32_t seed;
      NodePtr x;
      NodePtr y;
      int period_x;
      int period_y;
      int octaves;
      int lacunarity;
      float gain;
    };

    using Operation = std::variant<Constant,
                                   CoordinateX,
                                   CoordinateY,
                                   Add,
                                   Subtract,
                                   Multiply,
                                   MultiplyAdd,
                                   Sine,
                                   Smoothstep,
                                   PerlinNoise,
                                   FbmNoise,
                                   RidgedNoise,
                                   PeriodicFbmNoise,
                                   PeriodicRidgedNoise>;

    // Nodes are immutable and may be shared by several downstream
    // expressions.  Reusing a ScalarField therefore forms a DAG rather
    // than copying either the expression or any raster data.
    struct Node {
      explicit Node (Operation operation);

      const Operation operation;
    };
  }

  class ScalarField {
  public:
    explicit ScalarField (expression::NodePtr node);

    const expression::NodePtr& node () const noexcept {
      return m_node;
    }

  private:
    expression::NodePtr m_node;
  };

  ScalarField constant (float value);
  ScalarField coordinate_x ();
  ScalarField coordinate_y ();
  ScalarField sin (const ScalarField& operand);
  ScalarField multiply_add (const ScalarField& multiplier,
                            const ScalarField& multiplicand,
                            const ScalarField& addend);
  ScalarField smoothstep (float edge0, float edge1, const ScalarField& operand);
  ScalarField
  perlin_noise (std::uint32_t seed, const ScalarField& x, const ScalarField& y);
  ScalarField fbm_noise (std::uint32_t seed,
                         const ScalarField& x,
                         const ScalarField& y,
                         int octaves,
                         float lacunarity,
                         float gain);
  ScalarField ridged_noise (std::uint32_t seed,
                            const ScalarField& x,
                            const ScalarField& y,
                            int octaves,
                            float lacunarity,
                            float gain);
  ScalarField periodic_fbm_noise (std::uint32_t seed,
                                  const ScalarField& x,
                                  const ScalarField& y,
                                  int period_x,
                                  int period_y,
                                  int octaves,
                                  int lacunarity,
                                  float gain);
  ScalarField periodic_ridged_noise (std::uint32_t seed,
                                     const ScalarField& x,
                                     const ScalarField& y,
                                     int period_x,
                                     int period_y,
                                     int octaves,
                                     int lacunarity,
                                     float gain);

  ScalarField operator+ (const ScalarField& left, const ScalarField& right);
  ScalarField operator* (const ScalarField& left, const ScalarField& right);
  ScalarField operator- (const ScalarField& left, const ScalarField& right);

  ScalarField operator+ (const ScalarField& left, float right);
  ScalarField operator+ (float left, const ScalarField& right);
  ScalarField operator* (const ScalarField& left, float right);
  ScalarField operator* (float left, const ScalarField& right);
  ScalarField operator- (const ScalarField& left, float right);
  ScalarField operator- (float left, const ScalarField& right);

  // Counts node identities, not tree visits, so shared subexpressions
  // are counted once.
  std::size_t unique_node_count (const ScalarField& field);

  // --- typed recipe fields -------------------------------------------
  //
  // The DAG above is deliberately untyped: evaluators lower it to
  // float programs.  Field<QS> layers an mp-units quantity spec over a
  // ScalarField as a phantom type, so recipes compose dimensionally
  // even though every sample is still a plain float.  The recipe
  // domain is scale-free until materialization, so its quantities are
  // dimensionless -- but they are not interchangeable, and distinct
  // dimensionless *kinds* keep them apart at compile time.

  // A position along one axis of the normalized sampling domain
  // (0..1 across the recipe world, cycles once multiplied into a
  // noise lattice).  Not a height, not a weight: adding a mask to a
  // coordinate is a bug the compiler now rejects.
  QUANTITY_SPEC (recipe_coordinate, mp_units::dimensionless, mp_units::is_kind);

  template <auto QS>
  concept FieldSpec =
    mp_units::QuantitySpec<std::remove_const_t<decltype (QS)>>;

  namespace detail {
    // The kind algebra of field products.  A proportion (a mask or
    // blend weight) acts as an operator: weighting a quantity does
    // not change what kind of quantity it is.  Everything else
    // multiplies through the mp-units quantity-spec algebra, where
    // plain dimensionless is the identity.
    template <auto QS1, auto QS2>
    consteval auto field_product () {
      if constexpr (QS1 == moppe::proportion)
        return QS2;
      else if constexpr (QS2 == moppe::proportion)
        return QS1;
      else
        return QS1 * QS2;
    }
  }

  template <auto QS = mp_units::dimensionless>
    requires FieldSpec<QS>
  class Field {
  public:
    static constexpr auto quantity_spec = QS;

    explicit Field (ScalarField field) : m_field (std::move (field)) {}

    // The erasure boundary: evaluators and artifact writers take the
    // untyped ScalarField.  Spelled out (not an implicit conversion)
    // so typed and untyped algebra cannot mix silently.
    const ScalarField& untyped () const noexcept {
      return m_field;
    }

    const expression::NodePtr& node () const noexcept {
      return m_field.node ();
    }

  private:
    ScalarField m_field;
  };

  using DimensionlessField = Field<mp_units::dimensionless>;
  using CoordinateField = Field<recipe_coordinate>;
  using ProportionField = Field<moppe::proportion>;

  // Reinterpret a field as another kind of quantity.  This is the
  // recipe analogue of an explicit quantity cast: crossing between
  // kinds (noise becoming a coordinate displacement, a mask becoming
  // a height) must be visible at the crossing.
  template <auto To, auto From>
    requires FieldSpec<To> && FieldSpec<From>
  Field<To> field_cast (const Field<From>& field) {
    return Field<To> (field.untyped ());
  }

  // Additive structure is kind-preserving and kind-checked.
  template <auto QS>
  Field<QS> operator+ (const Field<QS>& left, const Field<QS>& right) {
    return Field<QS> (left.untyped () + right.untyped ());
  }
  template <auto QS>
  Field<QS> operator- (const Field<QS>& left, const Field<QS>& right) {
    return Field<QS> (left.untyped () - right.untyped ());
  }

  // Multiplication combines quantity specs like quantities do, except
  // that proportions weight without changing the other operand's kind.
  template <auto QS1, auto QS2>
  Field<detail::field_product<QS1, QS2> ()>
  operator* (const Field<QS1>& left, const Field<QS2>& right) {
    return Field<detail::field_product<QS1, QS2> ()> (left.untyped () *
                                                      right.untyped ());
  }

  // Bare numbers scale or offset a field within its own kind.
  template <auto QS>
  Field<QS> operator+ (const Field<QS>& left, float right) {
    return Field<QS> (left.untyped () + right);
  }
  template <auto QS>
  Field<QS> operator+ (float left, const Field<QS>& right) {
    return Field<QS> (left + right.untyped ());
  }
  template <auto QS>
  Field<QS> operator- (const Field<QS>& left, float right) {
    return Field<QS> (left.untyped () - right);
  }
  template <auto QS>
  Field<QS> operator- (float left, const Field<QS>& right) {
    return Field<QS> (left - right.untyped ());
  }
  template <auto QS>
  Field<QS> operator* (const Field<QS>& left, float right) {
    return Field<QS> (left.untyped () * right);
  }
  template <auto QS>
  Field<QS> operator* (float left, const Field<QS>& right) {
    return Field<QS> (left * right.untyped ());
  }

  // Typed primitives.  constant<QS> gives a literal its kind at the
  // point of definition (a warp amplitude is a coordinate
  // displacement, not an abstract number).
  template <auto QS = mp_units::dimensionless>
    requires FieldSpec<QS>
  Field<QS> constant (float value) {
    return Field<QS> (constant (value));
  }

  CoordinateField coordinate_u ();
  CoordinateField coordinate_v ();

  DimensionlessField sin (const DimensionlessField& operand);

  // A smoothstep is a soft threshold: whatever it reads, it emits a
  // blend weight.
  ProportionField
  smoothstep (float edge0, float edge1, const DimensionlessField& operand);

  // Fused multiply-add keeps the spec algebra of a * b + c.
  template <auto QS1, auto QS2>
  Field<detail::field_product<QS1, QS2> ()>
  multiply_add (const Field<QS1>& multiplier,
                const Field<QS2>& multiplicand,
                const Field<detail::field_product<QS1, QS2> ()>& addend) {
    return Field<detail::field_product<QS1, QS2> ()> (multiply_add (
      multiplier.untyped (), multiplicand.untyped (), addend.untyped ()));
  }

  // Noise samples a lattice, so its inputs are domain coordinates by
  // construction; its output is a fresh dimensionless value.
  DimensionlessField perlin_noise (std::uint32_t seed,
                                   const CoordinateField& x,
                                   const CoordinateField& y);
  DimensionlessField fbm_noise (std::uint32_t seed,
                                const CoordinateField& x,
                                const CoordinateField& y,
                                int octaves,
                                float lacunarity,
                                float gain);
  DimensionlessField ridged_noise (std::uint32_t seed,
                                   const CoordinateField& x,
                                   const CoordinateField& y,
                                   int octaves,
                                   float lacunarity,
                                   float gain);
  DimensionlessField periodic_fbm_noise (std::uint32_t seed,
                                         const CoordinateField& x,
                                         const CoordinateField& y,
                                         int period_x,
                                         int period_y,
                                         int octaves,
                                         int lacunarity,
                                         float gain);
  DimensionlessField periodic_ridged_noise (std::uint32_t seed,
                                            const CoordinateField& x,
                                            const CoordinateField& y,
                                            int period_x,
                                            int period_y,
                                            int octaves,
                                            int lacunarity,
                                            float gain);
}

#endif
