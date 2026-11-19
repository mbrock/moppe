#ifndef MOPPE_MATH_HH
#define MOPPE_MATH_HH

#include <moppe/correct_math.hh>
#include <moppe/gfx/vector.hh>
#include <moppe/quantities.hh>

#include <mp-units/framework.h>
#include <mp-units/math.h>

#include <cmath>
#include <cstddef>
#include <iostream>
#include <numbers>
#include <tuple>
#include <type_traits>

namespace moppe {
  using namespace mp_units;

  // Quantity literals: 90 * u::deg, 2.5f * u::m, 60 * u::Hz.
  namespace u {
    using namespace mp_units::si::unit_symbols;
  }

  inline constexpr float PI = std::numbers::pi_v<float>;
  inline constexpr float PI2 = 2.0f * PI;

  using seconds_t = quantity<si::second, float>;
  using degrees_t = quantity<si::degree, float>;
  // radians_t is in moppe/gfx/vector.hh, which the quaternion needs it in.
  using magnitude_t = quantity<one, float>;
  using speed_t = quantity<si::metre / si::second, float>;
  // A speed as a speedometer reads it.
  using kilometres_per_hour_t = quantity<si::kilo<si::metre> / si::hour, float>;
  using velocity_component_t =
    quantity<isq::velocity[si::metre / si::second], float>;
  using acceleration_component_t =
    quantity<isq::acceleration[si::metre / pow<2> (si::second)], float>;
  using newtons_t = quantity<si::newton, float>;
  using watts_t = quantity<si::watt, float>;
  using kilograms_t = quantity<si::kilogram, float>;
  using metres_per_second_squared_t =
    quantity<si::metre / pow<2> (si::second), float>;
  using pascals_t = quantity<si::pascal, float>;
  using newton_metres_t = quantity<si::newton * si::metre, float>;
  using kilograms_per_cubic_metre_t =
    quantity<si::kilogram / pow<3> (si::metre), float>;
  using kilogram_square_metres_t =
    quantity<si::kilogram * pow<2> (si::metre), float>;
  using radians_per_second_t = quantity<si::radian / si::second, float>;
  // The hour of the day on a clock, as the sky reads it.
  using hours_t = quantity<si::hour, float>;
  // Rates and per-length coefficients that are not decays: a heading
  // swung per second per radian of input, a drag per metre of speed.
  using per_second_t = quantity<one / si::second, float>;
  using per_metre_t = quantity<one / si::metre, float>;

  // Dimension-one kinds from moppe/quantities.hh.
  using proportion_t = quantity<proportion[one], float>;
  using probability_t = quantity<probability[one], float>;
  using control_signal_t = quantity<control_signal[one], float>;
  using noise_signal_t = quantity<noise_signal[one], float>;
  using grade_t = quantity<grade[one], float>;
  using blend_weight_t = quantity<blend_weight[one], float>;
  using scale_factor_t = quantity<scale_factor[one], float>;
  using froude_number_t = quantity<froude_number[one], float>;
  using damping_ratio_t = quantity<damping_ratio[one], float>;

  // The ISQ's coefficients of contact and of flow: a tyre's grip, what
  // rolling costs a loaded wheel as a share of its load, a body's drag,
  // what share of an engine's power reaches the water or the ground.
  using friction_coefficient_t =
    quantity<isq::static_friction_coefficient[one], float>;
  using kinetic_friction_t = quantity<isq::kinetic_friction_factor[one], float>;
  using rolling_resistance_t =
    quantity<isq::rolling_resistance_factor[one], float>;
  using drag_coefficient_t = quantity<isq::drag_coefficient[one], float>;
  using efficiency_t = quantity<isq::mechanical_efficiency[one], float>;

  using scaling_exponent_t = quantity<scaling_exponent[one], float>;
  using aspect_ratio_t = quantity<aspect_ratio[one], float>;
  using opacity_t = quantity<opacity[one], float>;

  // Lengths on the screen, in display points (moppe/quantities.hh):
  // 12.0f * u::pt.
  using points_t = quantity<display_point, float>;
  namespace u {
    inline constexpr auto pt = display_point;
  }
  // Lengths in a raster's pixels (moppe/quantities.hh): 1.5f * u::px.
  using pixels_t = quantity<pixel, float>;
  namespace u {
    inline constexpr auto px = pixel;
  }

  // Angles with a role (moppe/quantities.hh).
  using heading_t = quantity<heading[si::radian], float>;
  using inclination_t = quantity<inclination[si::radian], float>;

  // Ages and spans of the world's history.
  using age_t = quantity<age[astronomy::Julian_year], float>;

  // The inverse time constant of exponential decay (ISQ calls the
  // quantity a damping coefficient).  Every 1 - exp (-k * dt)
  // smoothing constant in the codebase is one of these; spelled
  // 3.1f / u::s at definition sites.
  using damping_t = quantity<one / si::second, float>;

  // The pull of the ground, the one every machine and the water fall by.
  // (The walker falls faster, by its own, for the feel of a jump.)
  inline constexpr metres_per_second_squared_t standard_gravity =
    9.81f * si::metre / pow<2> (si::second);
  using frequency_t = quantity<si::hertz, float>;
  using attenuation_t = quantity<isq::attenuation[one / si::metre], float>;

  inline constexpr meters_t one_meter = 1.0f * si::metre;

  inline seconds_t seconds (float value) {
    return value * si::second;
  }

  // Boundary escapes into unit-blind float code (rendering, raster
  // grids).  Each names the unit the float is measured in.
  inline float seconds_value (seconds_t q) {
    return q.numerical_value_in (si::second);
  }
  inline float radians_value (radians_t q) {
    return q.numerical_value_in (si::radian);
  }
  inline float scalar_value (magnitude_t q) {
    return q.numerical_value_in (one);
  }
  inline float newtons_value (newtons_t q) {
    return q.numerical_value_in (si::newton);
  }
  inline float meters_value (meters_t q) {
    return q.numerical_value_in (si::metre);
  }
  // A host's reading in points, as a length on the screen.
  inline points_t display_points (float value) {
    return value * display_point;
  }
  inline float points_value (points_t q) {
    return q.numerical_value_in (display_point);
  }
  inline float pixels_value (pixels_t q) {
    return q.numerical_value_in (pixel);
  }
  inline float opacity_value (opacity_t q) {
    return q.numerical_value_in (one);
  }
  inline float kilograms_value (kilograms_t q) {
    return q.numerical_value_in (si::kilogram);
  }
  inline float speed_value (speed_t q) {
    return q.numerical_value_in (si::metre / si::second);
  }
  inline float newton_metres_value (newton_metres_t q) {
    return q.numerical_value_in (si::newton * si::metre);
  }
  inline float moment_value (kilogram_square_metres_t q) {
    return q.numerical_value_in (si::kilogram * pow<2> (si::metre));
  }
  inline float per_second_value (per_second_t q) {
    return q.numerical_value_in (one / si::second);
  }
  inline float per_metre_value (per_metre_t q) {
    return q.numerical_value_in (one / si::metre);
  }

  inline float attenuation_value (attenuation_t q) {
    return q.numerical_value_in (one / si::metre);
  }

  // Frame-rate-independent exponential decay: the surviving
  // proportion of a quantity after dt at decay rate k.  The typed rate
  // makes the exponent's dimensionlessness (frame-rate independence) a
  // compile-time fact.  A length or a duration scales by it directly;
  // unit-blind floats take numerical_value_in (one).
  inline proportion_t decay (damping_t k, seconds_t dt) {
    return cr::exp (-scalar_value (k * dt)) * proportion[one];
  }

  // The complementary smoothing weight: how far toward a target a
  // low-pass filter moves during dt when it converges at rate k.  A
  // blend weight is its own kind, so a step toward a typed target is
  // named back into the target's type: radians_t ((to - x) * alpha).
  inline blend_weight_t smoothing_alpha (damping_t k, seconds_t dt) {
    return (1.0f - decay (k, dt).numerical_value_in (one)) * blend_weight[one];
  }

  // Trig on typed angles.  Degrees convert implicitly at the call
  // (float representation), so sin (45 * u::deg) works; the plain
  // float result feeds unit-blind geometry code.
  inline float sin (radians_t a) {
    return cr::sin (radians_value (a));
  }
  inline float cos (radians_t a) {
    return cr::cos (radians_value (a));
  }
  inline float tan (radians_t a) {
    return cr::tan (radians_value (a));
  }

  // The same trig, and the plane's measures, rounded as the platform's
  // library rounds them (std::sinf, std::hypotf, ...), for presentation
  // code -- meshes, posing, the camera, the renderer -- that has always
  // called std:: and must keep its bits when it is typed. Generation
  // never uses these: it calls the correctly rounded ones above, which
  // agree on every platform (docs/determinism.md). Float only, as the
  // calls they replace were; call them qualified, libm::sin (a).
  namespace libm {
    inline float sin (radians_t a) {
      return std::sin (radians_value (a));
    }
    inline float cos (radians_t a) {
      return std::cos (radians_value (a));
    }
    inline float tan (radians_t a) {
      return std::tan (radians_value (a));
    }
    // The angle of (x, y), from two lengths (or any two quantities of one
    // kind and unit) or two plain numbers of one frame.
    template <Quantity Q>
      requires std::same_as<typename Q::rep, float>
    inline radians_t atan2 (Q y, Q x) {
      return std::atan2 (y.numerical_value_in (Q::unit),
                         x.numerical_value_in (Q::unit)) *
             si::radian;
    }
    inline radians_t atan2 (float y, float x) {
      return std::atan2 (y, x) * si::radian;
    }
    template <Quantity Q>
      requires std::same_as<typename Q::rep, float>
    inline Q hypot (Q a, Q b) {
      return Q (std::hypot (a.numerical_value_in (Q::unit),
                            b.numerical_value_in (Q::unit)) *
                Q::reference);
    }
    inline radians_t asin (float value) {
      return std::asin (value) * si::radian;
    }
    inline radians_t acos (float value) {
      return std::acos (value) * si::radian;
    }
  }

  template <typename T>
  void clamp (T& x, const T& min, const T& max) {
    if (x > max)
      x = max;
    else if (x < min)
      x = min;
  }

  template <typename T>
  T max (T a, T b) {
    return a > b ? a : b;
  }

  template <typename T>
  T min (T a, T b) {
    return a < b ? a : b;
  }

  template <typename T>
  T linear_interpolate (const T& x, const T& y, const T& alpha) {
    return (1 - alpha) * x + alpha * y;
  }

  inline Vec3 linear_vector_interpolate (const Vec3& from,
                                         const Vec3& to,
                                         blend_weight_t alpha) {
    const float weight = alpha.numerical_value_in (one);
    return (1.0f - weight) * from + weight * to;
  }

  template <Quantity Q1, Quantity Q2>
  auto dot (const Q1& left, const Q2& right) {
    return scalar_product (left.numerical_value_in (Q1::unit),
                           right.numerical_value_in (Q2::unit)) *
           (Q1::reference * Q2::reference);
  }

  template <Quantity Q1, Quantity Q2>
  auto cross (const Q1& left, const Q2& right) {
    return vector_product (left.numerical_value_in (Q1::unit),
                           right.numerical_value_in (Q2::unit)) *
           (Q1::reference * Q2::reference);
  }

  // Physical spatial vectors keep one unit around one numerical vector.  The
  // simulation can migrate fields to these types incrementally without ever
  // constructing a vector whose individual elements are quantities.
  // A place in the world is a point, measured from the world's origin:
  // two of them do not add, and their difference is a position vector.
  using position_t =
    quantity_point<isq::position_vector[si::metre],
                   default_point_origin (isq::position_vector[si::metre]),
                   Vec3>;
  using displacement_t = quantity<isq::displacement[si::metre], Vec3>;
  using spatial_extent_t = quantity<spatial_extent[si::metre], Vec3>;
  using velocity_t = quantity<isq::velocity[si::metre / si::second], Vec3>;
  using acceleration_t =
    quantity<isq::acceleration[si::metre / pow<2> (si::second)], Vec3>;
  using force_t = quantity<isq::force[si::newton], Vec3>;
  using impulse_t = quantity<isq::impulse[si::newton * si::second], Vec3>;

  inline position_t position (const Vec3& value) {
    return position_t (value * isq::position_vector[si::metre]);
  }

  inline displacement_t displacement (const Vec3& value) {
    return value * isq::displacement[si::metre];
  }

  inline const Vec3& displacement_value (const displacement_t& value) {
    return value.numerical_value_ref_in (si::metre);
  }

  inline force_t force (const Vec3& value) {
    return value * isq::force[si::newton];
  }

  inline const Vec3& force_value (const force_t& value) {
    return value.numerical_value_ref_in (si::newton);
  }

  inline impulse_t impulse (const Vec3& value) {
    return value * isq::impulse[si::newton * si::second];
  }

  inline const Vec3& impulse_value (const impulse_t& value) {
    return value.numerical_value_ref_in (si::newton * si::second);
  }

  inline velocity_t velocity (const Vec3& value) {
    return value * isq::velocity[si::metre / si::second];
  }

  inline spatial_extent_t spatial_extent_in_metres (const Vec3& value) {
    return value * spatial_extent[si::metre];
  }

  inline const Vec3& extent_value (const spatial_extent_t& value) {
    return value.numerical_value_ref_in (si::metre);
  }

  inline const Vec3& position_value (const position_t& value) {
    return value.quantity_ref_from (value.point_origin)
      .numerical_value_ref_in (si::metre);
  }

  inline Vec3& position_value (position_t& value) {
    return value.quantity_ref_from (value.point_origin)
      .numerical_value_ref_in (si::metre);
  }

  inline const Vec3& velocity_value (const velocity_t& value) {
    return value.numerical_value_ref_in (si::metre / si::second);
  }

  inline Vec3& velocity_value (velocity_t& value) {
    return value.numerical_value_ref_in (si::metre / si::second);
  }

  using angular_velocity_t =
    quantity<isq::angular_velocity[si::radian / si::second], Vec3>;

  // Which way, as a unit vector: a typed direction says it is not a place
  // or a distance. `direction_of` normalizes; `along` walks a distance
  // that way. Local direction arithmetic can stay on Vec3 inside a
  // function; signatures and members carry the type.
  using direction_t = quantity<direction[one], Vec3>;

  inline direction_t unit_direction (const Vec3& unit) {
    return unit * direction[one];
  }

  inline const Vec3& direction_value (const direction_t& value) {
    return value.numerical_value_ref_in (one);
  }

  inline direction_t direction_of (const displacement_t& value) {
    return unit_direction (normalized (value.numerical_value_in (si::metre)));
  }

  inline displacement_t along (const direction_t& way, meters_t distance) {
    return displacement (direction_value (way) *
                         distance.numerical_value_in (si::metre));
  }

  // ---- The plan: the horizontal plane, x and z ----
  //
  // Where something stands on the map, with no height of its own: a road's
  // centreline, a place's footprint, an animal's grazing ground. A plan
  // position lifted to an elevation is a 3-D position (`at_height`); a 3-D
  // position seen from above is a plan position (`plan`). The world's
  // period, the torus's extent, is a plan extent (moppe/terrain/torus.hh).
  //
  // The point is measured as a horizontal displacement from the world's
  // origin, so a plan displacement moves it and two of them subtract to
  // one, with no quantity_cast (position_t, older, needs one to
  // isq::position_vector). A 3-D displacement does not move it: take its
  // plan () first. The lanes are named axes (moppe/quantities.hh):
  // `const auto [x, z] = d;` gives an x_displacement_t and a
  // z_displacement_t, which do not mix.
  using plan_position_t =
    quantity_point<horizontal_displacement[si::metre],
                   default_point_origin (horizontal_displacement[si::metre]),
                   Vec2>;
  using plan_displacement_t =
    quantity<horizontal_displacement[si::metre], Vec2>;
  using x_displacement_t = quantity<x_displacement[si::metre], float>;
  using z_displacement_t = quantity<z_displacement[si::metre], float>;
  using plan_extent_t = quantity<spatial_extent[si::metre], Vec2>;
  using plan_velocity_t = quantity<isq::velocity[si::metre / si::second], Vec2>;
  using plan_direction_t = quantity<direction[one], Vec2>;

  inline plan_position_t plan_position (const Vec2& metres) {
    return plan_position_t (metres * horizontal_displacement[si::metre]);
  }

  inline plan_position_t plan_position (float x_m, float z_m) {
    return plan_position (Vec2 (x_m, z_m));
  }

  inline plan_position_t plan_position (meters_t x, meters_t z) {
    return plan_position (x.numerical_value_in (si::metre),
                          z.numerical_value_in (si::metre));
  }

  inline plan_displacement_t plan_displacement (const Vec2& metres) {
    return metres * horizontal_displacement[si::metre];
  }

  inline plan_displacement_t plan_displacement (meters_t x, meters_t z) {
    return plan_displacement (Vec2 (x.numerical_value_in (si::metre),
                                    z.numerical_value_in (si::metre)));
  }

  inline plan_extent_t plan_extent (meters_t x, meters_t z) {
    return Vec2 (x.numerical_value_in (si::metre),
                 z.numerical_value_in (si::metre)) *
           spatial_extent[si::metre];
  }

  inline plan_direction_t plan_unit_direction (const Vec2& unit) {
    return unit * direction[one];
  }

  // The metres behind a plan value, for the boundary into float code.
  inline const Vec2& plan_value (const plan_position_t& value) {
    return value.quantity_ref_from (value.point_origin)
      .numerical_value_ref_in (si::metre);
  }

  inline Vec2& plan_value (plan_position_t& value) {
    return value.quantity_ref_from (value.point_origin)
      .numerical_value_ref_in (si::metre);
  }

  inline const Vec2& plan_value (const plan_displacement_t& value) {
    return value.numerical_value_ref_in (si::metre);
  }

  inline const Vec2& plan_value (const plan_extent_t& value) {
    return value.numerical_value_ref_in (si::metre);
  }

  inline const Vec2& plan_value (const plan_direction_t& value) {
    return value.numerical_value_ref_in (one);
  }

  // Each lane as a length: east-west x and north-south z.
  inline meters_t x_of (const plan_position_t& at) {
    return plan_value (at).x () * si::metre;
  }
  inline meters_t z_of (const plan_position_t& at) {
    return plan_value (at).z () * si::metre;
  }
  inline meters_t x_of (const plan_displacement_t& d) {
    return plan_value (d).x () * si::metre;
  }
  inline meters_t z_of (const plan_displacement_t& d) {
    return plan_value (d).z () * si::metre;
  }

  // How far, as a length: sqrt (x * x + z * z), or cr::hypot (x, z) where
  // the code being typed measured that way (the two round differently).
  inline meters_t length (const plan_displacement_t& d) {
    return length (plan_value (d)) * si::metre;
  }
  inline meters_t hypot (const plan_displacement_t& d) {
    return hypot (plan_value (d)) * si::metre;
  }

  inline plan_direction_t direction_of (const plan_displacement_t& d) {
    return plan_unit_direction (normalized (plan_value (d)));
  }

  inline plan_displacement_t along (const plan_direction_t& way,
                                    meters_t distance) {
    return plan_displacement (plan_value (way) *
                              distance.numerical_value_in (si::metre));
  }

  // Seen from above, and lifted back to a height.
  inline plan_position_t plan (const position_t& at) {
    return plan_position (plan (position_value (at)));
  }
  inline plan_displacement_t plan (const displacement_t& d) {
    return plan_displacement (plan (d.numerical_value_in (si::metre)));
  }
  inline position_t at_height (const plan_position_t& at, meters_t y) {
    return position (lift (plan_value (at), y.numerical_value_in (si::metre)));
  }
}

#endif
