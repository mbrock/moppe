#include <moppe/mov/character.hh>

#include <moppe/mov/box3d_vec.hh>

#include <algorithm>
#include <cfloat>
#include <cmath>

#include <box3d/box3d.h>

namespace moppe::mov {
  namespace {
    // Toward the wish, with no input, and in the air.
    constexpr metres_per_second_squared_t ground_acceleration =
      40.0f * u::m / pow<2> (u::s);
    constexpr metres_per_second_squared_t ground_braking =
      28.0f * u::m / pow<2> (u::s);
    constexpr metres_per_second_squared_t air_acceleration =
      7.0f * u::m / pow<2> (u::s);
    constexpr seconds_t coyote_time = 0.12f * u::s;
    constexpr seconds_t jump_buffer_time = 0.14f * u::s;
    // A grounded body follows ground falling away by this much per step
    // instead of launching off every crest.
    constexpr meters_t snap_reach = 0.35f * u::m;
    // Feet that rise or fall this much more than the ground's slope
    // predicts stepped onto or off something; the slope itself needs no
    // easing.
    constexpr meters_t step_snap = 0.04f * u::m;
    constexpr damping_t step_ease = 16.0f / u::s;

    // The capsule's hemisphere centres above the feet.
    const displacement_t capsule_low = displacement (Vec3 (
      0.0f, meters_value (Character::step_height + Character::radius), 0.0f));
    const displacement_t capsule_high = displacement (
      Vec3 (0.0f, meters_value (Character::height - Character::radius), 0.0f));

    // How fast a body climbs.
    speed_t climb_of (const velocity_t& v) {
      return velocity_value (v)[1] * (u::m / u::s);
    }

    meters_t terrain_height (const map::SurfaceGeometry& surface,
                             const plan_position_t& at) {
      return ground_height (
               surface, plan_value (at).x (), plan_value (at).z ()) *
             u::m;
    }

    // The heightfield's own slope, by central differences over a span a
    // little under a foot, so the plane agrees with the heights the feet
    // are set on rather than with the smoothed shading normals.
    direction_t terrain_slope_normal (const map::SurfaceGeometry& surface,
                                      const plan_position_t& at) {
      constexpr meters_t e = 0.25f * u::m;
      const plan_displacement_t east = plan_displacement (e, 0.0f * u::m);
      const plan_displacement_t south = plan_displacement (0.0f * u::m, e);
      const meters_t dx = terrain_height (surface, at + east) -
                          terrain_height (surface, at - east);
      const meters_t dz = terrain_height (surface, at + south) -
                          terrain_height (surface, at - south);
      return direction_of (displacement (Vec3 (
        -meters_value (dx), meters_value (2.0f * e), -meters_value (dz))));
    }

    // `v` without its vertical part.
    Vec3 horizontal (const Vec3& v) {
      return Vec3 (v[0], 0.0f, v[2]);
    }
    template <class Q>
    Q horizontal (const Q& v) {
      const Vec3 value = v.numerical_value_in (v.unit);
      return Vec3 (value[0], 0.0f, value[2]) * Q::reference;
    }

    // Moves `v` toward `target` by at most `amount`.
    velocity_t
    approach (const velocity_t& v, const velocity_t& target, speed_t amount) {
      const velocity_t gap = target - v;
      const speed_t distance = gap.magnitude ();
      if (distance <= amount || distance <= 0.0f * (u::m / u::s))
        return target;
      return v + gap * (amount / distance);
    }

  }

  void Character::spawn (position_t feet) {
    m_state = State {};
    m_state.position = feet;
    m_state.velocity = moppe::velocity (Vec3 ());
  }

  void Character::spawn (position_t feet, velocity_t velocity) {
    spawn (feet);
    m_state.velocity = velocity;
    m_state.grounded = false;
  }

  Character::Support Character::probe (const position_t& feet,
                                       meters_t reach_down,
                                       const map::SurfaceGeometry& surface,
                                       const TrunkField* obstacles) const {
    Support support;
    const meters_t ground = terrain_height (surface, plan (feet));
    if (ground <= height_of (feet) + step_height &&
        ground >= height_of (feet) - reach_down) {
      support.found = true;
      support.height = ground;
      support.normal = terrain_slope_normal (surface, plan (feet));
    }
    if (!obstacles)
      return support;

    // A rock's top is found by a narrow sphere dropped from step height:
    // narrower than the capsule, so a body is not held up by a flank it is
    // merely brushing.
    constexpr meters_t foot = 0.18f * u::m;
    const position_t from =
      feet + quantity_cast<isq::position_vector> (displacement (
               Vec3 (0.0f, meters_value (step_height + foot), 0.0f)));
    const GroundHit hit =
      obstacles->cast_down (from, foot, step_height + reach_down);
    if (hit.hit && walkable (hit.normal) &&
        (!support.found || height_of (hit.point) > support.height)) {
      support.found = true;
      support.height = height_of (hit.point);
      support.normal = hit.normal;
    }
    return support;
  }

  void Character::slide (position_t& feet,
                         const displacement_t& delta,
                         bool grounded,
                         const map::SurfaceGeometry& surface,
                         const TrunkField* obstacles) {
    // Box3D's mover loop: gather the planes the capsule touches, solve the
    // displacement that honours them, sweep it so nothing tunnels, repeat
    // from where it stopped.
    const position_t target =
      feet + quantity_cast<isq::position_vector> (delta);
    const meters_t reach = delta.magnitude () + 0.05f * u::m;
    std::vector<b3CollisionPlane> planes;
    for (int iteration = 0; iteration < 4; ++iteration) {
      m_planes.clear ();
      if (obstacles)
        obstacles->collide_mover (
          feet, capsule_low, capsule_high, radius, m_planes);

      // The heightfield is not a Box3D shape; its planes come from the
      // ground under and around the capsule's lower hemisphere. On ground
      // that can be walked the hemisphere floats clear of them, so only a
      // bank too steep to climb ever pushes back.
      const position_t low =
        feet + quantity_cast<isq::position_vector> (capsule_low);
      for (int k = 0; k < 7; ++k) {
        position_t at = low;
        if (k > 0) {
          const radians_t angle =
            static_cast<float> (k - 1) * (PI2 / 6.0f) * u::rad;
          const direction_t way =
            unit_direction (Vec3 (libm::cos (angle), 0.0f, libm::sin (angle)));
          at += quantity_cast<isq::position_vector> (along (way, radius));
        }
        const direction_t normal = terrain_slope_normal (surface, plan (at));
        const position_t ground =
          at_height (plan (at), terrain_height (surface, plan (at)));
        const meters_t depth =
          radius - dot (direction_value (normal),
                        (low - ground).numerical_value_in (si::metre)) *
                     u::m;
        if (depth > -reach)
          m_planes.push_back ({ normal, depth });
      }

      planes.clear ();
      for (const MoverPlane& plane : m_planes) {
        direction_t normal = plane.normal;
        meters_t depth = plane.depth;
        if (grounded) {
          // Standing, every contact is a wall: the probe, not the solver,
          // decides the height of the feet, so a push up a rock's flank
          // must not become a climb.
          const proportion_t flat =
            length (plan (direction_value (normal))) * proportion[one];
          if (flat < 0.05f * proportion[one])
            continue;
          normal = unit_direction (horizontal (direction_value (normal)) *
                                   (1.0f / flat.numerical_value_in (one)));
          depth /=
            std::max (flat, 0.25f * proportion[one]).numerical_value_in (one);
        }
        planes.push_back (
          { { to_b3 (direction_value (normal)), meters_value (depth) },
            FLT_MAX,
            0.0f,
            true });
      }

      const b3PlaneSolverResult solved =
        b3SolvePlanes (to_b3 ((target - feet).numerical_value_in (si::metre)),
                       planes.data (),
                       static_cast<int> (planes.size ()));
      displacement_t move = displacement (from_b3 (solved.delta));
      if (grounded)
        move = horizontal (move);
      const proportion_t fraction =
        obstacles ? obstacles->cast_mover (
                      feet, capsule_low, capsule_high, radius, move)
                  : 1.0f * proportion[one];
      const displacement_t moved = move * fraction.numerical_value_in (one);
      feet += quantity_cast<isq::position_vector> (moved);
      if (length2 (displacement_value (moved)) < 1e-8f)
        break;
    }

    velocity_t& v = m_state.velocity;
    v = moppe::velocity (
      from_b3 (b3ClipVector (to_b3 (velocity_value (v)),
                             planes.data (),
                             static_cast<int> (planes.size ()))));
  }

  void Character::step (seconds_t dt,
                        const CharacterIntent& intent,
                        const map::SurfaceGeometry& surface,
                        const TrunkField* obstacles) {
    State& s = m_state;
    position_t& feet = s.position;
    velocity_t& v = s.velocity;
    constexpr speed_t still = 0.0f * (u::m / u::s);

    s.jump_buffer = intent.jump ? jump_buffer_time
                                : std::max (0.0f * u::s, s.jump_buffer - dt);
    if (!s.grounded) {
      s.coyote = std::max (0.0f * u::s, s.coyote - dt);
      s.airborne_time += dt;
    }
    s.since_landing += dt;
    s.step_offset =
      quantity_cast<isq::length> (s.step_offset * decay (step_ease, dt));

    velocity_t wish = horizontal (intent.wish_velocity);
    if (s.grounded) {
      // Climbing costs pace: up a slope at the walkable limit a body makes
      // a little over half its level speed.
      const direction_t downhill = horizontal (s.ground_normal);
      const speed_t uphill =
        -dot (velocity_value (wish), direction_value (downhill)) *
        (u::m / u::s);
      if (uphill > still && length2 (velocity_value (wish)) > 0.0f)
        wish *=
          1.0f -
          0.45f *
            std::min (1.0f,
                      (uphill / wish.magnitude ()).numerical_value_in (one) /
                        0.77f);
      const bool steering = length2 (velocity_value (wish)) > 1e-4f;
      v = approach (horizontal (v),
                    wish,
                    (steering ? ground_acceleration : ground_braking) * dt);
    } else {
      // A little air control: enough to aim a jump, never to fly.
      if (length2 (velocity_value (wish)) > 1e-4f) {
        const velocity_t flat = horizontal (v);
        const velocity_t steered = approach (flat, wish, air_acceleration * dt);
        // Never brake a long jump just because the stick asks for less.
        const speed_t keep = std::max (flat.magnitude (), wish.magnitude ());
        const velocity_t limited =
          steered.magnitude () > keep && steered.magnitude () > still
            ? steered * (keep / steered.magnitude ())
            : steered;
        v = moppe::velocity (Vec3 (velocity_value (limited)[0],
                                   velocity_value (v)[1],
                                   velocity_value (limited)[2]));
      }
      velocity_value (v)[1] -= speed_value (gravity * dt);
    }

    bool jumped = false;
    if (s.jump_buffer > 0.0f * u::s && (s.grounded || s.coyote > 0.0f * u::s)) {
      velocity_value (v)[1] = speed_value (jump_speed);
      s.grounded = false;
      s.coyote = 0.0f * u::s;
      s.jump_buffer = 0.0f * u::s;
      s.airborne_time = 0.0f * u::s;
      jumped = true;
    }

    const bool was_grounded = s.grounded;
    const position_t before = feet;
    slide (feet, v * dt, was_grounded, surface, obstacles);

    Support support =
      probe (feet, was_grounded ? snap_reach : 0.0f * u::m, surface, obstacles);
    if (was_grounded && support.found && too_steep (support.normal) &&
        support.height > height_of (before) + 0.01f * u::m) {
      // Walking into a bank too steep to stand on: it acts as a wall, so
      // the body slides along its foot instead of hopping up and falling
      // back. The bank's downhill direction is the wall's normal.
      Vec3 wall = horizontal (direction_value (support.normal));
      if (length2 (wall) > 1e-8f) {
        normalize (wall);
        displacement_t moved = horizontal (feet - before);
        moved -= displacement (
          wall * std::min (0.0f, dot (displacement_value (moved), wall)));
        const Vec3& start = position_value (before);
        feet =
          moppe::position (Vec3 (start[0] + displacement_value (moved)[0],
                                 position_value (feet)[1],
                                 start[2] + displacement_value (moved)[2]));
        v -= moppe::velocity (wall *
                              std::min (0.0f, dot (velocity_value (v), wall)));
        support = probe (feet, snap_reach, surface, obstacles);
        if (support.found && too_steep (support.normal) &&
            support.height > height_of (before) + 0.01f * u::m) {
          feet = before;
          v = moppe::velocity (Vec3 ());
          support = probe (feet, snap_reach, surface, obstacles);
        }
      }
    }

    const bool walkable = support.found && Character::walkable (support.normal);
    if (was_grounded) {
      if (walkable) {
        const Vec3& n = direction_value (s.ground_normal);
        const displacement_t moved = feet - before;
        const meters_t predicted = -(n[0] * displacement_value (moved)[0] +
                                     n[2] * displacement_value (moved)[2]) /
                                   n[1] * u::m;
        const meters_t rise = support.height - height_of (feet);
        if (abs (rise - predicted) > step_snap)
          s.step_offset -= rise - predicted;
        position_value (feet)[1] = meters_value (support.height);
        s.ground_normal = support.normal;
      } else {
        // Off an edge, or onto ground too steep to stand on.
        s.grounded = false;
        s.coyote = coyote_time;
        s.airborne_time = 0.0f * u::s;
        velocity_value (v)[1] = std::min<float> (velocity_value (v)[1], 0.0f);
      }
    } else if (!jumped && climb_of (v) <= still && walkable &&
               support.height >= height_of (feet) - 1e-4f * u::m) {
      s.landing_speed = -climb_of (v);
      s.since_landing = 0.0f * u::s;
      s.grounded = true;
      s.ground_normal = support.normal;
      position_value (feet)[1] = meters_value (support.height);
      velocity_value (v)[1] = 0.0f;
    }

    // Nothing passes below the heightfield. On a bank too steep to stand on
    // this is what turns a fall into a slide: the velocity loses its
    // component into the slope and keeps the rest.
    if (!s.grounded) {
      const meters_t ground = terrain_height (surface, plan (feet));
      if (height_of (feet) < ground) {
        position_value (feet)[1] = meters_value (ground);
        const direction_t normal = terrain_slope_normal (surface, plan (feet));
        const speed_t falling = std::max (still, -climb_of (v));
        v -= moppe::velocity (
          direction_value (normal) *
          std::min (0.0f, dot (velocity_value (v), direction_value (normal))));
        s.ground_normal = normal;
        if (!jumped && Character::walkable (normal)) {
          s.landing_speed = falling;
          s.since_landing = 0.0f * u::s;
          s.grounded = true;
          velocity_value (v)[1] = 0.0f;
        }
      }
    }
    if (s.grounded) {
      s.coyote = 0.0f * u::s;
      s.airborne_time = 0.0f * u::s;
    }
  }
}
