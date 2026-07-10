#ifndef MOPPE_TERRAIN_FIELD_HH
#define MOPPE_TERRAIN_FIELD_HH

#include <cstddef>
#include <cstdint>
#include <memory>
#include <variant>

namespace moppe::terrain {
  namespace expression {
    struct Node;
    using NodePtr = std::shared_ptr<const Node>;

    struct Constant {
      float value;
    };

    struct CoordinateX { };
    struct CoordinateY { };

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

    using Operation = std::variant
      <Constant, CoordinateX, CoordinateY, Add, Subtract, Multiply,
       MultiplyAdd, Sine, Smoothstep, PerlinNoise, FbmNoise, RidgedNoise>;

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

    const expression::NodePtr& node () const noexcept
    { return m_node; }

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
  ScalarField smoothstep (float edge0, float edge1,
			  const ScalarField& operand);
  ScalarField perlin_noise (std::uint32_t seed, const ScalarField& x,
			    const ScalarField& y);
  ScalarField fbm_noise (std::uint32_t seed, const ScalarField& x,
			 const ScalarField& y, int octaves,
			 float lacunarity, float gain);
  ScalarField ridged_noise (std::uint32_t seed, const ScalarField& x,
			    const ScalarField& y, int octaves,
			    float lacunarity, float gain);

  ScalarField operator + (const ScalarField& left,
			  const ScalarField& right);
  ScalarField operator * (const ScalarField& left,
			  const ScalarField& right);
  ScalarField operator - (const ScalarField& left,
			  const ScalarField& right);

  ScalarField operator + (const ScalarField& left, float right);
  ScalarField operator + (float left, const ScalarField& right);
  ScalarField operator * (const ScalarField& left, float right);
  ScalarField operator * (float left, const ScalarField& right);
  ScalarField operator - (const ScalarField& left, float right);
  ScalarField operator - (float left, const ScalarField& right);

  // Counts node identities, not tree visits, so shared subexpressions
  // are counted once.
  std::size_t unique_node_count (const ScalarField& field);
}

#endif
