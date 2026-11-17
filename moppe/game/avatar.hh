#ifndef MOPPE_GAME_AVATAR_HH
#define MOPPE_GAME_AVATAR_HH

#include <moppe/game/frame_view.hh>
#include <moppe/gfx/math.hh>

namespace moppe::game {
  // The joints of the walking figure in world space, solved once per frame
  // from the walker's motion. Left and right index 0 and 1.
  struct AvatarSkeleton {
    // The body's frame: facing, its right hand, and up.
    direction_t forward = unit_direction (Vec3 (0, 0, 1));
    direction_t right = unit_direction (Vec3 (1, 0, 0));
    direction_t up = unit_direction (Vec3 (0, 1, 0));
    // The chest's frame, twisted and leaned relative to the hips.
    direction_t chest_forward = unit_direction (Vec3 (0, 0, 1));
    direction_t chest_up = unit_direction (Vec3 (0, 1, 0));
    // The head's gaze and its up.
    direction_t head_forward = unit_direction (Vec3 (0, 0, 1));
    direction_t head_up = unit_direction (Vec3 (0, 1, 0));

    position_t pelvis {};
    position_t waist {};
    position_t chest {};
    position_t neck {};
    position_t head {};
    position_t hip[2] {};
    position_t knee[2] {};
    position_t ankle[2] {};
    position_t heel[2] {};
    position_t toe[2] {};
    position_t shoulder[2] {};
    position_t elbow[2] {};
    position_t wrist[2] {};
  };

  // Body measurements shared by the pose solver and the mesh.
  namespace avatar_size {
    inline constexpr meters_t thigh = 0.45f * u::m;
    inline constexpr meters_t shin = 0.43f * u::m;
    inline constexpr meters_t ankle_height = 0.08f * u::m;
    inline constexpr meters_t hip_width = 0.10f * u::m;
    inline constexpr meters_t upper_arm = 0.29f * u::m;
    inline constexpr meters_t forearm = 0.27f * u::m;
    inline constexpr meters_t shoulder_width = 0.22f * u::m;
  }

  // A body holding on to something -- a bike's bars and pegs, a glider's
  // control bar -- given as its trunk's frame and where its hands and feet
  // must go. Elbows and knees bend toward their poles; a target out of
  // reach is pulled in to what the limb can touch.
  struct HoldPose {
    // The way the belly faces, and head-ward along the spine before lean.
    direction_t facing = unit_direction (Vec3 (0, 0, 1));
    direction_t up = unit_direction (Vec3 (0, 1, 0));
    position_t pelvis {};
    // The trunk's tilt from `up` toward `facing`.
    radians_t lean = 0.0f * u::rad;
    // Where the head looks, and roughly which way is up for it (neither
    // need be of unit length).
    Vec3 gaze { 0, 0, 1 };
    Vec3 head_up { 0, 1, 0 };
    position_t wrist[2] {};
    position_t ankle[2] {};
    // The ways the elbows and knees bend toward: leanings, of no
    // particular length.
    Vec3 elbow_pole[2];
    Vec3 knee_pole[2];
    // The feet's heel-to-toe and instep directions.
    direction_t sole = unit_direction (Vec3 (0, 0, 1));
    direction_t instep = unit_direction (Vec3 (0, 1, 0));
  };

  AvatarSkeleton pose_holding (const HoldPose& hold);

  // The shoulder the posed trunk would carry, for choosing a lean that
  // lets the hands reach.
  position_t held_shoulder (const HoldPose& hold, int side);

  // Procedural animation: idle breathing, a walk that becomes a run with
  // ground speed, a tucked jump, and knees that take a landing. Feet in
  // stance stay where they were planted, because the stride phase advances
  // by the ground the walker actually covered. `time` drives only idle
  // motion such as breathing.
  AvatarSkeleton pose_avatar (const WalkerPose& walker, seconds_t time);
}

#endif
