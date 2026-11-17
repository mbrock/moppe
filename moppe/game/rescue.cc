#include <moppe/game/rescue.hh>

#include <algorithm>
#include <cmath>

namespace moppe::game {
  namespace {
    // Kept in a namespace of its own: the engine is a unity build.
    namespace rescue_rig {
      // How lively a meeting is: the share of the closing speed they part
      // with, and the least parting speed of any real knock.
      constexpr float restitution = 0.55f;
      constexpr speed_t least_rebound = 0.8f * u::m / u::s;
      // How fast overlap is undone, per second, and at most.
      constexpr per_second_t overlap_rate = 4.0f / u::s;
      constexpr speed_t overlap_speed = 2.5f * u::m / u::s;
      // Machines push each other about more than they lift each other.
      constexpr float level_bias = 0.3f;

      // A machine stuck: how long the throttle may be open without getting
      // three metres on before it is stuck, and how often it heaves.
      constexpr meters_t free_distance = 3.0f * u::m;
      constexpr seconds_t stuck_after = 3.0f * u::s;
      constexpr seconds_t heave_every = 1.5f * u::s;

      // The rope: how hard it pulls (or holds off) for how far it is from
      // its length, how it is damped, and the most it gives, and how much
      // of the machine's weight it lifts while it pulls, as accelerations
      // of the machine. A generous helper, much stronger than any walker.
      constexpr quantity<one / pow<2> (u::s), float> rope_rate =
        22.0f / pow<2> (u::s);
      constexpr per_second_t rope_damping = 2.5f / u::s;
      constexpr metres_per_second_squared_t rope_most =
        7.0f * u::m / pow<2> (u::s);
      constexpr metres_per_second_squared_t rope_lift = 0.3f * standard_gravity;
      // The stretch over which the rope comes taut.
      constexpr meters_t rope_taking_up = 0.15f * u::m;
    }
  }

  using namespace rescue_rig;

  Vec3 MachineBody::point (const Vec3& local) const {
    const Vec3 left = normalized (cross (up, forward));
    return position + left * local[0] + up * local[1] + forward * local[2];
  }

  namespace {
    namespace rescue_rig {
      // A bumper in the world, and the weight behind it.
      struct Placed {
        Vec3 centre;
        meters_t radius {};
        kilograms_t mass = 1.0f * u::kg;
      };

      // A machine's bumpers, and those of what it tows.
      std::vector<Placed> placed_bumpers (const MachineBody& body) {
        std::vector<Placed> placed;
        const MachineKind& shape = machine_kind (body.machine);
        for (const Bumper& p : shape.bumpers)
          placed.push_back ({ body.point (p.centre), p.radius, body.mass });
        if (!body.towing)
          return placed;
        const Vec3 forward = body.towed_forward;
        const Vec3 up = body.towed_up;
        const Vec3 left = normalized (cross (up, forward));
        for (const Bumper& p : shape.towed_bumpers)
          placed.push_back ({ body.towed_position + left * p.centre[0] +
                                up * p.centre[1] + forward * p.centre[2],
                              p.radius,
                              body.towed_mass });
        return placed;
      }
    }
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
        meters_t deepest {};
        kilograms_t mass_a = a.mass, mass_b = b.mass;
        Vec3 normal, at;
        const std::vector<Placed> as = placed_bumpers (a);
        const std::vector<Placed> bs = placed_bumpers (b);
        for (const Placed& p : as)
          for (const Placed& q : bs) {
            const Vec3 pa = p.centre;
            const Vec3 qb = q.centre;
            const Vec3 apart = pa - qb;
            const float distance = length (apart);
            const meters_t overlap = p.radius + q.radius - distance * u::m;
            if (overlap <= deepest)
              continue;
            deepest = overlap;
            mass_a = p.mass;
            mass_b = q.mass;
            normal = distance > 1e-4f ? apart * (1.0f / distance)
                                      : Vec3 (0.0f, 1.0f, 0.0f);
            at = qb + normal * meters_value (q.radius - 0.5f * overlap);
          }
        if (deepest <= 0.0f * u::m)
          continue;
        normal[1] *= level_bias;
        if (length2 (normal) < 1e-6f)
          normal = Vec3 (0.0f, 0.0f, 1.0f);
        normalize (normal);

        const speed_t closing =
          dot (a.velocity - b.velocity, normal) * u::m / u::s;
        speed_t parting =
          std::min<speed_t> (deepest * overlap_rate, overlap_speed);
        if (closing < 0.0f * u::m / u::s)
          parting = std::max (parting,
                              std::max (-closing * restitution, least_rebound));
        if (closing >= parting)
          continue;
        const kilograms_t reduced = mass_a * mass_b / (mass_a + mass_b);
        // The momentum each takes, newton-seconds along the normal.
        const float blow =
          (reduced * (parting - closing)).numerical_value_in (u::N * u::s);
        found.push_back ({ .a = i,
                           .b = j,
                           .at = at,
                           .impulse = impulse (normal * blow),
                           .speed = std::max (0.0f * u::m / u::s, -closing) });
      }
    return found;
  }

  bool strain (Trouble& trouble,
               const Vec3& position,
               float throttle,
               seconds_t dt) {
    if (length2 (position - trouble.anchor) >
        meters_value (free_distance) * meters_value (free_distance)) {
      trouble = { .anchor = position };
      return false;
    }
    const bool trying = std::abs (throttle) > 0.4f;
    if (!trying) {
      // Let off, the next try heaves at once if it is still stuck; one
      // not yet stuck starts its count again, so the throttle's bursts
      // of a turn on the spot do not add up to being stuck.
      trouble.heave_wait = {};
      if (!trouble.stuck)
        trouble.strain = {};
      return false;
    }
    trouble.strain += dt;
    if (trouble.strain < stuck_after)
      return false;
    trouble.stuck = true;
    trouble.heave_wait -= dt;
    if (trouble.heave_wait > 0.0f * u::s)
      return false;
    trouble.heave_wait = heave_every;
    return true;
  }

  velocity_t
  heave (const Trouble& trouble, const Vec3& forward, float throttle) {
    Vec3 ahead (forward[0], 0.0f, forward[2]);
    ahead = length2 (ahead) > 1e-6f ? normalized (ahead) : Vec3 (0, 0, 1);
    const float n = static_cast<float> (trouble.heaves);
    const float along = std::min (2.0f + 0.6f * n, 5.5f);
    const float lift = std::min (1.8f + 0.4f * n, 4.0f);
    return velocity (ahead * (throttle < 0.0f ? -along : along) +
                     Vec3 (0.0f, lift, 0.0f));
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
    const meters_t stretch = distance * u::m - haul.rope;
    const speed_t closing =
      dot (body.velocity - walker_velocity, out) * u::m / u::s;
    const metres_per_second_squared_t pull =
      std::clamp<metres_per_second_squared_t> (
        rope_rate * stretch - rope_damping * closing, -rope_most, rope_most);
    const float taut = std::clamp (
      (stretch / rope_taking_up).numerical_value_in (one), 0.0f, 1.0f);
    // The rope's rates are accelerations; the machine's weight makes them
    // forces.
    return { .pull = force (out * newtons_value (pull * body.mass)),
             .lift = force (Vec3 (
               0.0f, newtons_value (rope_lift * taut * body.mass), 0.0f)) };
  }
}
