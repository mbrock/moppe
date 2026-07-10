#include <moppe/terrain/field.hh>

#include <stdexcept>
#include <type_traits>
#include <unordered_set>
#include <utility>

namespace moppe::terrain {
  namespace {
    template <typename Operation>
    ScalarField make_field (Operation operation) {
      return ScalarField
	(std::make_shared<expression::Node> (std::move (operation)));
    }

    void visit_nodes
      (const expression::NodePtr& node,
       std::unordered_set<const expression::Node*>& visited) {
      if (!visited.insert (node.get ()).second)
	return;

      std::visit ([&visited] (const auto& operation) {
	using T = std::decay_t<decltype (operation)>;
	if constexpr (std::is_same_v<T, expression::Add>
		      || std::is_same_v<T, expression::Subtract>
		      || std::is_same_v<T, expression::Multiply>) {
	  visit_nodes (operation.left, visited);
	  visit_nodes (operation.right, visited);
	} else if constexpr (std::is_same_v<T, expression::PerlinNoise>
			   || std::is_same_v<T, expression::FbmNoise>
			   || std::is_same_v<T, expression::RidgedNoise>) {
	  visit_nodes (operation.x, visited);
	  visit_nodes (operation.y, visited);
	} else if constexpr (std::is_same_v<T, expression::MultiplyAdd>) {
	  visit_nodes (operation.multiplier, visited);
	  visit_nodes (operation.multiplicand, visited);
	  visit_nodes (operation.addend, visited);
	} else if constexpr (std::is_same_v<T, expression::Sine>
			   || std::is_same_v<T, expression::Smoothstep>) {
	  visit_nodes (operation.operand, visited);
	}
      }, node->operation);
    }
  }

  expression::Node::Node (Operation operation)
    : operation (std::move (operation))
  { }

  ScalarField::ScalarField (expression::NodePtr node)
    : m_node (std::move (node)) {
    if (!m_node)
      throw std::invalid_argument ("a scalar field needs a root node");
  }

  ScalarField constant (float value) {
    return make_field (expression::Constant { value });
  }

  ScalarField coordinate_x () {
    return make_field (expression::CoordinateX { });
  }

  ScalarField coordinate_y () {
    return make_field (expression::CoordinateY { });
  }

  ScalarField sin (const ScalarField& operand) {
    return make_field (expression::Sine { operand.node () });
  }

  ScalarField multiply_add (const ScalarField& multiplier,
			    const ScalarField& multiplicand,
			    const ScalarField& addend) {
    return make_field (expression::MultiplyAdd
      { multiplier.node (), multiplicand.node (), addend.node () });
  }

  ScalarField smoothstep (float edge0, float edge1,
			  const ScalarField& operand) {
    if (!(edge1 > edge0))
      throw std::invalid_argument ("smoothstep edges must increase");
    return make_field
      (expression::Smoothstep { edge0, edge1, operand.node () });
  }

  ScalarField perlin_noise (std::uint32_t seed, const ScalarField& x,
			    const ScalarField& y) {
    return make_field
      (expression::PerlinNoise { seed, x.node (), y.node () });
  }

  namespace {
    void validate_fractal_noise (int octaves, float lacunarity,
				 float gain) {
      if (octaves <= 0)
	throw std::invalid_argument ("fractal noise needs an octave");
      if (!(lacunarity > 0.0f) || !(gain > 0.0f))
	throw std::invalid_argument
	  ("fractal noise lacunarity and gain must be positive");
    }
  }

  ScalarField fbm_noise (std::uint32_t seed, const ScalarField& x,
			 const ScalarField& y, int octaves,
			 float lacunarity, float gain) {
    validate_fractal_noise (octaves, lacunarity, gain);
    return make_field (expression::FbmNoise
      { seed, x.node (), y.node (), octaves, lacunarity, gain });
  }

  ScalarField ridged_noise (std::uint32_t seed, const ScalarField& x,
			    const ScalarField& y, int octaves,
			    float lacunarity, float gain) {
    validate_fractal_noise (octaves, lacunarity, gain);
    return make_field (expression::RidgedNoise
      { seed, x.node (), y.node (), octaves, lacunarity, gain });
  }

  ScalarField operator + (const ScalarField& left,
			  const ScalarField& right) {
    return make_field
      (expression::Add { left.node (), right.node () });
  }

  ScalarField operator * (const ScalarField& left,
			  const ScalarField& right) {
    return make_field
      (expression::Multiply { left.node (), right.node () });
  }

  ScalarField operator - (const ScalarField& left,
			  const ScalarField& right) {
    return make_field
      (expression::Subtract { left.node (), right.node () });
  }

  ScalarField operator + (const ScalarField& left, float right) {
    return left + constant (right);
  }

  ScalarField operator + (float left, const ScalarField& right) {
    return constant (left) + right;
  }

  ScalarField operator * (const ScalarField& left, float right) {
    return left * constant (right);
  }

  ScalarField operator * (float left, const ScalarField& right) {
    return constant (left) * right;
  }

  ScalarField operator - (const ScalarField& left, float right) {
    return left - constant (right);
  }

  ScalarField operator - (float left, const ScalarField& right) {
    return constant (left) - right;
  }

  std::size_t unique_node_count (const ScalarField& field) {
    std::unordered_set<const expression::Node*> visited;
    visit_nodes (field.node (), visited);
    return visited.size ();
  }
}
