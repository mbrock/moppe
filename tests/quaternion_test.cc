#include <moppe/gfx/math.hh>

#include <tests/test.hh>

#include <type_traits>

using namespace moppe;

namespace {
  MOPPE_TEST (quaternion_rotates_around_an_axis) {
    const Vec3 rotated =
      Quaternion::rotate (Vec3 (1, 0, 0), Vec3 (0, 0, 1), 90 * u::deg);

    MOPPE_CHECK_NEAR (rotated[0], 0.0f, 1e-6f);
    MOPPE_CHECK_NEAR (rotated[1], 1.0f, 1e-6f);
    MOPPE_CHECK_NEAR (rotated[2], 0.0f, 1e-6f);
  }

  MOPPE_TEST (quaternion_multiplication_applies_the_right_operand_first) {
    const Quaternion around_x =
      Quaternion::rotation (Vec3 (1, 0, 0), 90 * u::deg);
    const Quaternion around_z =
      Quaternion::rotation (Vec3 (0, 0, 1), 90 * u::deg);
    const Vec3 rotated =
      Quaternion::rotate (Vec3 (1, 0, 0), around_z * around_x);

    MOPPE_CHECK_NEAR (rotated[0], 0.0f, 1e-6f);
    MOPPE_CHECK_NEAR (rotated[1], 1.0f, 1e-6f);
    MOPPE_CHECK_NEAR (rotated[2], 0.0f, 1e-6f);
  }

  MOPPE_TEST (rotation_quaternion_has_unit_length) {
    const Quaternion rotation =
      Quaternion::rotation (Vec3 (0, 1, 0), 75 * u::deg);

    MOPPE_CHECK_NEAR (rotation.length (), 1.0f, 1e-6f);
  }

#ifdef __APPLE__
  static_assert (sizeof (Quaternion) == sizeof (simd_quatf));
  static_assert (alignof (Quaternion) == alignof (simd_quatf));
  static_assert (std::is_trivially_copyable_v<Quaternion>);
  static_assert (std::is_standard_layout_v<Quaternion>);

  MOPPE_TEST (quaternion_uses_apple_simd_storage) {
    const Quaternion rotation =
      Quaternion::rotation (Vec3 (0, 0, 1), 90 * u::deg);
    const simd_float4 value = rotation.native ().vector;
    const float half_sqrt_two = std::sqrt (0.5f);

    MOPPE_CHECK_NEAR (value.x, 0.0f, 1e-6f);
    MOPPE_CHECK_NEAR (value.y, 0.0f, 1e-6f);
    MOPPE_CHECK_NEAR (value.z, half_sqrt_two, 1e-6f);
    MOPPE_CHECK_NEAR (value.w, half_sqrt_two, 1e-6f);
  }
#endif
}
