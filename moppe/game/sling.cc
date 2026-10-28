#include <moppe/game/sling.hh>

#include <algorithm>

namespace moppe::game {
  namespace {
    // Kept in a namespace of its own: the engine is a unity build.
    namespace sling_rig {
      // Newtons a metre, and a metre a second: some sixteen centimetres
      // of stretch under the Caddy's weight, a third of a metre under the
      // tractor's, and nearly critically damped between them and the
      // helicopter.
      constexpr float stiffness = 60000.0f;
      constexpr float damping = 5000.0f;
      // The most the line takes before it would part: nearly three times
      // the tractor's weight, so a hard jerk is still held.
      constexpr float strongest = 55000.0f;
    }
  }

  Vec3 sling_ring (Machine machine) {
    switch (machine) {
    case Machine::bike:
      return Vec3 (0.0f, 2.1f, 0.0f);
    case Machine::jeep:
      return Vec3 (0.0f, 2.8f, 0.05f);
    case Machine::tractor:
      return Vec3 (0.0f, 3.4f, -0.15f);
    case Machine::caddy:
      return Vec3 (0.0f, 3.0f, 0.30f);
    case Machine::helicopter:
      break;
    case Machine::mountain_bike:
      return Vec3 (0.0f, 2.0f, 0.05f);
    case Machine::long_john:
      return Vec3 (0.0f, 2.2f, 0.1f);
    case Machine::boat:
      return Vec3 (0.0f, 2.2f, 0.6f);
    }
    return Vec3 ();
  }

  std::array<Vec3, 4> sling_legs (Machine machine) {
    switch (machine) {
    case Machine::bike:
      // The fork's top clamp and the rack's rear corners.
      return { Vec3 (0.0f, 0.53f, 0.35f),
               Vec3 (0.0f, 0.53f, 0.35f),
               Vec3 (0.13f, 0.36f, -0.97f),
               Vec3 (-0.13f, 0.36f, -0.97f) };
    case Machine::jeep:
      // The roll cage's corners.
      return { Vec3 (0.72f, 0.95f, 0.55f),
               Vec3 (-0.72f, 0.95f, 0.55f),
               Vec3 (0.72f, 0.95f, -1.2f),
               Vec3 (-0.72f, 0.95f, -1.2f) };
    case Machine::tractor:
      // The front axle's ends and the rear mudguards.
      return { Vec3 (0.45f, 0.75f, 1.35f),
               Vec3 (-0.45f, 0.75f, 1.35f),
               Vec3 (0.75f, 1.25f, -0.75f),
               Vec3 (-0.75f, 1.25f, -0.75f) };
    case Machine::caddy:
      // The bonnet's front corners and the bed's back ones.
      return { Vec3 (0.72f, 0.62f, 1.70f),
               Vec3 (-0.72f, 0.62f, 1.70f),
               Vec3 (0.74f, 0.68f, -2.12f),
               Vec3 (-0.74f, 0.68f, -2.12f) };
    case Machine::helicopter:
      break;
    case Machine::mountain_bike:
      // The head tube and the seat post.
      return { Vec3 (0.0f, 0.75f, 0.4f),
               Vec3 (0.0f, 0.75f, 0.4f),
               Vec3 (0.0f, 0.8f, -0.25f),
               Vec3 (0.0f, 0.8f, -0.25f) };
    case Machine::long_john:
      // The bulkhead's top corners and the hoop's.
      return { Vec3 (0.19f, 0.39f, 0.81f),
               Vec3 (-0.19f, 0.39f, 0.81f),
               Vec3 (0.25f, 0.63f, -0.15f),
               Vec3 (-0.25f, 0.63f, -0.15f) };
    case Machine::boat:
      // The bow handle and the transom's corners.
      return { Vec3 (0.0f, 0.45f, 2.65f),
               Vec3 (0.0f, 0.45f, 2.65f),
               Vec3 (0.4f, 0.36f, -0.9f),
               Vec3 (-0.4f, 0.36f, -0.9f) };
    }
    return {};
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
