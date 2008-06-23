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

    inline Vector3D& operator += (const Vector3D& v)
    { x += v.x; y += v.y; z += v.z; return *this; }
    inline Vector3D& operator -= (const Vector3D& v)
    { x -= v.x; y -= v.y; z -= v.z; return *this; }

    inline Vector3D& operator *= (T k)
    { x *= k; y *= k; z *= k; return *this; }
    inline Vector3D& operator /= (T k)
    { x /= k; y /= k; z /= k; return *this; }

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

  template <typename T>
  struct Quaternion {
    T x, y, z, w;

    Quaternion (T x, T y, T z, T w) 
      : x (x), y (y), z (z), w (w) { }

    Quaternion (const Vector3D<T>& v, T w)
      : x (v.x), y (v.y), z (v.z), w (w)
    { }
    
    static inline Quaternion
    rotation (const Vector3D<T>& axis, float theta)
    {
      return Quaternion (std::sin (theta / 2) * axis,
			 std::cos (theta / 2));
    }

    static inline Vector3D<T>
    rotate (const Vector3D<T>& v,
	    const Vector3D<T>& axis,
	    float theta)
    {
      Quaternion r = rotation (axis, theta);
      return rotate (v, r);
    }

    static inline Vector3D<T>
    rotate (const Vector3D<T>& v,
	    const Quaternion& q)
    {
      Quaternion r (q);
      const Quaternion rc (r.conjugate ());

      r *= Quaternion (v, 0);
      r *= rc;

      return r.vector ();
    }

    inline Vector3D<T> vector () const { return Vector3D<T> (x, y, z); }

    Quaternion& operator *= (const Quaternion& q)
    {
      const Quaternion a (*this);

      x = a.w*q.x + a.x*q.w + a.y*q.z - a.z*q.y;
      y = a.w*q.y - a.x*q.z + a.y*q.w + a.z*q.x;
      z = a.w*q.z + a.x*q.y - a.y*q.x + a.z*q.w;
      w = a.w*q.w - a.x*q.x - a.y*q.y - a.z*q.z;

      return *this;
    }
    
    inline Quaternion conjugate () const
    { return Quaternion (x, y, z, -w); }
  };
}

#endif
