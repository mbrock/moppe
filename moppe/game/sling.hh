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
    meters_t stretch {};
    // How much line beyond its length was out when it was hooked on: the
    // winch takes it in over the next moments, so the load is lifted
    // rather than yanked.
    meters_t payout {};
  };

  // How fast the winch takes the line in.
  inline constexpr speed_t sling_reel = 1.5f * u::m / u::s;

  // The line, from the hook to the ring.
  inline constexpr meters_t sling_line = 8.0f * u::m;
  // How far to one side of the hook a machine may stand and still be
  // hooked, and how much further than the line the ring may be.
  inline constexpr meters_t sling_reach = 8.0f * u::m;
  inline constexpr meters_t sling_slack = 1.5f * u::m;
  // The hook, in the helicopter's frame.
  inline displacement_t sling_hook () {
    return displacement (Vec3 (0.0f, 0.55f, 0.05f));
  }
  // How far below the helicopter's skids what hangs under it reaches
  // when the line is taut, about.
  inline constexpr meters_t sling_hang =
    sling_line + 3.0f * u::m - 0.55f * u::m + 0.3f * u::m;

  // The line's pull on the ring, from the hook and the ring in
  // the world, over a step of `dt`; the helicopter takes the opposite at
  // the hook. Updates the sling's stretch.
  force_t
  sling_pull (Sling& sling, position_t hook, position_t ring, seconds_t dt);
}

#endif
