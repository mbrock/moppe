// The numbers under the typed quantities: the float vectors (Vec3, Vec2,
// Float3, the general Vec3T) and the quaternion. They are the
// representations position_t, displacement_t, plan_position_t and the rest
// are built on (moppe/gfx/math.hh), and the raw form geometry takes at the
// boundaries the typed code meets: the renderer's layouts, Box3D, mesh
// posing. A boundary for tools/units-report: floats here are storage, not
// gaps. Signatures elsewhere carry the typed vectors
// (docs/typed-quantities.md).
#ifndef MOPPE_GFX_VECTOR_HH
#define MOPPE_GFX_VECTOR_HH

#include <moppe/correct_math.hh>

#include <mp-units/framework.h>
#include <mp-units/systems/si/core.h>
#include <mp-units/utility/cartesian_vector.h>

#ifdef __APPLE__
#include <simd/simd.h>
#endif

#include <cmath>
#include <concepts>
#include <cstddef>
#include <iostream>
#include <tuple>
#include <type_traits>

namespace moppe {
  // An angle in radians, which a rotation takes.
  using radians_t = mp_units::quantity<mp_units::si::radian, float>;

  inline float radians_in (radians_t angle) {
    return angle.numerical_value_in (mp_units::si::radian);
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

  // Clang's three-lane float vector, which Apple's simd names simd_float3.
  using Float3 = float __attribute__ ((ext_vector_type (3)));

  // mp-units deliberately accepts user-owned linear-algebra representations.
  // Keep the general Vec3T available for non-float component types while the
  // ubiquitous gameplay Vec3 is one SIMD register on every platform. Lanewise
  // arithmetic is exact IEEE; the reductions (dot, length, cross) are
  // spelled out in a fixed order, because the platforms' own -- simd's and
  // the standard library's hypot -- round differently, and a world must
  // generate the same everywhere (docs/determinism.md).
  class Vec3 {
  public:
    using value_type = float;
    static constexpr std::integral_constant<std::size_t, 3> extent {};

    class component_ref {
    public:
      component_ref (Vec3& vector, std::size_t index)
          : m_vector (vector), m_index (index) {}

      operator float () const {
        return m_vector.m_value[m_index];
      }

      component_ref& operator= (float value) {
        m_vector.m_value[m_index] = value;
        return *this;
      }

      component_ref& operator= (const component_ref& other) {
        return *this = static_cast<float> (other);
      }

      component_ref& operator+= (float value) {
        return *this = static_cast<float> (*this) + value;
      }

      component_ref& operator-= (float value) {
        return *this = static_cast<float> (*this) - value;
      }

      component_ref& operator*= (float value) {
        return *this = static_cast<float> (*this) * value;
      }

      component_ref& operator/= (float value) {
        return *this = static_cast<float> (*this) / value;
      }

    private:
      Vec3& m_vector;
      std::size_t m_index;
    };

    Vec3 () : m_value {} {}

    Vec3 (float x, float y = 0.0f, float z = 0.0f) : m_value { x, y, z } {}

    explicit Vec3 (Float3 value) : m_value (value) {}

    component_ref operator[] (std::size_t index) {
      return component_ref (*this, index);
    }

    float operator[] (std::size_t index) const {
      return m_value[index];
    }

    float magnitude () const {
      return std::sqrt (scalar_product (*this, *this));
    }

    float norm () const {
      return magnitude ();
    }

    Vec3 unit () const {
      return *this / magnitude ();
    }

    Vec3& operator+= (const Vec3& other) {
      m_value += other.m_value;
      return *this;
    }

    Vec3& operator-= (const Vec3& other) {
      m_value -= other.m_value;
      return *this;
    }

    Vec3& operator*= (float scale) {
      m_value *= scale;
      return *this;
    }

    Vec3& operator/= (float scale) {
      m_value /= scale;
      return *this;
    }

    const Float3& native () const {
      return m_value;
    }

    Float3& native () {
      return m_value;
    }

    friend Vec3 operator- (const Vec3& value) {
      return Vec3 (-value.m_value);
    }

    friend Vec3 operator+ (const Vec3& left, const Vec3& right) {
      return Vec3 (left.m_value + right.m_value);
    }

    friend Vec3 operator- (const Vec3& left, const Vec3& right) {
      return Vec3 (left.m_value - right.m_value);
    }

    friend Vec3 operator* (const Vec3& value, float scale) {
      return Vec3 (value.m_value * scale);
    }

    friend Vec3 operator* (float scale, const Vec3& value) {
      return value * scale;
    }

    friend Vec3 operator/ (const Vec3& value, float scale) {
      return Vec3 (value.m_value / scale);
    }

    friend bool operator== (const Vec3& left, const Vec3& right) {
      return left[0] == right[0] && left[1] == right[1] && left[2] == right[2];
    }

    friend float scalar_product (const Vec3& left, const Vec3& right) {
      return (left[0] * right[0] + left[1] * right[1]) + left[2] * right[2];
    }

    friend Vec3 vector_product (const Vec3& left, const Vec3& right) {
      return Vec3 (left[1] * right[2] - left[2] * right[1],
                   left[2] * right[0] - left[0] * right[2],
                   left[0] * right[1] - left[1] * right[0]);
    }

    friend std::ostream& operator<< (std::ostream& stream, const Vec3& value) {
      return stream << '[' << value[0] << ", " << value[1] << ", " << value[2]
                    << ']';
    }

  private:
    Float3 m_value;
  };

  inline float length (const Vec3& value) {
    return value.magnitude ();
  }

  inline float length2 (const Vec3& value) {
    return scalar_product (value, value);
  }

  inline void normalize (Vec3& value) {
    value = value.unit ();
  }

  inline Vec3 normalized (const Vec3& value) {
    return value.unit ();
  }

  inline float dot (const Vec3& left, const Vec3& right) {
    return scalar_product (left, right);
  }

  inline Vec3 cross (const Vec3& left, const Vec3& right) {
    return vector_product (left, right);
  }

  inline Vec3 scaled (const Vec3& left, const Vec3& right) {
    return Vec3 (left.native () * right.native ());
  }

  // The horizontal plane's two lanes: x, then z (the world's y is up).
  // The map, the torus, and everything planned on the ground live here, so
  // a place that has no height of its own says so by its type. Lanewise
  // arithmetic is exact IEEE and the reductions are written out in a fixed
  // order, as Vec3's are: `magnitude` is sqrt (x * x + z * z), the same
  // bits as the hand-written sum it replaces, and `hypot` is
  // cr::hypot (x, z), for code that measured that way.
  class Vec2 {
  public:
    using value_type = float;
    static constexpr std::integral_constant<std::size_t, 2> extent {};

    constexpr Vec2 () = default;
    constexpr Vec2 (float x, float z) : m_x (x), m_z (z) {}

    constexpr float x () const {
      return m_x;
    }
    constexpr float z () const {
      return m_z;
    }

    constexpr float operator[] (std::size_t index) const {
      return index == 0 ? m_x : m_z;
    }
    constexpr float& operator[] (std::size_t index) {
      return index == 0 ? m_x : m_z;
    }

    // The tuple protocol, through which mp-units names each lane's axis.
    template <std::size_t Index>
    constexpr float get () const {
      static_assert (Index < 2);
      return Index == 0 ? m_x : m_z;
    }

    float magnitude () const {
      return std::sqrt (scalar_product (*this, *this));
    }

    Vec2 unit () const {
      return *this / magnitude ();
    }

    constexpr Vec2& operator+= (const Vec2& other) {
      m_x += other.m_x;
      m_z += other.m_z;
      return *this;
    }
    constexpr Vec2& operator-= (const Vec2& other) {
      m_x -= other.m_x;
      m_z -= other.m_z;
      return *this;
    }
    constexpr Vec2& operator*= (float scale) {
      m_x *= scale;
      m_z *= scale;
      return *this;
    }
    constexpr Vec2& operator/= (float scale) {
      m_x /= scale;
      m_z /= scale;
      return *this;
    }

    friend constexpr Vec2 operator- (const Vec2& value) {
      return { -value.m_x, -value.m_z };
    }
    friend constexpr Vec2 operator+ (Vec2 left, const Vec2& right) {
      return left += right;
    }
    friend constexpr Vec2 operator- (Vec2 left, const Vec2& right) {
      return left -= right;
    }
    friend constexpr Vec2 operator* (Vec2 value, float scale) {
      return value *= scale;
    }
    friend constexpr Vec2 operator* (float scale, Vec2 value) {
      return value *= scale;
    }
    friend constexpr Vec2 operator/ (Vec2 value, float scale) {
      return value /= scale;
    }
    friend constexpr bool operator== (const Vec2&, const Vec2&) = default;

    friend constexpr float scalar_product (const Vec2& left,
                                           const Vec2& right) {
      return left.m_x * right.m_x + left.m_z * right.m_z;
    }

    friend std::ostream& operator<< (std::ostream& stream, const Vec2& value) {
      return stream << '[' << value.m_x << ", " << value.m_z << ']';
    }

  private:
    float m_x = 0.0f;
    float m_z = 0.0f;
  };

}

// Before any quantity over a Vec2 is formed: mp-units reads a vector's
// length from the tuple protocol once, when it first meets the type.
template <>
struct std::tuple_size<moppe::Vec2> : std::integral_constant<std::size_t, 2> {};

template <std::size_t Index>
struct std::tuple_element<Index, moppe::Vec2> {
  using type = float;
};

namespace moppe {
  inline float length (const Vec2& value) {
    return value.magnitude ();
  }

  inline float length2 (const Vec2& value) {
    return scalar_product (value, value);
  }

  inline Vec2 normalized (const Vec2& value) {
    return value.unit ();
  }

  inline float dot (const Vec2& left, const Vec2& right) {
    return scalar_product (left, right);
  }

  inline float hypot (const Vec2& value) {
    return cr::hypot (value.x (), value.z ());
  }

  // The horizontal part of a 3-vector, and a 3-vector at height y.
  inline Vec2 plan (const Vec3& value) {
    return { value[0], value[2] };
  }

  inline Vec3 lift (const Vec2& value, float y = 0.0f) {
    return Vec3 (value.x (), y, value.z ());
  }

  template <typename T>
  using QuaternionVector =
    std::conditional_t<std::same_as<T, float>, Vec3, Vec3T<T>>;

  template <typename T>
  struct QuaternionG {
    T x, y, z, w;

    QuaternionG (T x, T y, T z, T w) : x (x), y (y), z (z), w (w) {}

    QuaternionG (const QuaternionVector<T>& v, T w)
        : x (v[0]), y (v[1]), z (v[2]), w (w) {}

    // Make a rotation quaternion from an axis and an angle.
    static inline QuaternionG rotation (const QuaternionVector<T>& axis,
                                        radians_t angle) {
      return QuaternionG (cr::sin (radians_in (angle / 2)) * axis,
                          cr::cos (radians_in (angle / 2)));
    }

    // Rotate a vector around an axis by an angle.
    static inline QuaternionVector<T> rotate (const QuaternionVector<T>& v,
                                              const QuaternionVector<T>& axis,
                                              radians_t angle) {
      QuaternionG r = rotation (axis, angle);
      return rotate (v, r);
    }

    // Apply a rotation quaternion to a vector.
    static inline QuaternionVector<T> rotate (const QuaternionVector<T>& v,
                                              const QuaternionG& q) {
      QuaternionG r (q);
      const QuaternionG rc (r.conjugate ());

      r *= QuaternionG (v, 0);
      r *= rc;

      return r.vector ();
    }

    // Return my non-w components as a 3D vector.
    inline QuaternionVector<T> vector () const {
      return QuaternionVector<T> (x, y, z);
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

#ifdef __APPLE__
  // Keep the generic quaternion available for other component types while the
  // gameplay float quaternion uses Apple's native SIMD register and ABI.
  template <>
  class QuaternionG<float> {
  public:
    QuaternionG (float x, float y, float z, float w)
        : m_value (simd_quaternion (x, y, z, w)) {}

    QuaternionG (const Vec3& v, float w) : QuaternionG (v[0], v[1], v[2], w) {}

    static QuaternionG rotation (const Vec3& axis, radians_t angle) {
      const radians_t half_angle = angle / 2;
      return QuaternionG (cr::sin (radians_in (half_angle)) * axis,
                          cr::cos (radians_in (half_angle)));
    }

    static Vec3 rotate (const Vec3& v, const Vec3& axis, radians_t angle) {
      return rotate (v, rotation (axis, angle));
    }

    static Vec3 rotate (const Vec3& v, const QuaternionG& q) {
      const simd_quatf vector = simd_quaternion (v[0], v[1], v[2], 0.0f);
      const simd_quatf rotated =
        simd_mul (simd_mul (q.m_value, vector), simd_conjugate (q.m_value));
      return Vec3 (rotated.vector.xyz);
    }

    Vec3 vector () const {
      return Vec3 (m_value.vector.xyz);
    }

    QuaternionG& operator*= (const QuaternionG& q) {
      m_value = simd_mul (m_value, q.m_value);
      return *this;
    }

    QuaternionG conjugate () const {
      return QuaternionG (simd_conjugate (m_value));
    }

    float length () const {
      return simd_length (m_value.vector);
    }

    const simd_quatf& native () const {
      return m_value;
    }

  private:
    explicit QuaternionG (simd_quatf value) : m_value (value) {}

    simd_quatf m_value;
  };
#endif

  template <typename T>
  inline QuaternionG<T> operator* (QuaternionG<T> a, const QuaternionG<T>& b) {
    a *= b;
    return a;
  }

  using Quaternion = QuaternionG<float>;

#ifdef __APPLE__
  static_assert (sizeof (Quaternion) == sizeof (simd_quatf));
  static_assert (alignof (Quaternion) == alignof (simd_quatf));
  static_assert (std::is_trivially_copyable_v<Quaternion>);
  static_assert (std::is_standard_layout_v<Quaternion>);
#endif
}

#endif
