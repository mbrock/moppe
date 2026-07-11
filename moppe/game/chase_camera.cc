#include <moppe/game/chase_camera.hh>

#include <algorithm>
#include <cmath>

namespace moppe {
  namespace game {
    namespace {
      void spring (Vector3D& value,
                   Vector3D& velocity,
                   const Vector3D& target,
                   float frequency,
                   float damping,
                   float dt) {
        const float omega = 2.0f * PI * frequency;
        const Vector3D acceleration = (target - value) * (omega * omega) -
                                      velocity * (2.0f * damping * omega);
        velocity += acceleration * dt;
        value += velocity * dt;
      }
    }

    void ChaseCamera::update (const Vector3D& position,
                              const Vector3D& orientation,
                              const Vector3D& velocity,
                              seconds_t dt) {
      // dt-correct smoothing (3.1/s reproduces the old 0.05 @ 60Hz)
      float alpha = 1.0f - std::exp (-3.1f * dt);
      float fast = 1.0f - std::exp (-12.0f * dt);

      const bool reset = m_is_uninitialized;
      if (reset) {
        alpha = fast = 1;
        m_ahead = Vector3D ();
      }

      m_avg_orientation =
        linear_vector_interpolate (m_avg_orientation, orientation, alpha);
      m_avg_orientation.normalize ();

      // Sit behind the heading, tilted up by the pitch offset around
      // a well-defined horizontal right axis.
      Vector3D o = m_avg_orientation;
      Vector3D right = Vector3D (0, 1, 0).cross (o);
      if (right.length2 () < 1e-6f)
        right = Vector3D (1, 0, 0);
      right.normalize ();

      Vector3D d = Quaternion::rotate (-o, right, m_pitch_offset);
      d.normalize ();

      // Look slightly ahead of the motion so corners open up; the
      // look-ahead is low-passed because velocity is discontinuous.
      Vector3D ahead = velocity * 0.15f;
      const float ahead_len = ahead.length ();
      if (ahead_len > 8.0f)
        ahead *= 8.0f / ahead_len;
      m_ahead = linear_vector_interpolate (m_ahead, ahead, fast);

      Vector3D want_target = position + m_ahead;
      const Vector3D want_position = position + d * m_distance;
      if (reset) {
        m_target = want_target;
        m_position = want_position;
        m_target_velocity = Vector3D ();
        m_position_velocity = Vector3D ();
        m_is_uninitialized = false;
      } else {
        // The target remains responsive while the camera body has a
        // lightly underdamped follow spring: enough inertia to feel
        // physical through turns and jumps without becoming seasick.
        spring (m_target, m_target_velocity, want_target, 2.4f, 0.92f, dt);
        spring (
          m_position, m_position_velocity, want_position, 0.95f, 0.74f, dt);
      }

      // Clamp the HORIZONTAL trail only, against a smoothed speed so
      // collisions can't shrink the window in one frame.
      m_speed +=
        (velocity.length () - m_speed) * (1.0f - std::exp (-6.0f * dt));

      Vector3D offset = m_position - position;
      const float horiz = std::sqrt (offset.x * offset.x + offset.z * offset.z);
      const float max_len = m_distance + 0.06f * m_speed;
      if (horiz > max_len) {
        const float s = max_len / horiz;
        m_position.x = position.x + offset.x * s;
        m_position.z = position.z + offset.z * s;
        Vector3D outward (offset.x / horiz, 0, offset.z / horiz);
        const float outward_speed = m_position_velocity.dot (outward);
        if (outward_speed > 0)
          m_position_velocity -= outward * outward_speed;
      }
    }

    void ChaseCamera::limit (const map::HeightMap& map) {
      // Keep the look point out of the slope as velocity look-ahead
      // carries it across rolling terrain.
      const float target_floor =
        0.35f * one_meter + map.interpolated_height (m_target.x, m_target.z);
      if (m_target.y < target_floor) {
        m_target.y = target_floor;
        if (m_target_velocity.y < 0)
          m_target_velocity.y = 0;
      }

      float needed =
        2.2f * one_meter + map.interpolated_height (m_position.x, m_position.z);

      // The sight line toward the bike must clear the terrain too;
      // twelve taps catch narrow ridges that the old four-tap check
      // could step straight across. Clearance tapers toward the bike.
      for (int i = 1; i <= 10; ++i) {
        const float t = i / 12.0f;
        const float sx = m_position.x + (m_target.x - m_position.x) * t;
        const float sz = m_position.z + (m_target.z - m_position.z) * t;
        const float clearance = (0.3f + 1.9f * (1 - t)) * one_meter;
        const float g = clearance + map.interpolated_height (sx, sz);

        needed = max (needed, (g - m_target.y * t) / (1 - t));
      }

      // Collision correction is immediate; descent remains spring-smoothed
      // by update(), so safety does not add a second competing camera motion.
      if (m_position.y < needed) {
        m_position.y = needed;
        if (m_position_velocity.y < 0)
          m_position_velocity.y = 0;
      }
    }
  }
}
