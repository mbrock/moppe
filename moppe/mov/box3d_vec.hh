#ifndef MOPPE_MOV_BOX3D_VEC_HH
#define MOPPE_MOV_BOX3D_VEC_HH

#include <moppe/gfx/math.hh>

#include <box3d/math_functions.h>

// Conversions between moppe's vectors and Box3D's. They live in one header
// because the engine compiles as a unity build, where two files' private
// helpers of the same name would collide.

namespace moppe::mov {
  inline b3Vec3 to_b3 (const Vec3& v) {
    return { v[0], v[1], v[2] };
  }

  inline Vec3 from_b3 (const b3Vec3& v) {
    return Vec3 (v.x, v.y, v.z);
  }
}

#endif
