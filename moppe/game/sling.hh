#ifndef MOPPE_GAME_SLING_HH
#define MOPPE_GAME_SLING_HH

#include <moppe/game/rescue.hh>
#include <moppe/gfx/math.hh>

#include <array>

namespace moppe::game {
  // The helicopter's slung load: whichever machine the pilot hooked, hung
  // on a line from a hook under the helicopter's belly.
  //
  // Flying, with the hook over a machine and near enough above it that
  // the line reaches (it need not be aimed: some metres to one side
  // will do), the mount key hooks it on; the key again lets it go. The
  // line is a stiff spring that only pulls, from the hook to a ring over
  // the machine's centre of mass, where the legs of its bridle meet, so
  // that it hangs level. The flight computer knows what hangs under it
  // and lifts it too, but its weight is the helicopter's to carry: it
  // pulls the helicopter down and leans it, and slows its climb.
  struct Sling {
    // The line is on the hook.
    bool hooked = false;
    // What hangs on it.
    Machine load = Machine::caddy;
    // How far the line was stretched beyond its length last step, for
    // its damping.
    float stretch = 0.0f;
  };

  // The line, from the hook to the ring, metres.
  inline constexpr float sling_line = 8.0f;
  // How far to one side of the hook a machine may stand and still be
  // hooked, metres, and how much further than the line the ring may be.
  inline constexpr float sling_reach = 8.0f;
  inline constexpr float sling_slack = 1.5f;
  // The hook, in the helicopter's frame.
  inline Vec3 sling_hook () {
    return Vec3 (0.0f, 0.55f, 0.05f);
  }
  // The ring over each machine, and where its bridle's legs are made fast
  // to it (a bike's two, at its ends, twice over), in the machine's frame.
  Vec3 sling_ring (Machine machine);
  std::array<Vec3, 4> sling_legs (Machine machine);
  // How far below the helicopter's skids what hangs under it reaches
  // when the line is taut, about.
  inline constexpr float sling_hang = sling_line + 3.0f - 0.55f + 0.3f;

  // The line's pull on the ring, newtons, from the hook and the ring in
  // the world, over a step of `dt`; the helicopter takes the opposite at
  // the hook. Updates the sling's stretch.
  Vec3 sling_pull (Sling& sling, const Vec3& hook, const Vec3& ring, float dt);
}

#endif
