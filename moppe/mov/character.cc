#include <moppe/mov/character.hh>

#include <moppe/mov/box3d_vec.hh>

#include <algorithm>
#include <cfloat>
#include <cmath>

#include <box3d/box3d.h>

namespace moppe::mov {
  namespace {
    constexpr float ground_acceleration = 40.0f; // m/s^2 toward the wish
    constexpr float ground_braking = 28.0f;      // m/s^2 with no input
    constexpr float air_acceleration = 7.0f;
    constexpr float coyote_seconds = 0.12f;
    constexpr float jump_buffer_seconds = 0.14f;
    // A grounded body follows ground falling away by this much per step
    // instead of launching off every crest.
    constexpr float snap_reach = 0.35f;
    // Feet that rise or fall this much more than the ground's slope
    // predicts stepped onto or off something; the slope itself needs no
    // easing.
    constexpr float step_snap = 0.04f;
    constexpr float step_ease = 16.0f; // 1/s

    // The capsule's hemisphere centres above the feet.
    const Vec3
      capsule_low (0.0f, Character::step_height + Character::radius, 0.0f);
    const Vec3 capsule_high (0.0f, Character::height - Character::radius, 0.0f);

    float
    terrain_height (const map::SurfaceGeometry& surface, float x, float z) {
      return terrain::surface_elevation_value (
        spatial::sample<terrain::surface_elevation> (
          surface, moppe::position (Vec3 (x, 0.0f, z))));
    }

    // The heightfield's own slope, by central differences over a span a
    // little under a foot, so the plane agrees with the heights the feet
    // are set on rather than with the smoothed shading normals.
    Vec3 terrain_slope_normal (const map::SurfaceGeometry& surface,
                               float x,
                               float z) {
      constexpr float e = 0.25f;
      const float dx =
        terrain_height (surface, x + e, z) - terrain_height (surface, x - e, z);
      const float dz =
        terrain_height (surface, x, z + e) - terrain_height (surface, x, z - e);
      return normalized (Vec3 (-dx, 2.0f * e, -dz));
    }

    Vec3 horizontal (const Vec3& v) {
      return Vec3 (v[0], 0.0f, v[2]);
    }

    // Moves `v` toward `target` by at most `amount`.
    Vec3 approach (const Vec3& v, const Vec3& target, float amount) {
      const Vec3 gap = target - v;
      const float distance = length (gap);
      if (distance <= amount || distance <= 0.0f)
        return target;
      return v + gap * (amount / distance);
    }

  }

  void Character::spawn (position_t feet) {
    m_state = State {};
    m_state.position = feet;
    m_state.velocity = moppe::velocity (Vec3 ());
  }

  Character::Support Character::probe (const Vec3& feet,
                                       float reach_down,
                                       const map::SurfaceGeometry& surface,
                                       const TrunkField* obstacles) const {
    Support support;
    const float ground = terrain_height (surface, feet[0], feet[2]);
    if (ground <= feet[1] + step_height && ground >= feet[1] - reach_down) {
      support.found = true;
      support.height = ground;
      support.normal = terrain_slope_normal (surface, feet[0], feet[2]);
    }
    if (!obstacles)
      return support;

    // A rock's top is found by a narrow sphere dropped from step height:
    // narrower than the capsule, so a body is not held up by a flank it is
    // merely brushing.
    constexpr float foot = 0.18f;
    const Vec3 from = feet + Vec3 (0.0f, step_height + foot, 0.0f);
    const GroundHit hit =
      obstacles->cast_down (from, foot, step_height + reach_down);
    if (hit.hit && hit.normal[1] >= walkable_normal_y &&
        (!support.found || hit.point[1] > support.height)) {
      support.found = true;
      support.height = hit.point[1];
      support.normal = hit.normal;
    }
    return support;
  }

  void Character::slide (Vec3& feet,
                         const Vec3& delta,
                         bool grounded,
                         const map::SurfaceGeometry& surface,
                         const TrunkField* obstacles) {
    // Box3D's mover loop: gather the planes the capsule touches, solve the
    // displacement that honours them, sweep it so nothing tunnels, repeat
    // from where it stopped.
    const Vec3 target = feet + delta;
    const float reach = length (delta) + 0.05f;
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
      const Vec3 low = feet + capsule_low;
      for (int k = 0; k < 7; ++k) {
        Vec3 at = low;
        if (k > 0) {
          const float angle = static_cast<float> (k - 1) * (PI2 / 6.0f);
          at += Vec3 (std::cos (angle), 0.0f, std::sin (angle)) * radius;
        }
        const Vec3 normal = terrain_slope_normal (surface, at[0], at[2]);
        const Vec3 ground (
          at[0], terrain_height (surface, at[0], at[2]), at[2]);
        const float depth = radius - dot (normal, low - ground);
        if (depth > -reach)
          m_planes.push_back ({ normal, depth });
      }

      planes.clear ();
      for (const MoverPlane& plane : m_planes) {
        Vec3 normal = plane.normal;
        float depth = plane.depth;
        if (grounded) {
          // Standing, every contact is a wall: the probe, not the solver,
          // decides the height of the feet, so a push up a rock's flank
          // must not become a climb.
          const float flat =
            std::sqrt (normal[0] * normal[0] + normal[2] * normal[2]);
          if (flat < 0.05f)
            continue;
          normal = horizontal (normal) * (1.0f / flat);
          depth /= std::max (flat, 0.25f);
        }
        planes.push_back ({ { to_b3 (normal), depth }, FLT_MAX, 0.0f, true });
      }

      const b3PlaneSolverResult solved =
        b3SolvePlanes (to_b3 (target - feet),
                       planes.data (),
                       static_cast<int> (planes.size ()));
      Vec3 move = from_b3 (solved.delta);
      if (grounded)
        move[1] = 0.0f;
      const float fraction =
        obstacles ? obstacles->cast_mover (
                      feet, capsule_low, capsule_high, radius, move)
                  : 1.0f;
      feet += move * fraction;
      if (length2 (move * fraction) < 1e-8f)
        break;
    }

    Vec3& v = velocity_value (m_state.velocity);
    v = from_b3 (b3ClipVector (
      to_b3 (v), planes.data (), static_cast<int> (planes.size ())));
  }

  void Character::step (seconds_t dt,
                        const CharacterIntent& intent,
                        const map::SurfaceGeometry& surface,
                        const TrunkField* obstacles) {
    const float h = seconds_value (dt);
    State& s = m_state;
    Vec3& feet = position_value (s.position);
    Vec3& v = velocity_value (s.velocity);

    s.jump_buffer = intent.jump ? jump_buffer_seconds * u::s
                                : std::max (0.0f * u::s, s.jump_buffer - dt);
    if (!s.grounded) {
      s.coyote = std::max (0.0f * u::s, s.coyote - dt);
      s.airborne_time += dt;
    }
    s.since_landing += dt;
    s.step_offset *= decay (step_ease / u::s, dt);

    Vec3 wish = horizontal (intent.wish_velocity);
    if (s.grounded) {
      // Climbing costs pace: up a slope at the walkable limit a body makes
      // a little over half its level speed.
      const Vec3 downhill = horizontal (s.ground_normal);
      const float uphill = -dot (wish, downhill);
      if (uphill > 0.0f && length2 (wish) > 0.0f)
        wish *= 1.0f - 0.45f * std::min (1.0f, uphill / length (wish) / 0.77f);
      const bool steering = length2 (wish) > 1e-4f;
      v = approach (horizontal (v),
                    wish,
                    (steering ? ground_acceleration : ground_braking) * h);
    } else {
      // A little air control: enough to aim a jump, never to fly.
      if (length2 (wish) > 1e-4f) {
        const Vec3 flat = horizontal (v);
        const Vec3 steered = approach (flat, wish, air_acceleration * h);
        // Never brake a long jump just because the stick asks for less.
        const float keep = std::max (length (flat), length (wish));
        const Vec3 limited = length (steered) > keep && length (steered) > 0
                               ? steered * (keep / length (steered))
                               : steered;
        v = Vec3 (limited[0], v[1], limited[2]);
      }
      v[1] -= gravity * h;
    }

    bool jumped = false;
    if (s.jump_buffer > 0.0f * u::s && (s.grounded || s.coyote > 0.0f * u::s)) {
      v[1] = jump_speed;
      s.grounded = false;
      s.coyote = 0.0f * u::s;
      s.jump_buffer = 0.0f * u::s;
      s.airborne_time = 0.0f * u::s;
      jumped = true;
    }

    const bool was_grounded = s.grounded;
    const Vec3 before = feet;
    slide (feet, v * h, was_grounded, surface, obstacles);

    Support support =
      probe (feet, was_grounded ? snap_reach : 0.0f, surface, obstacles);
    if (was_grounded && support.found &&
        support.normal[1] < walkable_normal_y &&
        support.height > before[1] + 0.01f) {
      // Walking into a bank too steep to stand on: it acts as a wall, so
      // the body slides along its foot instead of hopping up and falling
      // back. The bank's downhill direction is the wall's normal.
      Vec3 wall = horizontal (support.normal);
      if (length2 (wall) > 1e-8f) {
        normalize (wall);
        Vec3 moved = horizontal (feet - before);
        moved -= wall * std::min (0.0f, dot (moved, wall));
        feet = Vec3 (before[0] + moved[0], feet[1], before[2] + moved[2]);
        v -= wall * std::min (0.0f, dot (v, wall));
        support = probe (feet, snap_reach, surface, obstacles);
        if (support.found && support.normal[1] < walkable_normal_y &&
            support.height > before[1] + 0.01f) {
          feet = before;
          v = Vec3 ();
          support = probe (feet, snap_reach, surface, obstacles);
        }
      }
    }

    const bool walkable =
      support.found && support.normal[1] >= walkable_normal_y;
    if (was_grounded) {
      if (walkable) {
        const Vec3& n = s.ground_normal;
        const Vec3 moved = feet - before;
        const float predicted = -(n[0] * moved[0] + n[2] * moved[2]) / n[1];
        const float rise = support.height - feet[1];
        if (std::abs (rise - predicted) > step_snap)
          s.step_offset -= (rise - predicted) * u::m;
        feet[1] = support.height;
        s.ground_normal = support.normal;
      } else {
        // Off an edge, or onto ground too steep to stand on.
        s.grounded = false;
        s.coyote = coyote_seconds * u::s;
        s.airborne_time = 0.0f * u::s;
        v[1] = std::min<float> (v[1], 0.0f);
      }
    } else if (!jumped && v[1] <= 0.0f && walkable &&
               support.height >= feet[1] - 1e-4f) {
      s.landing_speed = -v[1] * u::m / u::s;
      s.since_landing = 0.0f * u::s;
      s.grounded = true;
      s.ground_normal = support.normal;
      feet[1] = support.height;
      v[1] = 0.0f;
    }

    // Nothing passes below the heightfield. On a bank too steep to stand on
    // this is what turns a fall into a slide: the velocity loses its
    // component into the slope and keeps the rest.
    if (!s.grounded) {
      const float ground = terrain_height (surface, feet[0], feet[2]);
      if (feet[1] < ground) {
        feet[1] = ground;
        const Vec3 normal = terrain_slope_normal (surface, feet[0], feet[2]);
        const float falling = std::max (0.0f, -v[1]);
        v -= normal * std::min (0.0f, dot (v, normal));
        s.ground_normal = normal;
        if (!jumped && normal[1] >= walkable_normal_y) {
          s.landing_speed = falling * u::m / u::s;
          s.since_landing = 0.0f * u::s;
          s.grounded = true;
          v[1] = 0.0f;
        }
      }
    }
    if (s.grounded) {
      s.coyote = 0.0f * u::s;
      s.airborne_time = 0.0f * u::s;
    }
  }
}
