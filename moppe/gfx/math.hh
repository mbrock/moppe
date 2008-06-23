#ifndef MOPPE_MATH_HH
#define MOPPE_MATH_HH

// Operator deriving.
#include <boost/operators.hpp>

#include <cmath>

namespace moppe {
  const float PI  = 3.14159;
  const float PI2 = 3.14159 * 2;

  template <typename T>
  struct Vector3DG:
    public boost::equality_comparable<Vector3DG<T> >,
    public boost::addable<Vector3DG<T> >,
    public boost::subtractable<Vector3DG<T> >,
    public boost::multipliable2<Vector3DG<T>, T>,
    public boost::dividable2<Vector3DG<T>, T>
  {
    T x, y, z;

    Vector3DG ()              : x (0), y (0), z (0) { }
    Vector3DG (T x, T y, T z) : x (x), y (y), z (z) { }

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
  struct QuaternionG:
    public boost::multipliable<QuaternionG<T> >
  {
    T x, y, z, w;

    QuaternionG (T x, T y, T z, T w) 
      : x (x), y (y), z (z), w (w) { }

    QuaternionG (const Vector3DG<T>& v, T w)
      : x (v.x), y (v.y), z (v.z), w (w)
    { }
    
    static inline QuaternionG
    rotation (const Vector3DG<T>& axis, float theta)
    {
      return QuaternionG (std::sin (theta / 2) * axis,
			 std::cos (theta / 2));
    }

    static inline Vector3DG<T>
    rotate (const Vector3DG<T>& v,
	    const Vector3DG<T>& axis,
	    float theta)
    {
      QuaternionG r = rotation (axis, theta);
      return rotate (v, r);
    }

    static inline Vector3DG<T>
    rotate (const Vector3DG<T>& v,
	    const QuaternionG& q)
    {
      QuaternionG r (q);
      const QuaternionG rc (r.conjugate ());

      r *= QuaternionG (v, 0);
      r *= rc;

      return r.vector ();
    }

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
    { return QuaternionG (x, y, z, -w); }

    T length () const
    { return std::sqrt (x*x + y*y + z*z + w*w); }
  };

  typedef Vector3DG<float>   Vector3D;
  typedef QuaternionG<float> Quaternion;
}

#endif
