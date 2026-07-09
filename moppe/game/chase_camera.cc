#include <moppe/game/chase_camera.hh>

#include <algorithm>
#include <cmath>

namespace moppe {
namespace game {
  void
  ChaseCamera::update (const Vector3D& position,
		       const Vector3D& orientation,
		       const Vector3D& velocity,
		       seconds_t dt) {
    m_dt = dt;

    // dt-correct smoothing (3.1/s reproduces the old 0.05 @ 60Hz)
    float alpha = 1.0f - std::exp (-3.1f * dt);
    float fast  = 1.0f - std::exp (-12.0f * dt);

    if (m_is_uninitialized) {
      alpha = fast = 1;
      m_target = position;
      m_ahead = Vector3D ();
      m_is_uninitialized = false;
    }

    m_avg_orientation = linear_vector_interpolate (m_avg_orientation,
						   orientation,
						   alpha);
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
    m_target.x = want_target.x;
    m_target.z = want_target.z;
    m_target.y += (want_target.y - m_target.y) * fast;

    m_position = linear_vector_interpolate (m_position,
					    position + d * m_distance,
					    alpha);

    // Clamp the HORIZONTAL trail only, against a smoothed speed so
    // collisions can't shrink the window in one frame.
    m_speed += (velocity.length () - m_speed)
      * (1.0f - std::exp (-6.0f * dt));

    Vector3D offset = m_position - position;
    const float horiz = std::sqrt (offset.x * offset.x
				   + offset.z * offset.z);
    const float max_len = m_distance + 0.06f * m_speed;
    if (horiz > max_len) {
      const float s = max_len / horiz;
      m_position.x = position.x + offset.x * s;
      m_position.z = position.z + offset.z * s;
    }
  }

  void
  ChaseCamera::limit (const map::HeightMap& map) {
    // Occlusion guard only: the resting height belongs to the
    // pitch/distance tuning in update().
    float needed = 2 * one_meter
      + map.interpolated_height (m_position.x, m_position.z);

    // The sight line toward the bike must clear the terrain too;
    // the demanded clearance tapers toward the target.
    for (int i = 1; i <= 4; ++i) {
      const float t = i / 5.0f;
      const float sx = m_position.x + (m_target.x - m_position.x) * t;
      const float sz = m_position.z + (m_target.z - m_position.z) * t;
      const float g = 2 * one_meter * (1 - t)
	+ map.interpolated_height (sx, sz);

      needed = max (needed, (g - m_target.y * t) / (1 - t));
    }

    // Rate-limited: climb quickly when the ground demands it, sink
    // back gently -- but never let the eye actually go underground.
    if (m_position.y < needed) {
      m_position.y += (needed - m_position.y)
	* std::min (1.0f, 14.0f * m_dt);
      m_position.y = max (m_position.y, needed - 1.5f * one_meter);
    } else
      m_position.y = max (needed,
			  m_position.y - 6.0f * m_dt * one_meter);
  }
}
}
