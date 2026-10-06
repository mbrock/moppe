#include <moppe/game/avatar.hh>
#include <moppe/game/figure.hh>
#include <moppe/game/frame_view.hh>

#include <tests/test.hh>

#include <algorithm>
#include <cmath>

using namespace moppe;

namespace {
  Vec3 rest_point (std::size_t i) {
    const auto& p = game::figure::vertices[i].position;
    return Vec3 (p[0], p[1], p[2]);
  }

  // The worst ratio of a skinned triangle edge to its rest length, over
  // edges long enough to measure.
  float worst_stretch (const std::vector<Vec3>& points) {
    float worst = 0.0f;
    for (const auto& t : game::figure::triangles)
      for (int e = 0; e < 3; ++e) {
        const std::size_t a = t.vertex[e], b = t.vertex[(e + 1) % 3];
        const float rest = length (rest_point (a) - rest_point (b));
        if (rest > 1e-3f)
          worst = std::max (worst, length (points[a] - points[b]) / rest);
      }
    return worst;
  }
}

MOPPE_TEST (figure_rest_pose_reproduces_the_rest_mesh) {
  // The standing pose is close to the rig's rest pose, so nothing moves
  // far and nothing tears.
  game::WalkerPose walker;
  walker.position = Vec3 (3, 2, 1);
  std::vector<Vec3> points, normals;
  game::figure::skin (game::pose_avatar (walker, 0.0f), points, normals);
  MOPPE_CHECK (points.size () == game::figure::vertices.size ());
  MOPPE_CHECK (worst_stretch (points) < 1.3f);
}

MOPPE_TEST (figure_skin_never_tears_through_a_stride) {
  // Far from the origin, as in a real world, where any error in the blend
  // weights scales with the coordinates.
  game::WalkerPose walker;
  walker.position = Vec3 (700, 80, 650);
  walker.heading = Vec3 (0.6f, 0, 0.8f);
  std::vector<Vec3> points, normals;
  for (float speed : { 1.4f, 3.0f, 6.5f })
    for (int i = 0; i < 16; ++i) {
      walker.stride_speed = speed;
      walker.velocity = walker.heading * speed;
      walker.stride_phase = i / 16.0f;
      game::figure::skin (game::pose_avatar (walker, 0.0f), points, normals);
      const float stretch = worst_stretch (points);
      // Creases stretch short edges (the boot at a running ankle about
      // twofold); a tear stretches them by tens.
      MOPPE_CHECK (stretch < 2.5f);
    }
}

MOPPE_TEST (holding_pose_reaches_its_targets_with_true_limbs) {
  // A rider's targets, all within reach: the hands land exactly on them
  // and every limb keeps its length; the knees bend toward their poles.
  game::HoldPose hold;
  hold.pelvis = Vec3 (300, 40, -200);
  hold.lean = 0.5f;
  const Vec3 forward = hold.facing, up = hold.up, right (1, 0, 0);
  for (int i = 0; i < 2; ++i) {
    const float side = i == 0 ? -1.0f : 1.0f;
    hold.wrist[i] =
      hold.pelvis + forward * 0.45f + up * 0.35f + right * (side * 0.3f);
    hold.ankle[i] = hold.pelvis - up * 0.6f + right * (side * 0.18f);
    hold.elbow_pole[i] = right * side;
    hold.knee_pole[i] = forward;
  }
  const game::AvatarSkeleton k = game::pose_holding (hold);
  namespace size = game::avatar_size;
  for (int i = 0; i < 2; ++i) {
    MOPPE_CHECK (length (k.wrist[i] - hold.wrist[i]) < 1e-4f);
    MOPPE_CHECK (length (k.ankle[i] - hold.ankle[i]) < 1e-4f);
    MOPPE_CHECK_NEAR (
      length (k.elbow[i] - k.shoulder[i]), size::upper_arm, 1e-3f);
    MOPPE_CHECK_NEAR (length (k.wrist[i] - k.elbow[i]), size::forearm, 1e-3f);
    MOPPE_CHECK_NEAR (length (k.knee[i] - k.hip[i]), size::thigh, 1e-3f);
    MOPPE_CHECK_NEAR (length (k.ankle[i] - k.knee[i]), size::shin, 1e-3f);
    MOPPE_CHECK (dot (k.knee[i] - (k.hip[i] + k.ankle[i]) * 0.5f, forward) >
                 0.0f);
  }

  // Skinned to that pose, far from the origin, nothing tears.
  std::vector<Vec3> points, normals;
  game::figure::skin (k, points, normals);
  MOPPE_CHECK (worst_stretch (points) < 2.5f);
}
