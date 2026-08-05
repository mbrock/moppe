#include <moppe/gfx/mat4.hh>

#include <tests/test.hh>

#include <type_traits>

using namespace moppe;

namespace {
  static_assert (sizeof (Mat4) == 16 * sizeof (float));
  static_assert (std::is_trivially_copyable_v<Mat4>);
  static_assert (std::is_standard_layout_v<Mat4>);

#ifdef __APPLE__
  static_assert (alignof (Mat4) == alignof (simd_float4x4));
#endif

  MOPPE_TEST (matrix_multiplication_applies_the_right_operand_first) {
    const Mat4 transform =
      Mat4::translation (Vec3 (5, 6, 7)) * Mat4::scaling (Vec3 (2, 3, 4));

    MOPPE_CHECK (transform.transform_point (Vec3 (1, 2, 3)) ==
                 Vec3 (7, 12, 19));
  }

  MOPPE_TEST (matrix_vector_transform_ignores_translation) {
    const Mat4 transform = Mat4::translation (Vec3 (5, 6, 7));

    MOPPE_CHECK (transform.transform_vector (Vec3 (1, 2, 3)) == Vec3 (1, 2, 3));
  }

  MOPPE_TEST (matrix_rotation_keeps_the_right_handed_convention) {
    const Vec3 rotated = Mat4::rotation (90 * u::deg, Vec3 (0, 0, 1))
                           .transform_vector (Vec3 (1, 0, 0));

    MOPPE_CHECK_NEAR (rotated[0], 0.0f, 1e-6f);
    MOPPE_CHECK_NEAR (rotated[1], 1.0f, 1e-6f);
    MOPPE_CHECK_NEAR (rotated[2], 0.0f, 1e-6f);
  }

  MOPPE_TEST (reversed_projection_maps_near_to_one_and_far_to_zero) {
    constexpr float near = 0.5f;
    constexpr float far = 1000.0f;
    const Mat4 projection =
      Mat4::perspective_reversed (70 * u::deg, 16.0f / 9.0f, near, far);
    const auto projected_depth = [&] (float eye_z) {
      const float clip_z =
        projection.element (10) * eye_z + projection.element (14);
      const float clip_w = projection.element (11) * eye_z;
      return clip_z / clip_w;
    };

    MOPPE_CHECK_NEAR (projected_depth (-near), 1.0f, 1e-6f);
    MOPPE_CHECK_NEAR (projected_depth (-far), 0.0f, 1e-6f);
  }

#ifdef __APPLE__
  MOPPE_TEST (matrix_uses_apple_simd_storage) {
    const Mat4 translation = Mat4::translation (Vec3 (5, 6, 7));
    const simd_float4 column = translation.native ().columns[3];

    MOPPE_CHECK (simd_all (column == simd_float4 { 5, 6, 7, 1 }));
  }
#endif
}
