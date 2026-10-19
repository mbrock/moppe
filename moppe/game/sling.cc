#include <moppe/game/sling.hh>

#include <algorithm>

namespace moppe::game {
  namespace {
    // Kept in a namespace of its own: the engine is a unity build.
    namespace sling_rig {
      // Newtons a metre, and a metre a second: some sixteen centimetres
      // of stretch under the Caddy's weight, and nearly critically damped
      // between it and the helicopter.
      constexpr float stiffness = 60000.0f;
      constexpr float damping = 5000.0f;
      // The most the line takes before it would part: some six times the
      // Caddy's weight, so a hard jerk is still held.
      constexpr float strongest = 55000.0f;
    }
  }

  Vec3 sling_pull (Sling& sling, const Vec3& hook, const Vec3& ring, float dt) {
    using namespace sling_rig;
    const Vec3 along = hook - ring;
    const float span = length (along);
    const float stretch = span - sling_line;
    const float rate =
      dt > 0.0f ? (stretch - std::max (sling.stretch, 0.0f)) / dt : 0.0f;
    sling.stretch = stretch;
    if (stretch <= 0.0f || span < 1e-4f)
      return Vec3 ();
    const float tension =
      std::clamp (stiffness * stretch + damping * rate, 0.0f, strongest);
    return along * (tension / span);
  }
}
