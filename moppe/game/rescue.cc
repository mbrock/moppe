#include <moppe/game/rescue.hh>

#include <algorithm>
#include <cmath>

namespace moppe::game {
  namespace {
    // Kept in a namespace of its own: the engine is a unity build.
    namespace rescue_rig {
      // How lively a meeting is: the share of the closing speed they part
      // with, and the least parting speed of any real knock.
      constexpr proportion_t restitution = 0.55f * proportion[one];
      constexpr speed_t least_rebound = 0.8f * u::m / u::s;
      // How fast overlap is undone, per second, and at most.
      constexpr per_second_t overlap_rate = 4.0f / u::s;
      constexpr speed_t overlap_speed = 2.5f * u::m / u::s;
      // Machines push each other about more than they lift each other.
      constexpr scale_factor_t level_bias = 0.3f * scale_factor[one];

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

  position_t MachineBody::point (const displacement_t& local) const {
    const Vec3& l = displacement_value (local);
    const Vec3& u = direction_value (up);
    const Vec3& f = direction_value (forward);
    const Vec3 left = normalized (cross (u, f));
    return moppe::position (position_value (position) + left * l[0] + u * l[1] +
                            f * l[2]);
  }

  namespace {
    namespace rescue_rig {
      // A bumper in the world, and the weight behind it.
      struct Placed {
        position_t centre {};
        meters_t radius {};
        kilograms_t mass = 1.0f * u::kg;
      };

      // A machine's bumpers, and those of what it tows.
      std::vector<Placed> placed_bumpers (const MachineBody& body) {
        std::vector<Placed> placed;
        const MachineKind& shape = machine_kind (body.machine);
        for (const Bumper& p :
             body.bumpers.empty () ? shape.bumpers : body.bumpers)
          placed.push_back ({ body.point (p.centre), p.radius, body.mass });
        if (!body.towing)
          return placed;
        const Vec3& forward = direction_value (body.towed_forward);
        const Vec3& up = direction_value (body.towed_up);
        const Vec3 left = normalized (cross (up, forward));
        for (const Bumper& p : shape.towed_bumpers) {
          const Vec3& c = displacement_value (p.centre);
          placed.push_back (
            { moppe::position (position_value (body.towed_position) +
                               left * c[0] + up * c[1] + forward * c[2]),
              p.radius,
              body.towed_mass });
        }
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
        if (length2 (position_value (a.position) -
                     position_value (b.position)) > 10.0f * 10.0f)
          continue;
        // The deepest overlap of any two of their spheres.
        meters_t deepest {};
        kilograms_t mass_a = a.mass, mass_b = b.mass;
        Vec3 normal;
        position_t at {};
        const std::vector<Placed> as = placed_bumpers (a);
        const std::vector<Placed> bs = placed_bumpers (b);
        for (const Placed& p : as)
          for (const Placed& q : bs) {
            const Vec3& pa = position_value (p.centre);
            const Vec3& qb = position_value (q.centre);
            const Vec3 apart = pa - qb;
            const meters_t distance = length (apart) * u::m;
            const meters_t overlap = p.radius + q.radius - distance;
            if (overlap <= deepest)
              continue;
            deepest = overlap;
            mass_a = p.mass;
            mass_b = q.mass;
            normal = distance > 1e-4f * u::m
                       ? apart * (1.0f / meters_value (distance))
                       : Vec3 (0.0f, 1.0f, 0.0f);
            at = moppe::position (
              qb + normal * meters_value (q.radius - 0.5f * overlap));
          }
        if (deepest <= 0.0f * u::m)
          continue;
        normal[1] *= level_bias.numerical_value_in (one);
        if (length2 (normal) < 1e-6f)
          normal = Vec3 (0.0f, 0.0f, 1.0f);
        normalize (normal);

        const speed_t closing =
          dot (velocity_value (a.velocity) - velocity_value (b.velocity),
               normal) *
          u::m / u::s;
        speed_t parting =
          std::min<speed_t> (deepest * overlap_rate, overlap_speed);
        if (closing < 0.0f * u::m / u::s)
          parting = std::max (parting,
                              std::max<speed_t> (quantity_cast<isq::speed> (
                                                   -closing * restitution),
                                                 least_rebound));
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
               position_t position,
               control_signal_t throttle,
               seconds_t dt) {
    if (length2 (position_value (position) - position_value (trouble.anchor)) >
        meters_value (free_distance) * meters_value (free_distance)) {
      trouble = { .anchor = position };
      return false;
    }
    const bool trying = abs (throttle) > 0.4f * control_signal[one];
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

  velocity_t heave (const Trouble& trouble,
                    direction_t forward,
                    control_signal_t throttle) {
    const Vec3& way = direction_value (forward);
    Vec3 ahead (way[0], 0.0f, way[2]);
    ahead = length2 (ahead) > 1e-6f ? normalized (ahead) : Vec3 (0, 0, 1);
    const float n = static_cast<float> (trouble.heaves);
    const speed_t along = std::min<speed_t> (
      2.0f * u::m / u::s + 0.6f * u::m / u::s * n, 5.5f * u::m / u::s);
    const speed_t lift = std::min<speed_t> (
      1.8f * u::m / u::s + 0.4f * u::m / u::s * n, 4.0f * u::m / u::s);
    const speed_t signed_along =
      throttle < 0.0f * control_signal[one] ? -along : along;
    return velocity (ahead * speed_value (signed_along) +
                     Vec3 (0.0f, speed_value (lift), 0.0f));
  }

  HaulForce haul_force (const Haul& haul,
                        const MachineBody& body,
                        position_t feet,
                        velocity_t walker_velocity) {
    const position_t tie = body.point (haul.tie);
    Vec3 out = position_value (feet) - position_value (tie);
    out[1] = 0.0f;
    const meters_t distance = length (out) * u::m;
    if (distance < 1e-4f * u::m)
      return {};
    out *= 1.0f / meters_value (distance);
    // Held short, the rope keeps the machine at its length: it pulls it
    // after a walker going away, and holds it off one coming nearer or
    // standing in its way, so that it never rolls over them.
    const meters_t stretch = distance - haul.rope;
    const speed_t closing =
      dot (velocity_value (body.velocity) - velocity_value (walker_velocity),
           out) *
      u::m / u::s;
    const metres_per_second_squared_t pull =
      std::clamp<metres_per_second_squared_t> (
        rope_rate * stretch - rope_damping * closing, -rope_most, rope_most);
    const proportion_t taut =
      std::clamp (
        (stretch / rope_taking_up).numerical_value_in (one), 0.0f, 1.0f) *
      proportion[one];
    // The rope's rates are accelerations; the machine's weight makes them
    // forces.
    return { .pull = force (out * newtons_value (pull * body.mass)),
             .lift = force (Vec3 (0.0f,
                                  newtons_value (quantity_cast<isq::force> (
                                    rope_lift * taut * body.mass)),
                                  0.0f)) };
  }
}
