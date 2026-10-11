#ifndef MOPPE_GAME_CAMP_STATE_HH
#define MOPPE_GAME_CAMP_STATE_HH

// The camp as it stands (game/camp.hh): plain values, part of the
// session's copyable state.

#include <moppe/gfx/math.hh>

#include <cstdint>

namespace moppe::game {
  // Where a hammock's straps go round two trunks, each on its trunk's
  // axis, and the trunks' radii there. The sleeper's head lies toward the
  // second.
  struct HammockSite {
    Vec3 strap[2] {};
    float girth[2] {};
  };

  // A frying pan set on a hearth's stones with a few mushrooms in it.
  // It is its cook's, on whichever fire they set it.
  struct Pan {
    static constexpr int room = 4;
    bool on = false;
    // Where it sits and which way its handle points, radians about up.
    Vec3 place {};
    float yaw = 0.0f;
    // When it went on, and the mushrooms in it: the plan's indices.
    double since = -100.0;
    int count = 0;
    std::uint32_t mushrooms[room] {};
    double next_steam = 0.0;
  };

  // The camp as it stands; part of the session's copyable state.
  struct Camp {
    bool hung = false;
    HammockSite hammock;
    bool resting = false;
    // When the hiker last lay down or got up.
    double rest_time = -100.0;
    // The hammock's swing about the line between its straps, radians.
    float swing = 0.0f;
    float swing_rate = 0.0f;
    // The sleeper's gaze: round from the foot of the hammock, and above
    // the horizon, radians.
    float gaze_yaw = 0.0f;
    float gaze_pitch = 1.1f;

    bool fire = false;
    // The hearth on the ground, and when the fire was last lit or put out.
    Vec3 hearth {};
    double fire_time = -100.0;
    double next_spark = 0.0;
    double next_smoke = 0.0;

    Pan pan;
    // The last meal: when it was eaten, how done it was (see
    // pan_cooked), and how many mushrooms it was.
    double ate_time = -100.0;
    float ate_cooked = 0.0f;
    int ate_count = 0;
  };
}

#endif
