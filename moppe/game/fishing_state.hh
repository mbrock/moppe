#ifndef MOPPE_GAME_FISHING_STATE_HH
#define MOPPE_GAME_FISHING_STATE_HH

// Fishing as it stands (game/fishing.hh): plain values, part of the
// session's copyable state.

#include <moppe/game/game_clock.hh>
#include <moppe/gfx/math.hh>

#include <cstddef>
#include <cstdint>
#include <vector>

namespace moppe::game {
  using wriggle_t = quantity<wriggle[one], float>;

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
    meters_t length = 0.2f * u::m;
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
    play_time_t since = play_time (-100.0);
    // Where the walker stood to cast (or where the helmsman sits, kept up
    // with the boat as it drifts), and where the float lands and lies.
    position_t stand = position (Vec3 ());
    // Cast from the boat's helm rather than the bank.
    bool from_boat = false;
    position_t aim = position (Vec3 ());
    position_t bobber = position (Vec3 ());
    Waters waters = Waters::still;
    bool upland = false;
    // How many casts there have been, which seeds each one's luck; when
    // the next fish will come to the bait; and what it is.
    std::uint32_t casts = 0;
    play_time_t bite_at = play_time (0.0);
    Fish fish {};
    // Splashes on the water, the next due when.
    play_time_t next_splash = play_time (0.0);
  };

  // What was caught, in the order it came out of the water. Those from
  // the first `used` have gone in the pan.
  struct Creel {
    std::vector<Fish> fish;
    std::size_t used = 0;
    // The last fish landed or lost, and when, for its moment on screen.
    play_time_t last_time = play_time (-100.0);
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
    // Above the horizon ahead; past a right angle it is swung back over
    // the shoulder.
    inclination_t raise = 0.55f * inclination[u::rad];
    // How hard it is pulled, 0..1, bending toward the line.
    proportion_t bend = 0.0f * proportion[one];
    // The line runs from the tip to `water`, blended back to hanging
    // just under the tip as `out` falls from one to nought, and lifted
    // `arc` over the way between.
    position_t water = position (Vec3 ());
    proportion_t out = 1.0f * proportion[one];
    meters_t arc = 0.0f * u::m;
    // How tight the line is, 0..1, against its sag.
    proportion_t tight = 0.0f * proportion[one];
    // A fish on the end of it, and how it twists, in place of the float.
    bool fish_on = false;
    Fish fish {};
    wriggle_t wriggle = 0.0f * game::wriggle[one];
  };
}

#endif
