#ifndef MOPPE_GAME_FISHING_HH
#define MOPPE_GAME_FISHING_HH

// Fishing with a float. Standing at the water's edge facing open water,
// the walker casts a baited line; the float rides the water (drifting
// with a river's current) until something nibbles and then takes it,
// pulling the float under. Strike then and the fish is hooked, fought in,
// and lifted out on the line into the creel; wait too long and it gets
// away. What bites follows the water (FishKind). A fish in the creel can
// go in the pan on the fire with the mushrooms (game/camp.hh).

#include <moppe/game/avatar.hh>
#include <moppe/game/fishing_state.hh>
#include <moppe/gfx/math.hh>
#include <moppe/render/draw.hh>

#include <optional>

namespace moppe::game {
  // What an angler calls it.
  const char* fish_name (FishKind kind);

  // The fish that comes to the bait in `waters`, `upland` lakes being the
  // cold ones high up, and how large it is, both chosen by `seed`.
  [[nodiscard]] Fish fish_for (Waters waters, bool upland, std::uint32_t seed);

  // Seconds from the float's settling to the take, by `seed`.
  float bite_wait (std::uint32_t seed);

  // How long the parts of the business last: the cast's flight, the
  // moment there is to strike, fighting a fish in (longer for a bigger
  // one), holding it up, and winding in an empty line.
  inline constexpr float cast_seconds = 0.9f;
  inline constexpr float strike_seconds = 1.4f;
  float play_seconds (const Fish& fish);
  inline constexpr float landed_seconds = 2.6f;
  inline constexpr float wind_seconds = 0.9f;
  // How far a walker may cast, and how near, and how deep the water must
  // be where the float lands; how far they may wander from where they
  // stood before the line comes in.
  inline constexpr float longest_cast = 11.0f;
  inline constexpr float shortest_cast = 3.5f;
  inline constexpr float fishing_depth = 0.35f;
  inline constexpr float fishing_leash = 1.2f;

  // Moves the line on to `now`: the cast lands, a fish takes the bait,
  // one not struck in time gets away and another may come, one fought in
  // long enough is landed into the creel, and an empty line comes in.
  // `strike` is the pick key pressed this frame. Returns whether it was
  // used.
  bool tend_line (Fishing& fishing, double now, bool strike);

  // Begins a cast from where the walker stands at the water at `aim`.
  void cast_line (Line& line,
                  const Vec3& stand,
                  const Vec3& aim,
                  Waters waters,
                  bool upland,
                  double now);

  // Whether the line is out or being dealt with, the rod in hand.
  inline bool line_out (const Line& line) {
    return line.phase != Line::Phase::idle;
  }

  // Where the fish on the line is, fought in toward the bank, at the
  // point it leaves the water.
  Vec3 fish_in_water (const Line& line, double now);

  // The rod and line as drawn at `now`, if the rod is in hand; `time`
  // keeps the float bobbing and the fish twisting.
  std::optional<RodPose> rod_pose (const Line& line, double now, float time);

  // Where the right hand holds a rod raised `raise` radians, from the
  // shoulders `k` stands with.
  Vec3 rod_hand (const AvatarSkeleton& k, float raise);

  // The rod in the posed figure's right hand, the line, and the float or
  // the fish on its end, recorded as triangles.
  void draw_rod (render::DrawList& dl,
                 const AvatarSkeleton& k,
                 const RodPose& rod,
                 float time);

  // A fish, its mouth at `mouth`, its body running along `along` to its
  // tail, its back toward `back`; `scale` multiplies its length as drawn,
  // `wriggle` bends it, and `cooked` browns it as a pan's mushrooms are
  // (one fried golden, two burnt black).
  void draw_fish (render::DrawList& dl,
                  const Fish& fish,
                  const Vec3& mouth,
                  const Vec3& along,
                  const Vec3& back,
                  float scale,
                  float wriggle,
                  float cooked = 0.0f);
}

#endif
