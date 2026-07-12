#include <moppe/mov/vehicle.hh>

#include <cmath>

namespace moppe {
  namespace mov {
    static const float radius = 1; // metres

    // How fast full steering input swings the bike itself, in radians
    // per second per radian of yaw input.  Grip then drags the
    // velocity around after the heading.
    static const float steering_rate = 1.6;
    static const float air_steering_rate = 0.9;

    static const float boost_acceleration = 26.0f; // m/s^2
    static const radians_t boost_max_tilt = 60.0f * u::deg;
    static const seconds_t boost_full_burn_time = seconds (3.0f);
    static const seconds_t boost_recharge_time = seconds (5.0f);
    static const seconds_t boost_recharge_pause = seconds (0.65f);
    static const float boost_reserve_charge = 0.06f;
    static const float boost_emergency_level = 0.18f;

    Vehicle::Vehicle (position_t position,
                      degrees_t orientation,
                      const HeightMap& map,
                      newtons_t max_thrust,
                      watts_t power,
                      kilograms_t mass)
        : m_position (position), m_velocity (moppe::velocity (Vec3 ())),
          m_heading (sin (orientation), 0, cos (orientation)),
          m_thrust_orientation (m_heading), m_yaw (), m_yaw_target (),
          m_lean (0), m_render_heading (m_heading), m_render_normal (0, 1, 0),
          m_susp (0), m_susp_v (0), m_wheel_spin (0), m_boost_flight (false),
          m_map (map), m_max_thrust (max_thrust), m_power (power), m_thrust (0),
          m_mass (mass), m_boost_input (0), m_boost_drive (0),
          m_boost_level (0), m_boost_charge (1),
          m_boost_recharge_delay (seconds (0)), m_water_level (-1000),
          m_airborne_time (seconds (0)), m_impact (0), m_fall_top (0),
          m_fall_drop (0), m_obstacles (0), m_body_kind (0),
          m_body_color (0.8, 0.15, 0.1) {
      calculate_orientation ();
      fall_to_ground ();
    }

    Vehicle::State Vehicle::state () const {
      return { m_position,
               m_velocity,
               m_heading,
               m_thrust_orientation,
               m_yaw,
               m_yaw_target,
               m_lean,
               m_render_heading,
               m_render_normal,
               m_susp,
               m_susp_v,
               m_wheel_spin,
               m_boost_flight,
               m_thrust,
               m_boost_input,
               m_boost_drive,
               m_boost_level,
               m_boost_charge,
               m_boost_recharge_delay,
               m_water_level,
               m_airborne_time,
               m_impact,
               m_fall_top,
               m_fall_drop,
               m_body_kind,
               m_body_color };
    }

    void Vehicle::restore (const State& state) {
      m_position = state.position;
      m_velocity = state.velocity;
      m_heading = state.heading;
      m_thrust_orientation = state.thrust_orientation;
      m_yaw = state.yaw;
      m_yaw_target = state.yaw_target;
      m_lean = state.lean;
      m_render_heading = state.render_heading;
      m_render_normal = state.render_normal;
      m_susp = state.susp;
      m_susp_v = state.susp_v;
      m_wheel_spin = state.wheel_spin;
      m_boost_flight = state.boost_flight;
      m_thrust = state.thrust;
      m_boost_input = state.boost_input;
      m_boost_drive = state.boost_drive;
      m_boost_level = state.boost_level;
      m_boost_charge = state.boost_charge;
      m_boost_recharge_delay = state.boost_recharge_delay;
      m_water_level = state.water_level;
      m_airborne_time = state.airborne_time;
      m_impact = state.impact;
      m_fall_top = state.fall_top;
      m_fall_drop = state.fall_drop;
      m_body_kind = state.body_kind;
      m_body_color = state.body_color;
    }

    void Vehicle::calculate_orientation () {
      if (is_grounded ()) {
        const Vec3& p = position_value (m_position);
        Vec3 n = m_map.interpolated_normal (p[0], p[2]);

        // Keep the heading tangent to the ground; the heading itself
        // is steered explicitly, and grip drags the velocity along,
        // so a heading/velocity mismatch is a drift, not an error.
        Vec3 heading = m_heading - n * (dot (m_heading, n));

        if (length2 (heading) > 0.0001f) {
          normalize (heading);
          m_heading = heading;
        }

        m_thrust_orientation = m_heading;
      }
    }

    void Vehicle::fall_to_ground () {
      position_value (m_position)[1] = ground_height ();
    }

    void Vehicle::check_ground_collision () {
      Vec3& p = position_value (m_position);
      p[1] = max (ground_height () + radius, p[1]);
    }

    bool Vehicle::is_grounded () const {
      return std::abs (ground_height () - position_value (m_position)[1]) <
             (radius + 0.1f);
    }

    Vec3 Vehicle::drag () const {
      // Linear rolling drag plus quadratic air drag: terminal speed
      // lands near the speedometer's 300 km/h, and fall speeds stay
      // survivable
      const Vec3& v = velocity_value (m_velocity);
      return v * -(0.05f + 0.0035f * length (v));
    }

    // Grounded, or close enough that a micro-hop over a bump should
    // not cut the throttle -- keeps rough ground feeling planted
    // while real jumps still feel like jumps
    bool Vehicle::driving_contact () const {
      if (is_grounded ())
        return true;
      return m_airborne_time < seconds (0.12f) &&
             position_value (m_position)[1] - ground_height () < radius + 0.6f;
    }

    // The obstacle box whose roof is the effective ground under the
    // bike -- only counts once the bike is up at roof level, so a
    // building towering overhead is not "ground".
    const Box* Vehicle::roof_under () const {
      if (!m_obstacles)
        return 0;

      const Box* found = 0;
      const Vec3& p = position_value (m_position);
      float best = m_map.interpolated_height (p[0], p[2]);

      for (size_t i = 0; i < m_obstacles->size (); ++i) {
        const Box& b = (*m_obstacles)[i];
        if (p[0] >= b.x0 && p[0] <= b.x1 && p[2] >= b.z0 && p[2] <= b.z1 &&
            p[1] > b.top - 2 * radius && b.top > best) {
          best = b.top;
          found = &b;
        }
      }

      return found;
    }

    void Vehicle::collide_with_walls () {
      if (!m_obstacles)
        return;

      Vec3& p = position_value (m_position);
      Vec3& v = velocity_value (m_velocity);
      for (size_t i = 0; i < m_obstacles->size (); ++i) {
        const Box& b = (*m_obstacles)[i];

        if (p[1] - radius >= b.top - 0.05f)
          continue; // on or above the roof

        const float dx0 = p[0] - (b.x0 - radius);
        const float dx1 = (b.x1 + radius) - p[0];
        const float dz0 = p[2] - (b.z0 - radius);
        const float dz1 = (b.z1 + radius) - p[2];

        if (dx0 <= 0 || dx1 <= 0 || dz0 <= 0 || dz1 <= 0)
          continue; // clear of this block

        // Push out along the axis of least penetration and bounce;
        // a hard bonk registers as an impact for shake and dust
        const float px = std::min (dx0, dx1);
        const float pz = std::min (dz0, dz1);

        if (px < pz) {
          p[0] = (dx0 < dx1) ? b.x0 - radius : b.x1 + radius;
          m_impact = std::max (m_impact, 0.4f * std::abs (v[0]));
          v[0] *= -0.35f;
        } else {
          p[2] = (dz0 < dz1) ? b.z0 - radius : b.z1 + radius;
          m_impact = std::max (m_impact, 0.4f * std::abs (v[2]));
          v[2] *= -0.35f;
        }
      }
    }

    void Vehicle::steer (seconds_t dt) {
      const float dt_s = seconds_value (dt);
      if (abs (m_yaw) < 0.001f * u::rad)
        return;

      if (driving_contact ()) {
        // Full lock turns slower at speed: stable at 250 km/h,
        // nimble at walking pace
        const float vf =
          std::abs (dot (velocity_value (m_velocity), m_heading));
        const float rate = steering_rate / (1.0f + vf / 70.0f);
        m_heading = Quaternion::rotate (
          m_heading, ground_normal (), -m_yaw * rate * dt_s);
      } else
        // Mid-air attitude control: swing the bike around, keep the
        // momentum -- landing sideways starts a drift
        m_heading = Quaternion::rotate (
          m_heading, Vec3 (0, 1, 0), -m_yaw * air_steering_rate * dt_s);
    }

    // Tire grip pulls the velocity into line with where the bike
    // points.  Grip fades continuously with steering input and
    // speed, braking breaks traction outright, and an ongoing slide
    // keeps breathing instead of snapping straight.
    void Vehicle::apply_grip (seconds_t dt, const Vec3& n) {
      Vec3& velocity = velocity_value (m_velocity);
      Vec3 fwd = m_heading - n * dot (m_heading, n);
      if (length2 (fwd) < 0.000001f)
        return;
      normalize (fwd);

      // Split velocity into forward, surface-normal, and in-plane
      // lateral parts; only the lateral part is gripped, so a launch
      // (normal component) survives the coyote-contact window
      const float vf = dot (velocity, fwd);
      const Vec3 vn = n * dot (velocity, n);
      const Vec3 lat = velocity - fwd * vf - vn;

      const float steer_amt =
        std::min (1.0f, scalar_value (abs (m_yaw) / (0.8f * u::rad)));
      const float speed_amt =
        std::min (1.0f, std::max (0.0f, (std::abs (vf) - 15.0f) / 10.0f));

      // Knobby-tire baseline: the bike tracks where it points unless
      // you deliberately break traction with hard steering at speed
      // or a brake-slide (the old 3.0 base read as riding on a dream)
      damping_t grip = (4.5f - 3.0f * steer_amt * speed_amt) / u::s;

      if (m_thrust < -0.1f && vf > 3.0f)
        grip = std::min (grip, 0.8f / u::s); // brake-slide
      if (length2 (lat) > 16.0f)
        grip = std::min (grip, 2.0f / u::s); // mid-drift hysteresis

      velocity = fwd * vf + vn + lat * decay (grip, dt);
    }

    void Vehicle::update (seconds_t dt) {
      const float dt_s = seconds_value (dt);
      // Steering input ramps in rather than snapping: smooth onset
      // for the heading, the grip model, and the fork visual at once
      m_yaw += (m_yaw_target - m_yaw) * smoothing_alpha (9.0f / u::s, dt);

      steer (dt);
      calculate_orientation ();

      const bool contact = driving_contact ();

      // The trigger meters a finite reserve.  Recharging pauses after a
      // burn and is deliberately slower in the air, so feathering the
      // jets cannot produce permanent flight.
      if (m_boost_input > 0.001f) {
        if (m_boost_charge > boost_reserve_charge) {
          const float available = (m_boost_charge - boost_reserve_charge) *
                                  seconds_value (boost_full_burn_time) / dt_s;
          m_boost_level = std::min (m_boost_input, available);
          m_boost_charge =
            std::max (boost_reserve_charge,
                      m_boost_charge - m_boost_level * dt_s /
                                         seconds_value (boost_full_burn_time));
          m_boost_flight = true;
        } else
          // The reserve cannot sustain flight, but it always supplies enough
          // thrust to take the cruelty out of a long fall.
          m_boost_level = boost_emergency_level * m_boost_input;
        // The trigger must be released before recharge starts; this avoids
        // alternating reserve thrust and tiny rechargeable impulses.
        m_boost_recharge_delay = boost_recharge_pause;
      } else {
        m_boost_level = 0;
        if (m_boost_recharge_delay > seconds (0))
          m_boost_recharge_delay -= dt;
        else {
          const float recharge_scale = is_grounded () ? 1.0f : 0.35f;
          m_boost_charge =
            std::min (1.0f,
                      m_boost_charge + recharge_scale * dt_s /
                                         seconds_value (boost_recharge_time));
        }
      }

      const radians_t tilt = boost_max_tilt * std::abs (m_boost_drive);
      const float drive_sign = m_boost_drive < 0 ? -1.0f : 1.0f;
      const Vec3 boost_direction =
        Vec3 (0, cos (tilt), 0) + m_heading * (drive_sign * sin (tilt));

      Vec3 f;
      const float g = -9.82f; // m/s^2 (was mislabeled as metres before)
      const Vec3 n = ground_normal ();

      // Thrust stays on through micro-hops (coyote contact); the
      // normal force only applies with real ground under the wheels.
      // Force is engine-power-limited above a few m/s: hard launch,
      // tapering pull, a real top speed against drag.
      if (contact) {
        const speed_t vf =
          std::abs (dot (velocity_value (m_velocity), m_thrust_orientation)) *
          (u::m / u::s);
        const newtons_t force =
          std::min (m_max_thrust,
                    newtons_t (m_power / std::max (vf, 0.5f * (u::m / u::s))));
        f += m_thrust_orientation * newtons_value (m_thrust * force);
      }
      Vec3 a (f / m_mass.numerical_value_in (u::kg) + drag () + Vec3 (0, g, 0) +
              boost_direction * (boost_acceleration * m_boost_level));

      // The ground supplies only as much normal force as necessary.  A
      // partial vertical burn therefore lightens the vehicle; it leaves
      // the surface only once the jets overcome gravity.
      if (is_grounded ()) {
        const float into_ground = dot (a, n);
        if (into_ground < 0)
          a -= n * into_ground;
      }

      Vec3& velocity = velocity_value (m_velocity);
      velocity += a * dt_s;

      if (is_grounded ()) {
        const float normal_speed = dot (velocity, n);
        if (dot (a, n) <= 0.001f || normal_speed < 0)
          velocity -= n * normal_speed;
      }
      if (contact) {
        // Jets progressively unload the tires instead of turning grip
        // off at the slightest touch.
        const seconds_t grip_dt = dt * (1.0f - 0.8f * m_boost_level);
        apply_grip (grip_dt, n);
      }

      // Wading through the ocean is slow going
      if (position_value (m_position)[1] - radius < m_water_level)
        m_velocity *= decay (1.4f / u::s, dt);

      m_position += quantity_cast<isq::position_vector> (m_velocity * dt);

      bound ();
      check_ground_collision ();
      collide_with_walls ();

      // Landing detection, for camera shake and dust bursts.  What
      // matters is the speed INTO the surface at touchdown -- landing
      // parallel to a downhill slope is gentle no matter how fast the
      // descent was.
      if (is_grounded ()) {
        if (m_airborne_time > seconds (0.25f)) {
          m_impact = std::max (0.0f, -dot (velocity, ground_normal ()));
          // Boost-assisted landings are partly forgiven: the jets
          // flare on touchdown, or so the story goes.
          if (m_boost_flight)
            m_impact *= 0.75f;
          m_susp_v -= 0.10f * m_impact;
          m_fall_drop = m_fall_top - position_value (m_position)[1];
        }
        if (m_boost_level <= 0)
          m_boost_flight = false;
        m_airborne_time = seconds (0);
        m_fall_top = position_value (m_position)[1];
      } else {
        m_airborne_time += dt;
        m_fall_top = std::max (m_fall_top, position_value (m_position)[1]);
      }

      // Lean into corners: balance the turn against gravity
      {
        float target = 0;
        if (driving_contact ()) {
          const float vf = dot (velocity, m_heading);
          const float rate = steering_rate / (1.0f + std::abs (vf) / 70.0f);
          target = std::atan2 (vf * radians_value (-m_yaw) * rate, 9.82f);
          target = std::max (-0.7f, std::min (0.7f, target));
        }
        m_lean += (target - m_lean) * smoothing_alpha (8.0f / u::s, dt);
      }

      // Visual attitude is separate from the steering heading. In flight the
      // bike follows its trajectory, while steering can still yaw the bike
      // without redirecting its momentum. Near a vertical arc, retain the
      // previous right axis so the bike cannot arbitrarily roll over.
      Vec3 pose_forward = m_heading;
      Vec3 pose_up = ground_normal ();
      damping_t pose_rate = 10.0f / u::s;
      if (airborne () && length2 (velocity) > 4.0f) {
        pose_forward = normalized (velocity);
        Vec3 right = cross (Vec3 (0, 1, 0), pose_forward);
        if (length2 (right) < 0.0001f)
          right = cross (m_render_normal, m_render_heading);
        if (length2 (right) < 0.0001f)
          right = Vec3 (1, 0, 0);
        normalize (right);
        pose_up = cross (pose_forward, right);
        pose_rate = 4.5f / u::s;
      }

      const float pose_alpha = smoothing_alpha (pose_rate, dt);
      m_render_heading =
        linear_vector_interpolate (m_render_heading, pose_forward, pose_alpha);
      m_render_normal =
        linear_vector_interpolate (m_render_normal, pose_up, pose_alpha);
      if (length2 (m_render_heading) > 0.000001f)
        normalize (m_render_heading);
      if (length2 (m_render_normal) > 0.000001f)
        normalize (m_render_normal);

      // Visual suspension spring: kicked by landings, settles fast
      m_susp_v += (-70.0f * m_susp - 9.0f * m_susp_v) * dt_s;
      m_susp += m_susp_v * dt_s;
      m_susp = std::max (-0.35f, std::min (0.15f, m_susp));

      // Wheel roll for the renderer: ground speed while rolling, a
      // throttle-driven spin-up in the air.  ~0.68 m wheel radius as
      // drawn.  Kept in [0, 2pi) so precision survives long rides.
      {
        float rate = dot (velocity, m_heading) / 0.68f;
        if (!contact && abs (m_thrust) > 0.1f)
          rate = scalar_value (40.0f * m_thrust);
        m_wheel_spin =
          std::fmod (m_wheel_spin + rate * dt_s, 2.0f * 3.14159265f);
      }
    }

    void Vehicle::set_boost (float boost, float drive) {
      m_boost_input = std::max (0.0f, std::min (1.0f, boost));
      m_boost_drive = std::max (-1.0f, std::min (1.0f, drive));
    }

    void Vehicle::bound () {
      if (m_map.periodic ())
        return;

      // Invisible walls at the map edge, bouncing like the building
      // walls do.  (The old build flung you across the sky toward
      // the map center at 400 km/h -- it read as a glitchy teleport,
      // especially near the corner spawn.)
      const float margin = 2; // metres
      const float max_x = (m_map.width () - 2) * m_map.scale ()[0] - margin;
      const float max_z = (m_map.height () - 2) * m_map.scale ()[2] - margin;

      Vec3& position = position_value (m_position);
      Vec3& velocity = velocity_value (m_velocity);
      float bounced = 0;
      if (position[0] < margin) {
        position[0] = margin;
        if (velocity[0] < 0) {
          bounced = -velocity[0];
          velocity[0] *= -0.35f;
        }
      } else if (position[0] > max_x) {
        position[0] = max_x;
        if (velocity[0] > 0) {
          bounced = velocity[0];
          velocity[0] *= -0.35f;
        }
      }
      if (position[2] < margin) {
        position[2] = margin;
        if (velocity[2] < 0) {
          bounced = max (bounced, -velocity[2]);
          velocity[2] *= -0.35f;
        }
      } else if (position[2] > max_z) {
        position[2] = max_z;
        if (velocity[2] > 0) {
          bounced = max (bounced, velocity[2]);
          velocity[2] *= -0.35f;
        }
      }

      if (bounced > 0)
        m_impact = max (m_impact, 0.4f * bounced);
    }

  }
}
