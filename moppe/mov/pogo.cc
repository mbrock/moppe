#include <moppe/mov/pogo.hh>

#include <moppe/mov/box3d_vec.hh>

#include <algorithm>
#include <cfloat>
#include <cmath>

#include <box3d/box3d.h>

namespace moppe::mov {
  namespace {
    namespace pogo_parts {
      // Bounce launch speeds: gentle idle hop, brisk travel bounce, and the
      // peak super-boing when pumped or rebounding off a big drop.
      constexpr speed_t idle_hop_speed = 4.6f * u::m / u::s;
      constexpr speed_t travel_hop_speed = 9.4f * u::m / u::s;
      constexpr speed_t pumped_hop_speed = 17.5f * u::m / u::s;
      constexpr speed_t max_rebound_speed = 22.5f * u::m / u::s;

      constexpr metres_per_second_squared_t air_accel =
        18.0f * u::m / pow<2> (u::s);
      constexpr metres_per_second_squared_t air_drag_accel =
        5.0f * u::m / pow<2> (u::s);

      const displacement_t capsule_low =
        displacement (Vec3 (0.0f, 0.55f, 0.0f));
      const displacement_t capsule_high = displacement (
        Vec3 (0.0f, meters_value (Pogo::height - Pogo::radius), 0.0f));

      meters_t terrain_height (const map::SurfaceGeometry& surface,
                               const plan_position_t& at) {
        return ground_height (
                 surface, plan_value (at).x (), plan_value (at).z ()) *
               u::m;
      }

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

      velocity_t horizontal (const velocity_t& v) {
        const Vec3 raw = velocity_value (v);
        return moppe::velocity (Vec3 (raw[0], 0.0f, raw[2]));
      }

      velocity_t
      approach (const velocity_t& v, const velocity_t& target, speed_t amount) {
        const velocity_t gap = target - v;
        const speed_t distance = gap.magnitude ();
        if (distance <= amount || distance <= 0.0f * (u::m / u::s))
          return target;
        return v + gap * (amount / distance);
      }
    }
  }

  void Pogo::spawn (position_t feet,
                    const direction_t& heading,
                    velocity_t inherited) {
    using namespace pogo_parts;
    m_state = State {};
    m_state.position = feet;
    m_state.velocity = inherited;
    const Vec3& way = direction_value (heading);
    Vec3 level (way[0], 0.0f, way[2]);
    if (length2 (level) < 0.01f)
      level = Vec3 (0.0f, 0.0f, 1.0f);
    m_state.heading = unit_direction (normalized (level));
    // Start with an eager little pop-up if starting from rest on the ground.
    if (velocity_value (inherited)[1] < 2.0f) {
      m_state.compressing = true;
      m_state.compress_time = 0.0f * u::s;
      m_state.compress_duration = 0.11f * u::s;
      m_state.stored_launch = idle_hop_speed;
      m_state.stored_horizontal = horizontal (inherited);
    }
    m_turn = {};
    m_drive = {};
    m_strafe = {};
    m_pump = false;
  }

  void Pogo::turn_by (radians_t angle) {
    Vec3 h = Quaternion::rotate (
      direction_value (m_state.heading), Vec3 (0.0f, 1.0f, 0.0f), -angle);
    h[1] = 0.0f;
    if (length2 (h) > 1e-6f)
      m_state.heading = unit_direction (normalized (h));
  }

  Pogo::Support Pogo::probe (const position_t& tip,
                             meters_t reach_down,
                             const map::SurfaceGeometry& surface,
                             const TrunkField* obstacles) const {
    using namespace pogo_parts;
    Support support;
    const meters_t ground = terrain_height (surface, plan (tip));
    if (ground <= height_of (tip) + 0.55f * u::m &&
        ground >= height_of (tip) - reach_down) {
      support.found = true;
      support.height = ground;
      support.normal = terrain_slope_normal (surface, plan (tip));
    }
    if (!obstacles)
      return support;
    constexpr meters_t foot = 0.18f * u::m;
    const position_t from =
      tip + quantity_cast<isq::position_vector> (
              displacement (Vec3 (0.0f, 0.55f + meters_value (foot), 0.0f)));
    const GroundHit hit =
      obstacles->cast_down (from, foot, 0.55f * u::m + reach_down);
    if (hit.hit && direction_value (hit.normal)[1] > 0.35f &&
        (!support.found || height_of (hit.point) > support.height)) {
      support.found = true;
      support.height = height_of (hit.point);
      support.normal = hit.normal;
    }
    return support;
  }

  void Pogo::slide (position_t& tip,
                    const displacement_t& delta,
                    const map::SurfaceGeometry& surface,
                    const TrunkField* obstacles) {
    using namespace pogo_parts;
    const position_t target = tip + quantity_cast<isq::position_vector> (delta);
    const meters_t reach = delta.magnitude () + 0.05f * u::m;
    std::vector<b3CollisionPlane> planes;
    for (int iteration = 0; iteration < 4; ++iteration) {
      m_planes.clear ();
      if (obstacles)
        obstacles->collide_mover (
          tip, capsule_low, capsule_high, radius, m_planes);

      const position_t low =
        tip + quantity_cast<isq::position_vector> (capsule_low);
      for (int k = 0; k < 5; ++k) {
        position_t at = low;
        if (k > 0) {
          const radians_t angle =
            static_cast<float> (k - 1) * (PI2 / 4.0f) * u::rad;
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
      for (const MoverPlane& plane : m_planes)
        planes.push_back ({ { to_b3 (direction_value (plane.normal)),
                              meters_value (plane.depth) },
                            FLT_MAX,
                            0.0f,
                            true });

      const b3PlaneSolverResult solved =
        b3SolvePlanes (to_b3 ((target - tip).numerical_value_in (si::metre)),
                       planes.data (),
                       static_cast<int> (planes.size ()));
      const displacement_t move = displacement (from_b3 (solved.delta));
      const proportion_t fraction =
        obstacles
          ? obstacles->cast_mover (tip, capsule_low, capsule_high, radius, move)
          : 1.0f * proportion[one];
      const displacement_t moved = move * fraction.numerical_value_in (one);
      tip += quantity_cast<isq::position_vector> (moved);
      if (length2 (displacement_value (moved)) < 1e-8f)
        break;
    }

    velocity_t& v = m_state.velocity;
    v = moppe::velocity (
      from_b3 (b3ClipVector (to_b3 (velocity_value (v)),
                             planes.data (),
                             static_cast<int> (planes.size ()))));
  }

  void Pogo::update (seconds_t dt,
                     const map::SurfaceGeometry& surface,
                     const TrunkField* obstacles) {
    using namespace pogo_parts;
    State& s = m_state;
    s.just_bounced = false;
    s.just_landed = false;
    s.last_launch = 0.0f * (u::m / u::s);
    s.impact_speed = 0.0f * (u::m / u::s);

    if (abs (m_turn) > 0.01f * control_signal[one]) {
      const radians_per_second_t rate =
        -m_turn.numerical_value_in (one) * 2.6f * u::rad / u::s;
      s.heading = unit_direction (Quaternion::rotate (
        direction_value (s.heading), Vec3 (0.0f, 1.0f, 0.0f), rate * dt));
    }

    const Vec3& ahead = direction_value (s.heading);
    const Vec3 right (ahead[2], 0.0f, -ahead[0]);
    const Vec3 stick = ahead * m_drive.numerical_value_in (one) -
                       right * m_strafe.numerical_value_in (one);
    const proportion_t input_mag =
      std::min (1.0f, length (stick)) * proportion[one];
    const float charge_val = s.charge.numerical_value_in (one);
    const speed_t top_speed =
      cruise_speed + (super_speed - cruise_speed) * charge_val;
    velocity_t wish = moppe::velocity (Vec3 ());
    if (input_mag > 0.01f * proportion[one])
      wish = moppe::velocity (
        normalized (stick) *
        (input_mag.numerical_value_in (one) * speed_value (top_speed)));

    if (s.compressing) {
      // Planted on the ground: squish the coil spring smoothly down to full
      // compression, then release with a crisp BOING!
      s.compress_time += dt;
      const float phase = std::clamp (
        (s.compress_time / std::max (s.compress_duration, 0.04f * u::s))
          .numerical_value_in (one),
        0.0f,
        1.0f);
      // Asymmetric spring curve: compresses quickly, lingers a tiny beat at
      // bottom dead centre, then snaps upward.
      const float depth =
        std::clamp (0.35f + 0.65f * (speed_value (s.stored_launch) /
                                     speed_value (pumped_hop_speed)),
                    0.35f,
                    1.0f);
      s.compression = std::sin (phase * PI) * depth * proportion[one];

      // Slight horizontal carry while the tip pivots on the ground.
      const velocity_t pivot_drift =
        approach (horizontal (s.velocity),
                  wish * 0.25f,
                  22.0f * u::m / pow<2> (u::s) * dt);
      s.velocity = pivot_drift;
      slide (s.position, s.velocity * dt, surface, obstacles);
      const Support sup = probe (s.position, 0.35f * u::m, surface, obstacles);
      if (sup.found) {
        position_value (s.position)[1] = meters_value (sup.height);
        s.ground_normal = sup.normal;
      }

      // Lean the stick toward the intended takeoff direction while loading.
      const radians_t target_pitch =
        0.38f * m_drive.numerical_value_in (one) * u::rad;
      const radians_t target_roll =
        -0.30f * m_strafe.numerical_value_in (one) * u::rad;
      const float ease =
        smoothing_alpha (16.0f / u::s, dt).numerical_value_in (one);
      s.pitch += (target_pitch - s.pitch) * ease;
      s.roll += (target_roll - s.roll) * ease;

      if (phase >= 1.0f) {
        // BOING! Launch upward + forward along input & slope.
        if (m_pump)
          s.charge = std::min (1.0f, charge_val + 0.36f) * proportion[one];
        else
          s.charge = std::max (0.0f, charge_val - 0.32f) * proportion[one];

        const float next_charge = s.charge.numerical_value_in (one);
        const speed_t base_launch =
          m_pump ? (travel_hop_speed + (pumped_hop_speed - travel_hop_speed) *
                                         (0.35f + 0.65f * next_charge))
          : input_mag > 0.08f * proportion[one]
            ? (idle_hop_speed + (travel_hop_speed - idle_hop_speed) *
                                  input_mag.numerical_value_in (one))
            : idle_hop_speed;
        const speed_t launch_up =
          std::min (max_rebound_speed, std::max (s.stored_launch, base_launch));

        // Kick off horizontally toward wish velocity, blended with carried
        // momentum so chains of bounces accelerate smoothly past running.
        const velocity_t carried = s.stored_horizontal;
        velocity_t launch_horiz =
          input_mag > 0.05f * proportion[one]
            ? approach (carried, wish, 9.5f * u::m / u::s)
            : carried * 0.45f;
        // On a steep slope, reflect a bit of the bounce along the slope normal
        // so you can boing off hillsides comically.
        const Vec3& n = direction_value (s.ground_normal);
        const Vec3 slope_kick (n[0], 0.0f, n[2]);
        launch_horiz +=
          moppe::velocity (slope_kick * (0.22f * speed_value (launch_up)));

        s.velocity = moppe::velocity (Vec3 (velocity_value (launch_horiz)[0],
                                            speed_value (launch_up),
                                            velocity_value (launch_horiz)[2]));
        s.compressing = false;
        s.compression = 0.0f * proportion[one];
        s.airborne_time = 0.0f * u::s;
        s.just_bounced = true;
        s.last_launch = launch_up;
      }
      return;
    }

    // Airborne: arc under gravity with generous mid-air steering so you can
    // aim your landings over boulders, rivers, and trails.
    s.airborne_time += dt;
    s.compression =
      quantity_cast<proportion> (s.compression * decay (18.0f / u::s, dt));

    const velocity_t flat = horizontal (s.velocity);
    velocity_t next_flat = flat;
    if (input_mag > 0.02f * proportion[one]) {
      const velocity_t steered = approach (flat, wish, air_accel * dt);
      const speed_t keep = std::max (flat.magnitude (), wish.magnitude ());
      next_flat = steered.magnitude () > keep &&
                      steered.magnitude () > 0.0f * (u::m / u::s)
                    ? steered * (keep / steered.magnitude ())
                    : steered;
    } else {
      next_flat =
        approach (flat, moppe::velocity (Vec3 ()), air_drag_accel * dt);
    }

    const float vy =
      velocity_value (s.velocity)[1] - speed_value (gravity * dt);
    s.velocity = moppe::velocity (
      Vec3 (velocity_value (next_flat)[0], vy, velocity_value (next_flat)[2]));

    slide (s.position, s.velocity * dt, surface, obstacles);

    // Tilt the stick playfully into the arc: leaning forward as it rises and
    // reaching out ahead as it drops toward the next touchdown.
    const float fwd_speed = dot (velocity_value (next_flat), ahead);
    const float side_speed = dot (velocity_value (next_flat), right);
    const radians_t target_pitch =
      std::clamp (0.022f * fwd_speed - 0.014f * vy, -0.35f, 0.48f) * u::rad;
    const radians_t target_roll =
      std::clamp (-0.025f * side_speed -
                    0.22f * m_strafe.numerical_value_in (one),
                  -0.42f,
                  0.42f) *
      u::rad;
    const float ease =
      smoothing_alpha (10.0f / u::s, dt).numerical_value_in (one);
    s.pitch += (target_pitch - s.pitch) * ease;
    s.roll += (target_roll - s.roll) * ease;

    // Check for touchdown once descending.
    const float climb = velocity_value (s.velocity)[1];
    const meters_t ground = terrain_height (surface, plan (s.position));
    const Support sup = probe (s.position, 0.06f * u::m, surface, obstacles);
    const bool hit_ground = height_of (s.position) <= ground;
    const bool hit_support =
      climb <= 0.0f && sup.found &&
      sup.height >= height_of (s.position) - 0.02f * u::m;

    if (hit_ground || hit_support) {
      const meters_t land_y =
        hit_support ? std::max (ground, sup.height) : ground;
      const direction_t normal =
        hit_support ? sup.normal
                    : terrain_slope_normal (surface, plan (s.position));
      position_value (s.position)[1] = meters_value (land_y);
      s.ground_normal = normal;
      const speed_t fall_speed = std::max (0.0f, -climb) * (u::m / u::s);

      // Rebound elasticity: falling onto the giant spring converts almost all
      // impact speed into an even bigger comical rebound (capped safely).
      const speed_t rebound =
        std::min (max_rebound_speed,
                  (0.92f * speed_value (fall_speed) + (m_pump ? 3.2f : 0.8f)) *
                    (u::m / u::s));
      s.compressing = true;
      s.compress_time = 0.0f * u::s;
      s.compress_duration =
        std::clamp (0.10f + 0.0045f * speed_value (fall_speed), 0.10f, 0.19f) *
        u::s;
      s.stored_launch = rebound;
      s.stored_horizontal = horizontal (s.velocity);
      s.just_landed = true;
      s.impact_speed = fall_speed;
      velocity_value (s.velocity)[1] = 0.0f;
    }
  }

  position_t Pogo::eye_position () const {
    // Footpegs sit 0.62 m above the rubber tip at rest, dipping by
    // compression * spring_travel; the hiker's eyes sit 1.60 m above the pegs
    // (slightly lower when crouching into the spring).
    const float c = m_state.compression.numerical_value_in (one);
    const meters_t lift =
      (0.62f + 1.60f - c * (meters_value (spring_travel) + 0.16f)) * u::m;
    return m_state.position +
           quantity_cast<isq::position_vector> (
             displacement (Vec3 (0.0f, meters_value (lift), 0.0f)));
  }
}
