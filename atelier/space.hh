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
  namespace si = mp_units::si;
  namespace isq = mp_units::isq;
  namespace angular = mp_units::angular;

  using mp_units::absolute_point_origin;
  using mp_units::quantity;
  using mp_units::quantity_point;
  using mp_units::quantity_spec;

  using Real = float;
  using Vec3 = mp_units::utility::cartesian_vector<Real, 3>;

  using Length = quantity<si::metre, Real>;
  using Duration = quantity<si::second, Real>;
  using Angle = quantity<angular::radian, Real>;
  using AngularRate = quantity<angular::radian / si::second, Real>;
  using Wavenumber = quantity<angular::radian / si::metre, Real>;

  // A dimensionless diagnostic derived from relative deformation of nearby
  // tiles.  It is intentionally not called strain or stress: those names are
  // reserved for physical constitutive quantities in the ligament model.
  inline constexpr struct deformation_reading
      : quantity_spec<mp_units::dimensionless> {
  } deformation_reading;
  using DeformationReading = quantity<deformation_reading[mp_units::one], Real>;

  // The one origin every point of the scene is measured from.
  inline constexpr struct scene_t final
      : absolute_point_origin<isq::displacement> {
  } scene;

  using Displacement = quantity<isq::displacement[si::metre], Vec3>;
  using Point = quantity_point<isq::displacement[si::metre], scene, Vec3>;

  // Directions are dimensionless unit vectors; a direction scaled by a
  // length is a displacement.  These three are the scene's compass.
  inline constexpr Vec3 east { 1, 0, 0 };
  inline constexpr Vec3 up { 0, 1, 0 };
  inline constexpr Vec3 north { 0, 0, 1 };

  // The horizontal direction at a given bearing away from north.
  [[nodiscard]] inline Vec3 radial (Angle bearing) {
    return Vec3 { angular::sin (bearing), 0.0f, angular::cos (bearing) };
  }

  // The unit direction of a displacement.
  [[nodiscard]] inline Vec3 direction (const Displacement& d) {
    return Vec3 (d / d.magnitude ());
  }

  // The length of a displacement's shadow along a unit direction.
  // Implemented on the representation, since mp-units defines
  // scalar_product between two vectors, not between a vector quantity
  // and a bare direction; no number escapes, the projection is a
  // Length again.
  [[nodiscard]] inline Length along (const Displacement& d, const Vec3& dir) {
    return scalar_product (d.numerical_value_in (si::metre), dir) * si::metre;
  }

  // --- The GPU bridge ---------------------------------------------
  //
  // Shaders speak raw single-precision numbers measured in metres and
  // radians.  These functions are the only place in the atelier where
  // a quantity gives up its units.

  [[nodiscard]] inline Real in_metres (Length length) {
    return length.numerical_value_in (si::metre);
  }

  [[nodiscard]] inline Real in_radians (Angle angle) {
    return angle.numerical_value_in (angular::radian);
  }

  [[nodiscard]] inline simd_float3 as_simd (const Vec3& v) {
    return { v[0], v[1], v[2] };
  }

  [[nodiscard]] inline simd_float3 in_metres (const Displacement& d) {
    const auto [x, y, z] = d.numerical_value_in (si::metre);
    return { x, y, z };
  }

  [[nodiscard]] inline simd_float3 in_metres (const Point& p) {
    return in_metres (p - scene);
  }
}
