#include <moppe/game/sling.hh>

#include <algorithm>

namespace moppe::game {
  namespace {
    // Kept in a namespace of its own: the engine is a unity build.
    namespace sling_rig {
      // Per metre, and per metre a second: some sixteen centimetres
      // of stretch under the Caddy's weight, a third of a metre under the
      // tractor's, and nearly critically damped between them and the
      // helicopter.
      constexpr quantity<u::N / u::m, float> stiffness = 60000.0f * u::N / u::m;
      constexpr quantity<u::N * u::s / u::m, float> damping =
        5000.0f * u::N * u::s / u::m;
      // The most the line takes before it would part: nearly three times
      // the tractor's weight, so a hard jerk is still held.
      constexpr newtons_t strongest = 55000.0f * u::N;
    }
  }

  force_t
  sling_pull (Sling& sling, position_t hook, position_t ring, seconds_t dt) {
    using namespace sling_rig;
    const Vec3 along = position_value (hook) - position_value (ring);
    const meters_t span = length (along) * u::m;
    const meters_t stretch = span - (sling_line + sling.payout);
    sling.payout = std::max (meters_t {}, sling.payout - sling_reel * dt);
    const speed_t rate =
      dt > 0.0f * u::s ? (stretch - std::max (sling.stretch, 0.0f * u::m)) / dt
                       : 0.0f * u::m / u::s;
    sling.stretch = stretch;
    if (stretch <= 0.0f * u::m || span < 1e-4f * u::m)
      return force (Vec3 ());
    const newtons_t tension = std::clamp<newtons_t> (
      stiffness * stretch + damping * rate, 0.0f * u::N, strongest);
    return force (along * (newtons_value (tension) / meters_value (span)));
  }
}
