#include <moppe/game/rescue.hh>

#include <algorithm>
#include <cmath>

namespace moppe::game {
  namespace {
    // Kept in a namespace of its own: the engine is a unity build.
    namespace rescue_rig {
      // The bumpers, laid over each machine's Box3D hulls: the Trail 90's
      // wheels and rider, the jeep's tub, the
      // tractor's big rear wheels and its bonnet, the Caddy's long body,
      // the helicopter's cabin and tail boom, and the mountainbike's
      // wheels and rider. The rotor meets nothing.
      const Bumper bike_bumpers[] = {
        { Vec3 (0.0f, 0.05f, 0.45f), 0.36f },
        { Vec3 (0.0f, 0.1f, -0.5f), 0.38f },
        { Vec3 (0.0f, 0.85f, -0.15f), 0.32f },
      };
      const Bumper jeep_bumpers[] = {
        { Vec3 (0.0f, 0.5f, 1.15f), 0.85f },
        { Vec3 (0.0f, 0.5f, 0.0f), 0.9f },
        { Vec3 (0.0f, 0.5f, -1.15f), 0.85f },
      };
      const Bumper tractor_bumpers[] = {
        { Vec3 (0.0f, 0.8f, -0.8f), 1.0f },
        { Vec3 (0.0f, 0.75f, 0.85f), 0.75f },
      };
      const Bumper caddy_bumpers[] = {
        { Vec3 (0.0f, 0.45f, 1.3f), 0.82f },
        { Vec3 (0.0f, 0.45f, 0.0f), 0.85f },
        { Vec3 (0.0f, 0.5f, -1.4f), 0.82f },
      };
      const Bumper mountain_bike_bumpers[] = {
        { Vec3 (0.0f, 0.05f, 0.4f), 0.36f },
        { Vec3 (0.0f, 0.05f, -0.4f), 0.36f },
        { Vec3 (0.0f, 0.85f, -0.1f), 0.32f },
      };
      // The Long John's back wheel, its box, its front wheel, and the
      // rider over the back.
      const Bumper long_john_bumpers[] = {
        { Vec3 (0.0f, 0.0f, -0.95f), 0.36f },
        { Vec3 (0.0f, -0.05f, 0.3f), 0.4f },
        { Vec3 (0.0f, -0.05f, 0.9f), 0.3f },
        { Vec3 (0.0f, 0.8f, -0.6f), 0.3f },
      };
      // The boat's long narrow hull, keel to gunwale.
      const Bumper boat_bumpers[] = {
        { Vec3 (0.0f, 0.22f, -0.5f), 0.45f },
        { Vec3 (0.0f, 0.22f, 0.4f), 0.45f },
        { Vec3 (0.0f, 0.22f, 1.3f), 0.45f },
        { Vec3 (0.0f, 0.25f, 2.2f), 0.4f },
      };
      const Bumper helicopter_bumpers[] = {
        { Vec3 (0.0f, 1.3f, 0.3f), 1.25f },
        { Vec3 (0.0f, 1.7f, -2.4f), 0.5f },
        { Vec3 (0.0f, 1.75f, -4.2f), 0.5f },
      };

      // How lively a meeting is: the share of the closing speed they part
      // with, and the least parting speed of any real knock.
      constexpr float restitution = 0.55f;
      constexpr float least_rebound = 0.8f;
      // How fast overlap is undone, per second, and at most.
      constexpr float overlap_rate = 4.0f;
      constexpr float overlap_speed = 2.5f;
      // Machines push each other about more than they lift each other.
      constexpr float level_bias = 0.3f;

      // A machine stuck: how long the throttle may be open without getting
      // three metres on before it is stuck, and how often it heaves.
      constexpr float free_distance = 3.0f;
      constexpr float stuck_after = 3.0f;
      constexpr float heave_every = 1.5f;

      // The rope: how hard it pulls (or holds off) for how far it is from
      // its length, how it is damped, and the most it gives, and how much
      // of the machine's weight it lifts while it pulls, as accelerations
      // of the machine. A generous helper, much stronger than any walker.
      constexpr float rope_rate = 22.0f;
      constexpr float rope_damping = 2.5f;
      constexpr float rope_most = 7.0f;
      constexpr float rope_lift = 0.3f * 9.81f;
    }
  }

  using namespace rescue_rig;

  const char* machine_name (Machine machine) {
    switch (machine) {
    case Machine::bike:
      return "the bike";
    case Machine::jeep:
      return "the jeep";
    case Machine::tractor:
      return "the tractor";
    case Machine::caddy:
      return "the Caddy";
    case Machine::helicopter:
      return "the helicopter";
    case Machine::mountain_bike:
      return "the mountainbike";
    case Machine::long_john:
      return "the Long John";
    case Machine::boat:
      return "the boat";
    }
    return "it";
  }

  std::span<const Bumper> bumpers (Machine machine) {
    switch (machine) {
    case Machine::bike:
      return bike_bumpers;
    case Machine::jeep:
      return jeep_bumpers;
    case Machine::tractor:
      return tractor_bumpers;
    case Machine::caddy:
      return caddy_bumpers;
    case Machine::helicopter:
      return helicopter_bumpers;
    case Machine::mountain_bike:
      return mountain_bike_bumpers;
    case Machine::long_john:
      return long_john_bumpers;
    case Machine::boat:
      return boat_bumpers;
    }
    return {};
  }

  Vec3 machine_centre (Machine machine) {
    switch (machine) {
    case Machine::bike:
      return Vec3 (0.0f, 0.42f, 0.0f);
    case Machine::jeep:
      return Vec3 (0.0f, 0.15f, 0.05f);
    case Machine::tractor:
      return Vec3 (0.0f, 0.78f, -0.15f);
    case Machine::caddy:
      return Vec3 (0.0f, 0.24f, 0.30f);
    case Machine::helicopter:
      return Vec3 (0.0f, 1.15f, 0.05f);
    case Machine::mountain_bike:
      return Vec3 (0.0f, 0.55f, 0.0f);
    case Machine::long_john:
      return Vec3 (0.0f, 0.2f, -0.2f);
    case Machine::boat:
      return Vec3 (0.0f, 0.2f, 0.15f);
    }
    return Vec3 ();
  }

  Vec3 tie_point (Machine machine, bool front) {
    const float end = front ? 1.0f : -1.0f;
    switch (machine) {
    case Machine::bike:
      // The front fender's stays, or the rack's rear rail.
      return front ? Vec3 (0.0f, 0.2f, 0.85f) : Vec3 (0.0f, 0.36f, -1.0f);
    case Machine::jeep:
      return Vec3 (0.0f, 0.35f, end * 1.95f);
    case Machine::tractor:
      // The front axle's beam, or the drawbar under the seat.
      return front ? Vec3 (0.0f, 0.45f, 1.55f) : Vec3 (0.0f, 0.55f, -1.45f);
    case Machine::caddy:
      return Vec3 (0.0f, 0.25f, front ? 2.06f : -2.22f);
    case Machine::helicopter:
      return Vec3 (0.0f, 0.3f, end * 1.6f);
    case Machine::mountain_bike:
      // The head tube, or the seat post.
      return front ? Vec3 (0.0f, 0.45f, 0.35f) : Vec3 (0.0f, 0.5f, -0.25f);
    case Machine::long_john:
      // The front bulkhead, or the seat post.
      return front ? Vec3 (0.0f, 0.3f, 0.82f) : Vec3 (0.0f, 0.4f, -0.72f);
    case Machine::boat:
      // The carrying handle on the bow, or the transom's top.
      return front ? Vec3 (0.0f, 0.45f, 2.65f) : Vec3 (0.0f, 0.35f, -0.95f);
    }
    return Vec3 ();
  }

  Vec3 MachineBody::point (const Vec3& local) const {
    const Vec3 left = normalized (cross (up, forward));
    return position + left * local[0] + up * local[1] + forward * local[2];
  }

  std::vector<Bump> find_bumps (std::span<const MachineBody> bodies) {
    std::vector<Bump> found;
    const int count = static_cast<int> (bodies.size ());
    for (int i = 0; i < count; ++i)
      for (int j = i + 1; j < count; ++j) {
        const MachineBody& a = bodies[i];
        const MachineBody& b = bodies[j];
        if (!a.ours && !b.ours)
          continue;
        if (length2 (a.position - b.position) > 10.0f * 10.0f)
          continue;
        // The deepest overlap of any two of their spheres.
        float deepest = 0.0f;
        Vec3 normal, at;
        for (const Bumper& p : bumpers (a.machine))
          for (const Bumper& q : bumpers (b.machine)) {
            const Vec3 pa = a.point (p.centre);
            const Vec3 qb = b.point (q.centre);
            const Vec3 apart = pa - qb;
            const float distance = length (apart);
            const float overlap = p.radius + q.radius - distance;
            if (overlap <= deepest)
              continue;
            deepest = overlap;
            normal = distance > 1e-4f ? apart * (1.0f / distance)
                                      : Vec3 (0.0f, 1.0f, 0.0f);
            at = qb + normal * (q.radius - 0.5f * overlap);
          }
        if (deepest <= 0.0f)
          continue;
        normal[1] *= level_bias;
        if (length2 (normal) < 1e-6f)
          normal = Vec3 (0.0f, 0.0f, 1.0f);
        normalize (normal);

        const float closing = dot (a.velocity - b.velocity, normal);
        float parting = std::min (deepest * overlap_rate, overlap_speed);
        if (closing < 0.0f)
          parting = std::max (parting,
                              std::max (-closing * restitution, least_rebound));
        if (closing >= parting)
          continue;
        const float reduced = a.mass * b.mass / (a.mass + b.mass);
        found.push_back ({ .a = i,
                           .b = j,
                           .at = at,
                           .impulse = normal * (reduced * (parting - closing)),
                           .speed = std::max (0.0f, -closing) });
      }
    return found;
  }

  bool
  strain (Trouble& trouble, const Vec3& position, float throttle, float dt) {
    if (length2 (position - trouble.anchor) > free_distance * free_distance) {
      trouble = { .anchor = position };
      return false;
    }
    const bool trying = std::abs (throttle) > 0.4f;
    if (!trying) {
      // Let off, the next try heaves at once if it is still stuck.
      trouble.heave_wait = 0.0f;
      return false;
    }
    trouble.strain += dt;
    if (trouble.strain < stuck_after)
      return false;
    trouble.stuck = true;
    trouble.heave_wait -= dt;
    if (trouble.heave_wait > 0.0f)
      return false;
    trouble.heave_wait = heave_every;
    return true;
  }

  Vec3 heave (const Trouble& trouble, const Vec3& forward, float throttle) {
    Vec3 ahead (forward[0], 0.0f, forward[2]);
    ahead = length2 (ahead) > 1e-6f ? normalized (ahead) : Vec3 (0, 0, 1);
    const float n = static_cast<float> (trouble.heaves);
    const float along = std::min (2.0f + 0.6f * n, 5.5f);
    const float lift = std::min (1.8f + 0.4f * n, 4.0f);
    return ahead * (throttle < 0.0f ? -along : along) + Vec3 (0.0f, lift, 0.0f);
  }

  HaulForce haul_force (const Haul& haul,
                        const MachineBody& body,
                        const Vec3& feet,
                        const Vec3& walker_velocity) {
    const Vec3 tie = body.point (haul.tie);
    Vec3 out = feet - tie;
    out[1] = 0.0f;
    const float distance = length (out);
    if (distance < 1e-4f)
      return {};
    out *= 1.0f / distance;
    // Held short, the rope keeps the machine at its length: it pulls it
    // after a walker going away, and holds it off one coming nearer or
    // standing in its way, so that it never rolls over them.
    const float stretch = distance - haul.rope;
    const float closing = dot (body.velocity - walker_velocity, out);
    const float pull = std::clamp (
      rope_rate * stretch - rope_damping * closing, -rope_most, rope_most);
    const float taut = std::clamp (stretch / 0.15f, 0.0f, 1.0f);
    return { .pull = out * (pull * body.mass),
             .lift = Vec3 (0.0f, rope_lift * taut * body.mass, 0.0f) };
  }
}
