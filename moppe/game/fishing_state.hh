#ifndef MOPPE_GAME_FISHING_STATE_HH
#define MOPPE_GAME_FISHING_STATE_HH

// Fishing as it stands (game/fishing.hh): plain values, part of the
// session's copyable state.

#include <moppe/gfx/math.hh>

#include <cstddef>
#include <cstdint>
#include <vector>

namespace moppe::game {
  // The fish of the north's waters. Which bites follows the water: perch,
  // pike, and roach in still lakes and slow pools, brown trout and
  // grayling in running water, arctic char in the cold lakes high up, and
  // herring and cod off the coast.
  enum class FishKind : std::uint8_t {
    perch,
    pike,
    roach,
    brown_trout,
    grayling,
    arctic_char,
    herring,
    cod,
  };
  inline constexpr int fish_kind_count = 8;

  // What kind of water the line went into.
  enum class Waters : std::uint8_t { still, running, sea };

  struct Fish {
    FishKind kind = FishKind::perch;
    // Nose to tail fork.
    float length = 0.2f; // metres
    std::uint32_t seed = 0;
  };

  // The line, from the cast until it comes in again.
  struct Line {
    enum class Phase : std::uint8_t {
      // The rod is put away.
      idle,
      // The float flies out over the water.
      casting,
      // It rides the water, now and then nibbled at.
      waiting,
      // Something has taken the bait and pulls the float under: strike
      // now or lose it.
      biting,
      // Hooked, and fighting the line on its way in.
      playing,
      // Lifted out of the water on the end of the line.
      landed,
      // Come in with nothing on it.
      empty,
    };
    Phase phase = Phase::idle;
    // When the phase began.
    double since = -100.0;
    // Where the walker stood to cast, and where the float lands and lies.
    Vec3 stand {};
    Vec3 aim {};
    Vec3 bobber {};
    Waters waters = Waters::still;
    bool upland = false;
    // How many casts there have been, which seeds each one's luck; when
    // the next fish will come to the bait; and what it is.
    std::uint32_t casts = 0;
    double bite_at = 0.0;
    Fish fish {};
    // Splashes on the water, the next due when.
    double next_splash = 0.0;
  };

  // What was caught, in the order it came out of the water. Those from
  // the first `used` have gone in the pan.
  struct Creel {
    std::vector<Fish> fish;
    std::size_t used = 0;
    // The last fish landed or lost, and when, for its moment on screen.
    double last_time = -100.0;
    Fish last {};
    bool last_lost = false;

    int kept () const {
      return static_cast<int> (fish.size () - used);
    }
  };

  struct Fishing {
    Line line;
    Creel creel;
  };

  // The rod as drawn at a moment (rod_pose in game/fishing.hh), in the
  // walker's pose: how high it is held, how far it bends, and where the
  // line runs.
  struct RodPose {
    // Radians above the horizon ahead; past a right angle it is swung
    // back over the shoulder.
    float raise = 0.55f;
    // How hard it is pulled, 0..1, bending toward the line.
    float bend = 0.0f;
    // The line runs from the tip to `water`, blended back to hanging
    // just under the tip as `out` falls from one to nought, and lifted
    // `arc` metres over the way between.
    Vec3 water {};
    float out = 1.0f;
    float arc = 0.0f;
    // How tight the line is, 0..1, against its sag.
    float tight = 0.0f;
    // A fish on the end of it, and how it twists, in place of the float.
    bool fish_on = false;
    Fish fish {};
    float wriggle = 0.0f;
  };
}

#endif
