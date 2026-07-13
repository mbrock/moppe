#pragma once

#include "atelier/space.hh"

// GPU matrices: where the scene's quantities become the float4x4s a
// vertex shader multiplies by.  Everything here consumes points,
// angles, and lengths, and emits raw clip-space arithmetic.

namespace atelier {
  using Matrix = simd_float4x4;

  [[nodiscard]] inline Matrix translation (const Point& to) {
    Matrix matrix = matrix_identity_float4x4;
    matrix.columns[3] = simd_make_float4 (in_metres (to), 1.0f);
    return matrix;
  }

  [[nodiscard]] inline Matrix rotation (Angle by, const Vec3& about) {
    return simd_matrix4x4 (simd_quaternion (in_radians (by), as_simd (about)));
  }

  // The rotation carrying one unit direction onto another.
  [[nodiscard]] inline Matrix rotation (const Vec3& from, const Vec3& to) {
    return simd_matrix4x4 (simd_quaternion (as_simd (from), as_simd (to)));
  }

  [[nodiscard]] inline Matrix perspective (Angle vertical_fov,
                                           Real aspect_ratio,
                                           Length near_plane,
                                           Length far_plane) {
    const Real y = Real (1.0f / angular::tan (vertical_fov / 2));
    const Real x = y / aspect_ratio;
    const Real z = Real (far_plane / (near_plane - far_plane));
    return { simd_make_float4 (x, 0, 0, 0),
             simd_make_float4 (0, y, 0, 0),
             simd_make_float4 (0, 0, z, -1),
             simd_make_float4 (0, 0, in_metres (near_plane) * z, 0) };
  }

  [[nodiscard]] inline Matrix orthographic (Length width,
                                            Real aspect_ratio,
                                            Length near_plane,
                                            Length far_plane) {
    const Real width_metres = in_metres (width);
    const Real near_metres = in_metres (near_plane);
    const Real far_metres = in_metres (far_plane);
    const Real x = 2 / width_metres;
    const Real y = x * aspect_ratio;
    const Real z = 1 / (near_metres - far_metres);
    const Real offset = near_metres / (near_metres - far_metres);
    return { simd_make_float4 (x, 0, 0, 0),
             simd_make_float4 (0, y, 0, 0),
             simd_make_float4 (0, 0, z, 0),
             simd_make_float4 (0, 0, offset, 1) };
  }

  [[nodiscard]] inline Matrix look_at (const Point& eye, const Point& focus) {
    const simd_float3 from = in_metres (eye);
    const simd_float3 back = simd_normalize (from - in_metres (focus));
    const simd_float3 right = simd_normalize (simd_cross (as_simd (up), back));
    const simd_float3 above = simd_cross (back, right);
    return { simd_make_float4 (right.x, above.x, back.x, 0),
             simd_make_float4 (right.y, above.y, back.y, 0),
             simd_make_float4 (right.z, above.z, back.z, 0),
             simd_make_float4 (-simd_dot (right, from),
                               -simd_dot (above, from),
                               -simd_dot (back, from),
                               1) };
  }
}
