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
		      || std::is_same_v<T, expression::Multiply>) {
	  visit_nodes (operation.left, visited);
	  visit_nodes (operation.right, visited);
	} else if constexpr (std::is_same_v<T, expression::Sine>) {
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
    return left + (-1.0f * right);
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
