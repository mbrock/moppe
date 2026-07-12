#ifndef MOPPE_MATH_HH
#define MOPPE_MATH_HH

#include <mp-units/framework.h>
#include <mp-units/math.h>
#include <mp-units/systems/isq.h>
#include <mp-units/systems/si.h>

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
  struct Vector3DG {
    typedef T scalar_type;
    using value_type = scalar_type;

    T x, y, z;

    Vector3DG () : x (0), y (0), z (0) {}
    Vector3DG (T x, T y, T z) : x (x), y (y), z (z) {}
    Vector3DG (const Vector3DG& v) : x (v.x), y (v.y), z (v.z) {}

    Vector3DG& operator= (const Vector3DG& v) = default;

    // Indexed access; also what marks this type as an order-1 (vector)
    // representation to mp-units, together with norm() below, so
    // quantity<si::metre, Vector3D> is a valid vector quantity.
    T& operator[] (std::size_t i) {
      return (&x)[i];
    }
    const T& operator[] (std::size_t i) const {
      return (&x)[i];
    }

    inline Vector3DG& operator+= (const Vector3DG& v) {
      x += v.x;
      y += v.y;
      z += v.z;
      return *this;
    }
    inline Vector3DG& operator-= (const Vector3DG& v) {
      x -= v.x;
      y -= v.y;
      z -= v.z;
      return *this;
    }

    inline Vector3DG& operator*= (T k) {
      x *= k;
      y *= k;
      z *= k;
      return *this;
    }
    inline Vector3DG& operator/= (T k) {
      x /= k;
      y /= k;
      z /= k;
      return *this;
    }

    inline Vector3DG operator- () const {
      return Vector3DG (-x, -y, -z);
    }

    inline bool operator== (const Vector3DG& v) const {
      return (x == v.x) && (y == v.y) && (z == v.z);
    }

    inline T length () const {
      return std::sqrt (length2 ());
    }
    inline T length2 () const {
      return dot (*this);
    }
    // mp-units spelling of the Euclidean magnitude.
    inline T norm () const {
      return length ();
    }

    void normalize () {
      const T k = length2 ();
      if (k != 0.0)
        *this /= std::sqrt (k);
    }

    Vector3DG normalized () const {
      Vector3DG t (*this);
      t.normalize ();
      return t;
    }

    Vector3DG cross (const Vector3DG& v) const {
      return Vector3DG (
        y * v.z - z * v.y, z * v.x - x * v.z, x * v.y - y * v.x);
    }

    T dot (const Vector3DG& v) const {
      return x * v.x + y * v.y + z * v.z;
    }

    Vector3DG scaled (const Vector3DG& v) const {
      return Vector3DG (x * v.x, y * v.y, z * v.z);
    }
  };

  template <typename T>
  inline Vector3DG<T> operator+ (Vector3DG<T> a, const Vector3DG<T>& b) {
    a += b;
    return a;
  }

  template <typename T>
  inline Vector3DG<T> operator- (Vector3DG<T> a, const Vector3DG<T>& b) {
    a -= b;
    return a;
  }

  // The scalar is a non-deduced parameter so that e.g. v * 10
  // converts the int instead of failing deduction.
  template <typename T>
  inline Vector3DG<T> operator* (Vector3DG<T> v,
                                 typename Vector3DG<T>::scalar_type k) {
    v *= k;
    return v;
  }

  template <typename T>
  inline Vector3DG<T> operator* (typename Vector3DG<T>::scalar_type k,
                                 Vector3DG<T> v) {
    v *= k;
    return v;
  }

  template <typename T>
  inline Vector3DG<T> operator/ (Vector3DG<T> v,
                                 typename Vector3DG<T>::scalar_type k) {
    v /= k;
    return v;
  }

  template <typename T>
  inline bool operator!= (const Vector3DG<T>& a, const Vector3DG<T>& b) {
    return !(a == b);
  }

  template <typename T>
  std::ostream& operator<< (std::ostream& os, const Vector3DG<T>& v) {
    return os << "<" << v.x << " " << v.y << " " << v.z << ">";
  }

  template <typename T>
  Vector3DG<T> linear_vector_interpolate (const Vector3DG<T>& from,
                                          const Vector3DG<T>& to,
                                          const T& alpha) {
    return Vector3DG<T> (linear_interpolate (from.x, to.x, alpha),
                         linear_interpolate (from.y, to.y, alpha),
                         linear_interpolate (from.z, to.z, alpha));
  }

  // Vector-quantity algebra: mp-units keeps the unit outside and the
  // numerical Vector3DG inside (quantity<si::metre, Vector3D>), so
  // products combine the numerical vectors and the references.
  template <Quantity Q1, Quantity Q2>
    requires requires (const Q1& a, const Q2& b) {
      a.numerical_value_in (Q1::unit).dot (b.numerical_value_in (Q2::unit));
    }
  auto dot (const Q1& a, const Q2& b) {
    return a.numerical_value_in (Q1::unit).dot (
             b.numerical_value_in (Q2::unit)) *
           (Q1::reference * Q2::reference);
  }

  template <Quantity Q1, Quantity Q2>
    requires requires (const Q1& a, const Q2& b) {
      a.numerical_value_in (Q1::unit).cross (b.numerical_value_in (Q2::unit));
    }
  auto cross (const Q1& a, const Q2& b) {
    return a.numerical_value_in (Q1::unit).cross (
             b.numerical_value_in (Q2::unit)) *
           (Q1::reference * Q2::reference);
  }

  template <typename T>
  struct QuaternionG {
    T x, y, z, w;

    QuaternionG (T x, T y, T z, T w) : x (x), y (y), z (z), w (w) {}

    QuaternionG (const Vector3DG<T>& v, T w)
        : x (v.x), y (v.y), z (v.z), w (w) {}

    // Make a rotation quaternion from an axis and an angle.
    static inline QuaternionG rotation (const Vector3DG<T>& axis,
                                        radians_t angle) {
      return QuaternionG (moppe::sin (angle / 2) * axis,
                          moppe::cos (angle / 2));
    }

    // Rotate a vector around an axis by an angle.
    static inline Vector3DG<T>
    rotate (const Vector3DG<T>& v, const Vector3DG<T>& axis, radians_t angle) {
      QuaternionG r = rotation (axis, angle);
      return rotate (v, r);
    }

    // Apply a rotation quaternion to a vector.
    static inline Vector3DG<T> rotate (const Vector3DG<T>& v,
                                       const QuaternionG& q) {
      QuaternionG r (q);
      const QuaternionG rc (r.conjugate ());

      r *= QuaternionG (v, 0);
      r *= rc;

      return r.vector ();
    }

    // Return my non-w components as a 3D vector.
    inline Vector3DG<T> vector () const {
      return Vector3DG<T> (x, y, z);
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

  typedef Vector3DG<float> Vector3D;
  typedef QuaternionG<float> Quaternion;
}

#endif
