#ifndef MOPPE_MATH_HH
#define MOPPE_MATH_HH

// Operator deriving.
#include <boost/operators.hpp>

#include <cmath>

namespace moppe {
  template <typename T>
  struct Vector3D:
    public boost::equality_comparable<Vector3D<T> >,
    public boost::addable<Vector3D<T> >,
    public boost::subtractable<Vector3D<T> >,
    public boost::multipliable2<Vector3D<T>, T>,
    public boost::dividable2<Vector3D<T>, T>
  {
    T x, y, z;

    Vector3D ()              : x (0), y (0), z (0) { }
    Vector3D (T x, T y, T z) : x (x), y (y), z (z) { }

    inline Vector3D operator += (const Vector3D& v)
    { x += v.x; y += v.y; z += v.z; }
    inline Vector3D operator -= (const Vector3D& v)
    { x -= v.x; y -= v.y; z -= v.z; }

    inline Vector3D operator *= (T k) { x *= k; y *= k; z *= k; }
    inline Vector3D operator /= (T k) { x /= k; y /= k; z /= k; }

    inline bool operator == (const Vector3D& v) const
    { return (x == v.x) && (y == v.y) && (z == v.z); }

    inline T length  () const { return std::sqrt (length2 ()); }
    inline T length2 () const { return x*x + y*y + z*z; }

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
  };
}

#endif
