#include <moppe/game/chase_camera.hh>

#include <algorithm>
#include <cmath>

namespace moppe {
  namespace game {
    namespace {
      // damping_ratio is the dimensionless zeta of the second-order
      // system, not a rate: 1 is critical, below rings, above crawls.
      void spring (Vec3& value,
                   Vec3& velocity,
                   const Vec3& target,
                   frequency_t frequency,
                   float damping_ratio,
                   seconds_t dt) {
        const float omega = PI2 * frequency.numerical_value_in (u::Hz);
        const float dt_s = seconds_value (dt);
        const Vec3 acceleration = (target - value) * (omega * omega) -
                                  velocity * (2.0f * damping_ratio * omega);
        velocity += acceleration * dt_s;
        value += velocity * dt_s;
      }
    }

    void ChaseCamera::update (const Vec3& position,
                              const Vec3& orientation,
                              const Vec3& velocity,
                              seconds_t dt) {
      // dt-correct smoothing (3.1/s reproduces the old 0.05 @ 60Hz)
      float alpha = smoothing_alpha (3.1f / u::s, dt);
      float fast = smoothing_alpha (12.0f / u::s, dt);

      const bool reset = m_is_uninitialized;
      if (reset) {
        alpha = fast = 1;
        m_ahead = Vec3 ();
      }

      m_avg_orientation =
        linear_vector_interpolate (m_avg_orientation, orientation, alpha);
      normalize (m_avg_orientation);

      // Sit behind the heading, tilted up by the pitch offset around
      // a well-defined horizontal right axis.
      Vec3 o = m_avg_orientation;
      Vec3 right = cross (Vec3 (0, 1, 0), o);
      if (length2 (right) < 1e-6f)
        right = Vec3 (1, 0, 0);
      normalize (right);

      Vec3 d = Quaternion::rotate (-o, right, m_pitch_offset);
      normalize (d);

      // Look slightly ahead of the motion so corners open up; the
      // look-ahead is low-passed because velocity is discontinuous.
      Vec3 ahead = velocity * 0.15f;
      const float ahead_len = length (ahead);
      if (ahead_len > 8.0f)
        ahead *= 8.0f / ahead_len;
      m_ahead = linear_vector_interpolate (m_ahead, ahead, fast);

      Vec3 want_target = position + m_ahead;
      const float dist = meters_value (m_distance);
      const Vec3 scaled_offset (d[0] * dist / m_horizontal_scale,
                                d[1] * dist / m_vertical_scale,
                                d[2] * dist / m_horizontal_scale);
      const Vec3 want_position = position + scaled_offset;
      if (reset) {
        m_target = want_target;
        m_position = want_position;
        m_target_velocity = Vec3 ();
        m_position_velocity = Vec3 ();
        m_is_uninitialized = false;
      } else {
        // The target remains responsive while the camera body has a
        // lightly underdamped follow spring: enough inertia to feel
        // physical through turns and jumps without becoming seasick.
        spring (
          m_target, m_target_velocity, want_target, 2.4f * u::Hz, 0.92f, dt);
        spring (m_position,
                m_position_velocity,
                want_position,
                0.95f * u::Hz,
                0.74f,
                dt);
      }

      // Clamp the HORIZONTAL trail only, against a smoothed speed so
      // collisions can't shrink the window in one frame.
      m_speed +=
        (length (velocity) - m_speed) * smoothing_alpha (6.0f / u::s, dt);

      Vec3 offset = m_position - position;
      const float horiz =
        std::sqrt (offset[0] * offset[0] + offset[2] * offset[2]);
      const float max_len =
        meters_value (m_distance) / m_horizontal_scale + 0.06f * m_speed;
      if (horiz > max_len) {
        const float s = max_len / horiz;
        m_position[0] = position[0] + offset[0] * s;
        m_position[2] = position[2] + offset[2] * s;
        Vec3 outward (offset[0] / horiz, 0, offset[2] / horiz);
        const float outward_speed = dot (m_position_velocity, outward);
        if (outward_speed > 0)
          m_position_velocity -= outward * outward_speed;
      }
    }

    void ChaseCamera::limit (const map::HeightMap& map) {
      // Keep the look point out of the slope as velocity look-ahead
      // carries it across rolling terrain.
      const float target_floor =
        0.35f / m_vertical_scale +
        map.interpolated_height (m_target[0], m_target[2]);
      if (m_target[1] < target_floor) {
        m_target[1] = target_floor;
        if (m_target_velocity[1] < 0)
          m_target_velocity[1] = 0;
      }

      float needed = 2.2f / m_vertical_scale +
                     map.interpolated_height (m_position[0], m_position[2]);

      // The sight line toward the bike must clear the terrain too;
      // twelve taps catch narrow ridges that the old four-tap check
      // could step straight across. Clearance tapers toward the bike.
      for (int i = 1; i <= 10; ++i) {
        const float t = i / 12.0f;
        const float sx = m_position[0] + (m_target[0] - m_position[0]) * t;
        const float sz = m_position[2] + (m_target[2] - m_position[2]) * t;
        const float clearance = (0.3f + 1.9f * (1 - t)) / m_vertical_scale;
        const float g = clearance + map.interpolated_height (sx, sz);

        needed = max (needed, (g - m_target[1] * t) / (1 - t));
      }

      // Collision correction is immediate; descent remains spring-smoothed
      // by update(), so safety does not add a second competing camera motion.
      if (m_position[1] < needed) {
        m_position[1] = needed;
        if (m_position_velocity[1] < 0)
          m_position_velocity[1] = 0;
      }
    }
  }
}
