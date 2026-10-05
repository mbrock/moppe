#ifndef MOPPE_GAME_AVATAR_HH
#define MOPPE_GAME_AVATAR_HH

#include <moppe/game/frame_view.hh>
#include <moppe/gfx/math.hh>

namespace moppe::game {
  // The joints of the walking figure in world space, solved once per frame
  // from the walker's motion. Left and right index 0 and 1.
  struct AvatarSkeleton {
    // The body's frame: facing, its right hand, and up.
    Vec3 forward { 0, 0, 1 };
    Vec3 right { 1, 0, 0 };
    Vec3 up { 0, 1, 0 };
    // The chest's frame, twisted and leaned relative to the hips.
    Vec3 chest_forward { 0, 0, 1 };
    Vec3 chest_up { 0, 1, 0 };
    // The head's gaze and its up.
    Vec3 head_forward { 0, 0, 1 };
    Vec3 head_up { 0, 1, 0 };

    Vec3 pelvis;
    Vec3 waist;
    Vec3 chest;
    Vec3 neck;
    Vec3 head;
    Vec3 hip[2];
    Vec3 knee[2];
    Vec3 ankle[2];
    Vec3 heel[2];
    Vec3 toe[2];
    Vec3 shoulder[2];
    Vec3 elbow[2];
    Vec3 wrist[2];
  };

  // Body measurements shared by the pose solver and the mesh, metres.
  namespace avatar_size {
    inline constexpr float thigh = 0.45f;
    inline constexpr float shin = 0.43f;
    inline constexpr float ankle_height = 0.08f;
    inline constexpr float hip_width = 0.10f;
    inline constexpr float upper_arm = 0.29f;
    inline constexpr float forearm = 0.27f;
    inline constexpr float shoulder_width = 0.22f;
  }

  // Procedural animation: idle breathing, a walk that becomes a run with
  // ground speed, a tucked jump, and knees that take a landing. Feet in
  // stance stay where they were planted, because the stride phase advances
  // by the ground the walker actually covered. `time` drives only idle
  // motion such as breathing.
  AvatarSkeleton pose_avatar (const WalkerPose& walker, float time);
}

#endif
