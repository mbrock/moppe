#pragma once

#include <mp-units/framework.h>
#include <mp-units/systems/angular.h>
#include <mp-units/systems/isq.h>
#include <mp-units/systems/si.h>
#include <mp-units/systems/si/chrono.h>
#include <mp-units/utility/cartesian_vector.h>

#include <simd/simd.h>

// The atelier's model space: a three-dimensional metric stage.
//
// Everything the scene reasons about is a genuine quantity: lengths in
// metres, time in seconds, and angles in the strong angular system, so
// a bare number can never pose as an angle.  Spatial values divide in
// the affine style into points (places) and displacements (differences
// of places); points admit no addition or scaling, so the compiler
// keeps the geometry honest.  Bare floats exist only past the GPU
// bridge at the bottom of this file.

namespace atelier {
  using Real = float;
  using Vec3 = mp_units::utility::cartesian_vector<Real, 3>;

  using Length = mp_units::quantity<mp_units::si::metre, Real>;
  using Duration = mp_units::quantity<mp_units::si::second, Real>;
  using Angle = mp_units::quantity<mp_units::angular::radian, Real>;
  using AngularRate =
    mp_units::quantity<mp_units::angular::radian / mp_units::si::second, Real>;

  // The one origin every point of the scene is measured from.
  inline constexpr struct scene_t final
      : mp_units::absolute_point_origin<mp_units::isq::displacement> {
  } scene;

  using Displacement =
    mp_units::quantity<mp_units::isq::displacement[mp_units::si::metre], Vec3>;
  using Point =
    mp_units::quantity_point<mp_units::isq::displacement[mp_units::si::metre],
                             scene,
                             Vec3>;

  // Directions are dimensionless unit vectors; a direction scaled by a
  // length is a displacement.  These three are the scene's compass.
  inline constexpr Vec3 east { 1, 0, 0 };
  inline constexpr Vec3 up { 0, 1, 0 };
  inline constexpr Vec3 north { 0, 0, 1 };

  // The horizontal direction at a given bearing away from north.
  [[nodiscard]] inline Vec3 radial (Angle bearing) {
    using mp_units::angular::cos, mp_units::angular::sin;
    return Vec3 { sin (bearing), 0.0f, cos (bearing) };
  }

  // The unit direction of a displacement.
  [[nodiscard]] inline Vec3 direction (const Displacement& d) {
    return Vec3 (d / d.magnitude ());
  }

  // --- The GPU bridge ---------------------------------------------
  //
  // Shaders speak raw single-precision numbers measured in metres and
  // radians.  These functions are the only place in the atelier where
  // a quantity gives up its units.

  [[nodiscard]] inline Real in_metres (Length length) {
    return length.numerical_value_in (mp_units::si::metre);
  }

  [[nodiscard]] inline Real in_radians (Angle angle) {
    return angle.numerical_value_in (mp_units::angular::radian);
  }

  [[nodiscard]] inline simd_float3 as_simd (const Vec3& v) {
    return { v[0], v[1], v[2] };
  }

  [[nodiscard]] inline simd_float3 in_metres (const Displacement& d) {
    const auto [x, y, z] = d.numerical_value_in (mp_units::si::metre);
    return { x, y, z };
  }

  [[nodiscard]] inline simd_float3 in_metres (const Point& p) {
    return in_metres (p - scene);
  }
}
