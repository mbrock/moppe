#include <moppe/gfx/math.hh>

#include <tests/test.hh>

#include <mp-units/systems/si.h>

namespace {
  using namespace mp_units;
  using namespace mp_units::si::unit_symbols;

  MOPPE_TEST (mp_units_is_available_to_project_targets) {
    const auto distance = 125 * m;
    const auto duration = 5 * s;
    const auto speed = distance / duration;

    MOPPE_CHECK (speed == 25 * m / s);
  }

  // Vector3D is a conforming mp-units vector representation: indexed
  // access gives it tensor order 1, norm() supplies the magnitude, and
  // scalar algebra makes it scalable, so vector-character quantities
  // can carry it (unit outside, numerical vector inside).
  static_assert (
    RepresentationOf<moppe::Vector3D, quantity_tensor_order::vector>);

  MOPPE_TEST (vector3d_is_a_vector_quantity_representation) {
    const quantity displacement =
      moppe::Vector3D (3, 4, 0) * isq::displacement[m];

    // The magnitude of a vector quantity is a scalar quantity.
    MOPPE_CHECK (displacement.magnitude () == 5 * m);

    // Unit conversions apply to every component.
    const quantity displacement_km =
      moppe::Vector3D (0.003f, 0.004f, 0) * isq::displacement[km];
    MOPPE_CHECK ((displacement_km.in (m) == displacement));

    // Deriving velocity from displacement keeps the vector character
    // and combines dimensions.
    const quantity velocity = displacement / (2 * isq::duration[s]);
    MOPPE_CHECK (velocity.magnitude () == 2.5f * m / s);

    // Products from moppe::dot and moppe::cross combine references.
    const quantity area = moppe::dot (displacement, displacement);
    MOPPE_CHECK (area == 25 * m * m);

    const quantity torque_arm =
      moppe::cross (moppe::Vector3D (1, 0, 0) * isq::displacement[m],
                    moppe::Vector3D (0, 1, 0) * isq::displacement[m]);
    MOPPE_CHECK (torque_arm.numerical_value_in (m * m) ==
                 moppe::Vector3D (0, 0, 1));
  }
}
