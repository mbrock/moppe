#ifndef MOPPE_MATH_HH
#define MOPPE_MATH_HH

// Operator deriving.
#include <boost/operators.hpp>

#include <cmath>

namespace moppe {
  const float PI  = 3.14159;
  const float PI2 = 3.14159 * 2;

  typedef float degrees_t;
  typedef float radians_t;

  inline radians_t degrees_to_radians (degrees_t x)
  { return x * (PI2 / 360); }

  template <typename T>
  void clamp (T& x, const T& min, const T& max) {
    if (x > max)
      x = max;
    else if (x < min)
      x = min;
  }

  template <typename T>
  struct Vector3DG:
    // Thanks Boost!  Free derived operators!
    public boost::equality_comparable<Vector3DG<T> >,
    public boost::addable<Vector3DG<T> >,
    public boost::subtractable<Vector3DG<T> >,
    public boost::multipliable2<Vector3DG<T>, T>,
    public boost::dividable2<Vector3DG<T>, T>
  {
    T x, y, z;

    Vector3DG ()                   : x (0),   y (0),   z (0)   { }
    Vector3DG (T x, T y, T z)      : x (x),   y (y),   z (z)   { }
    Vector3DG (const Vector3DG& v) : x (v.x), y (v.y), z (v.z) { }

    inline Vector3DG& operator += (const Vector3DG& v)
    { x += v.x; y += v.y; z += v.z; return *this; }
    inline Vector3DG& operator -= (const Vector3DG& v)
    { x -= v.x; y -= v.y; z -= v.z; return *this; }

    inline Vector3DG& operator *= (T k)
    { x *= k; y *= k; z *= k; return *this; }
    inline Vector3DG& operator /= (T k)
    { x /= k; y /= k; z /= k; return *this; }

    inline bool operator == (const Vector3DG& v) const
    { return (x == v.x) && (y == v.y) && (z == v.z); }

    inline T length  () const { return std::sqrt (length2 ()); }
    inline T length2 () const { return dot (*this); }

    void normalize ()
    {
      const T k = length2 ();
      if (k != 0.0)
	*this /= std::sqrt (k);
    }

    T normalized () const
    {
      T t (*this);
      t.normalize ();
      return t;
    }

    Vector3DG cross (const Vector3DG& v) const {
      return Vector3DG (y * v.z - z * v.y,
			z * v.x - x * v.z,
			x * v.y - y * v.x);
    }

    T dot (const Vector3DG& v) const {
      return x * v.x + y * v.y + z * v.z;
    }
  };

  template <typename T>
  std::ostream& operator << (std::ostream& os,
			     const Vector3DG<T>& v) {
    return os << "<" << v.x << " " << v.y << " " << v.z << ">";
  }

  template <typename T>
  struct QuaternionG:
    public boost::multipliable<QuaternionG<T> >
  {
    T x, y, z, w;

    QuaternionG (T x, T y, T z, T w) 
      : x (x), y (y), z (z), w (w) { }

    QuaternionG (const Vector3DG<T>& v, T w)
      : x (v.x), y (v.y), z (v.z), w (w)
    { }
    
    // Make a rotation quaternion from an axis and an angle.
    static inline QuaternionG
    rotation (const Vector3DG<T>& axis, float angle)
    {
      return QuaternionG (std::sin (angle / 2) * axis,
			  std::cos (angle / 2));
    }

    // Rotate a vector around an axis by an angle.
    static inline Vector3DG<T>
    rotate (const Vector3DG<T>& v,
	    const Vector3DG<T>& axis,
	    float angle)
    {
      QuaternionG r = rotation (axis, angle);
      return rotate (v, r);
    }

    // Apply a rotation quaternion to a vector.
    static inline Vector3DG<T>
    rotate (const Vector3DG<T>& v, const QuaternionG& q)
    {
      QuaternionG r (q);
      const QuaternionG rc (r.conjugate ());

      r *= QuaternionG (v, 0);
      r *= rc;

      return r.vector ();
    }

    // Return my non-w components as a 3D vector.
    inline Vector3DG<T> vector () const { return Vector3DG<T> (x, y, z); }

    QuaternionG& operator *= (const QuaternionG& q)
    {
      const QuaternionG a (*this);

      x = a.w*q.x + a.x*q.w + a.y*q.z - a.z*q.y;
      y = a.w*q.y - a.x*q.z + a.y*q.w + a.z*q.x;
      z = a.w*q.z + a.x*q.y - a.y*q.x + a.z*q.w;
      w = a.w*q.w - a.x*q.x - a.y*q.y - a.z*q.z;

      return *this;
    }
    
    inline QuaternionG conjugate () const
    { return QuaternionG (-x, -y, -z, w); }

    T length () const
    { return std::sqrt (x*x + y*y + z*z + w*w); }
  };

  typedef Vector3DG<float>   Vector3D;
  typedef QuaternionG<float> Quaternion;
}

#endif
