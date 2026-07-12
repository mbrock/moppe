#ifndef MOPPE_TERRAIN_FIELD_HH
#define MOPPE_TERRAIN_FIELD_HH

#include <moppe/terrain/types.hh>

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
      Seed seed;
      NodePtr x;
      NodePtr y;
    };

    struct FbmNoise {
      Seed seed;
      NodePtr x;
      NodePtr y;
      int octaves;
      float lacunarity;
      float gain;
    };

    struct RidgedNoise {
      Seed seed;
      NodePtr x;
      NodePtr y;
      int octaves;
      float lacunarity;
      float gain;
    };

    struct PeriodicFbmNoise {
      Seed seed;
      NodePtr x;
      NodePtr y;
      int period_x;
      int period_y;
      int octaves;
      int lacunarity;
      float gain;
    };

    struct PeriodicRidgedNoise {
      Seed seed;
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
  perlin_noise (Seed seed, const ScalarField& x, const ScalarField& y);
  ScalarField fbm_noise (Seed seed,
                         const ScalarField& x,
                         const ScalarField& y,
                         int octaves,
                         float lacunarity,
                         float gain);
  ScalarField ridged_noise (Seed seed,
                            const ScalarField& x,
                            const ScalarField& y,
                            int octaves,
                            float lacunarity,
                            float gain);
  ScalarField periodic_fbm_noise (Seed seed,
                                  const ScalarField& x,
                                  const ScalarField& y,
                                  int period_x,
                                  int period_y,
                                  int octaves,
                                  int lacunarity,
                                  float gain);
  ScalarField periodic_ridged_noise (Seed seed,
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

  // --- typed field expressions ---------------------------------------
  //
  // The DAG above is deliberately untyped: evaluators lower it to
  // float programs.  Field<QS> layers an mp-units quantity spec over a
  // ScalarField as a phantom type, so expressions compose dimensionally
  // even though every sample is still a plain float.  The field is
  // scale-free until materialization, so its quantities are
  // dimensionless -- but they are not interchangeable, and distinct
  // dimensionless *kinds* keep them apart at compile time.

  // A position along one axis of the normalized sampling domain
  // (0..1 across the procedural field, cycles once multiplied into a
  // noise lattice).  Not a height, not a weight: adding a mask to a
  // coordinate is a bug the compiler now rejects.
  QUANTITY_SPEC (field_coordinate, mp_units::dimensionless, mp_units::is_kind);
  QUANTITY_SPEC (relative_elevation,
                 mp_units::dimensionless,
                 mp_units::is_kind);
  QUANTITY_SPEC (normalized_sample, mp_units::dimensionless, mp_units::is_kind);

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
      else if constexpr (QS1 == moppe::noise_signal) {
        if constexpr (QS2 == mp_units::dimensionless ||
                      QS2 == moppe::noise_signal)
          return moppe::noise_signal;
        else
          return QS2;
      } else if constexpr (QS2 == moppe::noise_signal) {
        if constexpr (QS1 == mp_units::dimensionless)
          return moppe::noise_signal;
        else
          return QS1;
      } else
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
  using CoordinateField = Field<field_coordinate>;
  using NoiseField = Field<moppe::noise_signal>;
  using ProportionField = Field<moppe::proportion>;
  using RelativeElevationField = Field<relative_elevation>;

  // Reinterpret a field as another kind of quantity.  This is the
  // field analogue of an explicit quantity cast: crossing between
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
  template <auto QS>
  ProportionField
  smoothstep (float edge0, float edge1, const Field<QS>& operand) {
    return ProportionField (smoothstep (edge0, edge1, operand.untyped ()));
  }

  // Fused multiply-add keeps the spec algebra of a * b + c.
  template <auto QS1, auto QS2>
  Field<detail::field_product<QS1, QS2> ()>
  multiply_add (const Field<QS1>& multiplier,
                const Field<QS2>& multiplicand,
                const Field<detail::field_product<QS1, QS2> ()>& addend) {
    return Field<detail::field_product<QS1, QS2> ()> (multiply_add (
      multiplier.untyped (), multiplicand.untyped (), addend.untyped ()));
  }

  // Noise samples a lattice, so its inputs are domain coordinates and its
  // output is explicitly a procedural signal rather than generic scalar data.
  NoiseField
  perlin_noise (Seed seed, const CoordinateField& x, const CoordinateField& y);
  NoiseField fbm_noise (Seed seed,
                        const CoordinateField& x,
                        const CoordinateField& y,
                        int octaves,
                        float lacunarity,
                        float gain);
  NoiseField ridged_noise (Seed seed,
                           const CoordinateField& x,
                           const CoordinateField& y,
                           int octaves,
                           float lacunarity,
                           float gain);
  NoiseField periodic_fbm_noise (Seed seed,
                                 const CoordinateField& x,
                                 const CoordinateField& y,
                                 int period_x,
                                 int period_y,
                                 int octaves,
                                 int lacunarity,
                                 float gain);
  NoiseField periodic_ridged_noise (Seed seed,
                                    const CoordinateField& x,
                                    const CoordinateField& y,
                                    int period_x,
                                    int period_y,
                                    int octaves,
                                    int lacunarity,
                                    float gain);
}

#endif
