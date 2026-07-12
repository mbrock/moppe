#ifndef MOPPE_MATH_HH
#define MOPPE_MATH_HH

#include <moppe/quantities.hh>

#include <mp-units/framework.h>
#include <mp-units/math.h>
#include <mp-units/systems/isq.h>
#include <mp-units/systems/si.h>
#include <mp-units/utility/cartesian_vector.h>

#include <cmath>
#include <iostream>
#include <numbers>

namespace moppe {
  using namespace mp_units;

  // Quantity literals: 90 * u::deg, 2.5f * u::m, 60 * u::Hz.
  namespace u {
    using namespace mp_units::si::unit_symbols;
  }

  inline constexpr float PI = std::numbers::pi_v<float>;
  inline constexpr float PI2 = 2.0f * PI;

  using seconds_t = quantity<si::second, float>;
  using meters_t = quantity<si::metre, float>;
  using degrees_t = quantity<si::degree, float>;
  using radians_t = quantity<si::radian, float>;
  using magnitude_t = quantity<one, float>;
  using speed_t = quantity<si::metre / si::second, float>;
  using newtons_t = quantity<si::newton, float>;
  using watts_t = quantity<si::watt, float>;
  using kilograms_t = quantity<si::kilogram, float>;

  // Dimension-one kinds from moppe/quantities.hh.
  using proportion_t = quantity<proportion[one], float>;
  using probability_t = quantity<probability[one], float>;
  using control_signal_t = quantity<control_signal[one], float>;
  using noise_signal_t = quantity<noise_signal[one], float>;

  // The inverse time constant of exponential decay (ISQ calls the
  // quantity a damping coefficient).  Every 1 - exp (-k * dt)
  // smoothing constant in the codebase is one of these; spelled
  // 3.1f / u::s at definition sites.
  using damping_t = quantity<one / si::second, float>;
  using frequency_t = quantity<si::hertz, float>;

  inline constexpr meters_t one_meter = 1.0f * si::metre;

  inline seconds_t seconds (float value) {
    return value * si::second;
  }

  // Boundary escapes into unit-blind float code (rendering, raster
  // grids).  Each names the unit the float is measured in.
  inline float seconds_value (seconds_t q) {
    return q.numerical_value_in (si::second);
  }
  inline float meters_value (meters_t q) {
    return q.numerical_value_in (si::metre);
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

  // Frame-rate-independent exponential decay: the surviving
  // proportion of a quantity after dt at decay rate k.  Returns the
  // plain float weight that the unit-blind vector lerps consume; the
  // typed rate is the point, making the exponent's dimensionlessness
  // (frame-rate independence) a compile-time fact.
  inline float decay (damping_t k, seconds_t dt) {
    return std::exp (-scalar_value (k * dt));
  }

  // The complementary smoothing weight: how far toward a target a
  // low-pass filter moves during dt when it converges at rate k.
  inline float smoothing_alpha (damping_t k, seconds_t dt) {
    return 1.0f - decay (k, dt);
  }

  // Trig on typed angles.  Degrees convert implicitly at the call
  // (float representation), so sin (45 * u::deg) works; the plain
  // float result feeds unit-blind geometry code.
  inline float sin (radians_t a) {
    return std::sin (radians_value (a));
  }
  inline float cos (radians_t a) {
    return std::cos (radians_value (a));
  }
  inline float tan (radians_t a) {
    return std::tan (radians_value (a));
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

  template <typename T>
  using Vec3T = mp_units::utility::cartesian_vector<T, 3>;

  template <typename T>
  T length (const Vec3T<T>& value) {
    return value.magnitude ();
  }

  template <typename T>
  T length2 (const Vec3T<T>& value) {
    return scalar_product (value, value);
  }

  template <typename T>
  void normalize (Vec3T<T>& value) {
    value = value.unit ();
  }

  template <typename T>
  Vec3T<T> normalized (const Vec3T<T>& value) {
    return value.unit ();
  }

  template <typename T, typename U>
  auto dot (const Vec3T<T>& left, const Vec3T<U>& right) {
    return scalar_product (left, right);
  }

  template <typename T, typename U>
  auto cross (const Vec3T<T>& left, const Vec3T<U>& right) {
    return vector_product (left, right);
  }

  template <typename T, typename U>
  auto scaled (const Vec3T<T>& left, const Vec3T<U>& right) {
    return Vec3T { left[0] * right[0], left[1] * right[1], left[2] * right[2] };
  }

  template <typename T>
  Vec3T<T> linear_vector_interpolate (const Vec3T<T>& from,
                                      const Vec3T<T>& to,
                                      const T& alpha) {
    return Vec3T<T> (linear_interpolate (from[0], to[0], alpha),
                     linear_interpolate (from[1], to[1], alpha),
                     linear_interpolate (from[2], to[2], alpha));
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

  template <typename T>
  struct QuaternionG {
    T x, y, z, w;

    QuaternionG (T x, T y, T z, T w) : x (x), y (y), z (z), w (w) {}

    QuaternionG (const Vec3T<T>& v, T w)
        : x (v[0]), y (v[1]), z (v[2]), w (w) {}

    // Make a rotation quaternion from an axis and an angle.
    static inline QuaternionG rotation (const Vec3T<T>& axis, radians_t angle) {
      return QuaternionG (moppe::sin (angle / 2) * axis,
                          moppe::cos (angle / 2));
    }

    // Rotate a vector around an axis by an angle.
    static inline Vec3T<T>
    rotate (const Vec3T<T>& v, const Vec3T<T>& axis, radians_t angle) {
      QuaternionG r = rotation (axis, angle);
      return rotate (v, r);
    }

    // Apply a rotation quaternion to a vector.
    static inline Vec3T<T> rotate (const Vec3T<T>& v, const QuaternionG& q) {
      QuaternionG r (q);
      const QuaternionG rc (r.conjugate ());

      r *= QuaternionG (v, 0);
      r *= rc;

      return r.vector ();
    }

    // Return my non-w components as a 3D vector.
    inline Vec3T<T> vector () const {
      return Vec3T<T> (x, y, z);
    }

    QuaternionG& operator*= (const QuaternionG& q) {
      const QuaternionG a (*this);

      x = a.w * q.x + a.x * q.w + a.y * q.z - a.z * q.y;
      y = a.w * q.y - a.x * q.z + a.y * q.w + a.z * q.x;
      z = a.w * q.z + a.x * q.y - a.y * q.x + a.z * q.w;
      w = a.w * q.w - a.x * q.x - a.y * q.y - a.z * q.z;

      return *this;
    }

    inline QuaternionG conjugate () const {
      return QuaternionG (-x, -y, -z, w);
    }

    T length () const {
      return std::sqrt (x * x + y * y + z * z + w * w);
    }
  };

  template <typename T>
  inline QuaternionG<T> operator* (QuaternionG<T> a, const QuaternionG<T>& b) {
    a *= b;
    return a;
  }

  using Vec3 = Vec3T<float>;
  using Quaternion = QuaternionG<float>;

  // Physical spatial vectors keep one unit around one numerical vector.  The
  // simulation can migrate fields to these types incrementally without ever
  // constructing a vector whose individual elements are quantities.
  using position_t = quantity<isq::position_vector[si::metre], Vec3>;
  using velocity_t = quantity<isq::velocity[si::metre / si::second], Vec3>;
  using acceleration_t =
    quantity<isq::acceleration[si::metre / pow<2> (si::second)], Vec3>;
  using force_t = quantity<isq::force[si::newton], Vec3>;

  inline position_t position (const Vec3& value) {
    return value * isq::position_vector[si::metre];
  }

  inline velocity_t velocity (const Vec3& value) {
    return value * isq::velocity[si::metre / si::second];
  }

  inline const Vec3& position_value (const position_t& value) {
    return value.numerical_value_ref_in (si::metre);
  }

  inline Vec3& position_value (position_t& value) {
    return value.numerical_value_ref_in (si::metre);
  }

  inline const Vec3& velocity_value (const velocity_t& value) {
    return value.numerical_value_ref_in (si::metre / si::second);
  }

  inline Vec3& velocity_value (velocity_t& value) {
    return value.numerical_value_ref_in (si::metre / si::second);
  }
}

#endif
