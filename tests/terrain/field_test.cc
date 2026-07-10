#include <moppe/terrain/field.hh>

#include <tests/test.hh>

#include <variant>

using namespace moppe::terrain;

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

  MOPPE_CHECK
    (std::holds_alternative<expression::Add> (expression.node ()->operation));
  MOPPE_CHECK (unique_node_count (expression) == 5);
}

MOPPE_TEST (terrain_operations_are_explicit_runtime_variants) {
  const ScalarField x = coordinate_x ();
  const ScalarField difference = x - 0.25f;
  const ScalarField fused = multiply_add (x, constant (2.0f), x);
  const ScalarField mask = smoothstep (0.2f, 0.8f, x);

  MOPPE_CHECK (std::holds_alternative<expression::Subtract>
    (difference.node ()->operation));
  MOPPE_CHECK (std::holds_alternative<expression::MultiplyAdd>
    (fused.node ()->operation));
  MOPPE_CHECK (std::holds_alternative<expression::Smoothstep>
    (mask.node ()->operation));
}
