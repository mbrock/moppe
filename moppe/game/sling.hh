#ifndef MOPPE_GAME_SLING_HH
#define MOPPE_GAME_SLING_HH

#include <moppe/gfx/math.hh>

#include <array>

namespace moppe::game {
  // The helicopter's slung load: the Caddy, hung on a line from a hook
  // under the helicopter's belly, with the motocross in its bed (the
  // hardtop off and the tailgate down) and the jeep, as a model of
  // itself, on its passenger seat.
  //
  // Taking the helicopter's seat rigs it: the Caddy, wherever it is, is
  // set down beside the helicopter if it is not there already, the bike
  // and the jeep are stowed in it, and the line is hooked on. The line is
  // a stiff spring that only pulls, from the hook to a ring above the
  // Caddy, where the four legs of its bridle meet; the ring stands over
  // the Caddy's centre of mass, so that it hangs level. The flight
  // computer knows what hangs under it and lifts it too. The mount key
  // in the air lets the load go; the Caddy falls, and once it stands
  // still on the ground (or is let go of there) the bike and the jeep are
  // unloaded beside it.
  struct Sling {
    // The line is on the hook.
    bool hooked = false;
    // The bike and the jeep ride in the Caddy.
    bool laden = false;
    // How far the line was stretched beyond its length last step, for
    // its damping.
    float stretch = 0.0f;
  };

  // The line, from the hook to the ring, metres.
  inline constexpr float sling_line = 8.0f;
  // The hook, in the helicopter's frame; the ring, and where the bridle's
  // legs are made fast to the Caddy (the bonnet's front corners and the
  // bed's back ones), in the Caddy's.
  inline Vec3 sling_hook () {
    return Vec3 (0.0f, 0.55f, 0.05f);
  }
  inline Vec3 sling_ring () {
    return Vec3 (0.0f, 3.0f, 0.30f);
  }
  inline std::array<Vec3, 4> sling_legs () {
    return { Vec3 (0.72f, 0.62f, 1.70f),
             Vec3 (-0.72f, 0.62f, 1.70f),
             Vec3 (0.74f, 0.68f, -2.12f),
             Vec3 (-0.74f, 0.68f, -2.12f) };
  }
  // How far below the helicopter's skids the Caddy's wheels hang when
  // the line is taut.
  inline constexpr float sling_hang = sling_line + 3.0f - 0.55f + 0.3f;

  // The line's pull on the ring, newtons, from the hook and the ring in
  // the world, over a step of `dt`; the helicopter takes the opposite at
  // the hook. Updates the sling's stretch.
  Vec3 sling_pull (Sling& sling, const Vec3& hook, const Vec3& ring, float dt);
}

#endif
