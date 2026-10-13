#include <moppe/game/chase_camera.hh>
#include <moppe/gfx/signal.hh>

#include <algorithm>
#include <cmath>

namespace moppe {
  namespace game {
    namespace {
      // The camera's follow spring: lightly underdamped, enough inertia
      // to feel physical through turns and jumps without becoming seasick.
      constexpr float FOLLOW_HZ = 0.95f;
      constexpr float FOLLOW_DAMPING = 0.74f;
      // Seconds of speed along the boom the camera falls back by (or
      // runs out ahead by, looking back), so going fast is felt.
      constexpr float SPEED_REACH = 0.06f;

      // damping_ratio is the dimensionless zeta of the second-order
      // system, not a rate: 1 is critical, below rings, above crawls. It
      // damps against `reference`, the velocity the value should keep
      // when it has arrived: a spring that moves with the subject does
      // not trail behind it in proportion to its speed.
      void spring (position_t& value,
                   velocity_t& velocity,
                   position_t target,
                   velocity_t reference,
                   frequency_t frequency,
                   float damping_ratio,
                   seconds_t dt) {
        const frequency_t omega = PI2 * frequency;
        const displacement_t error =
          quantity_cast<isq::displacement> (target - value);
        const acceleration_t acceleration = quantity_cast<isq::acceleration> (
          error * omega * omega -
          (velocity - reference) * (2.0f * damping_ratio * omega));
        velocity += quantity_cast<isq::velocity> (acceleration * dt);
        value += quantity_cast<isq::position_vector> (velocity * dt);
      }

      float ground (const map::SurfaceGeometry& surface, const Vec3& at) {
        return terrain::surface_elevation_value (
          spatial::sample<terrain::surface_elevation> (
            surface, moppe::position (Vec3 (at[0], 0.0f, at[2]))));
      }

      // How high a camera at `camera` must stand to be `at_camera` metres
      // above the ground and to see `toward` over it, the clearance
      // tapering to `at_subject` there. Twelve taps catch narrow ridges
      // that four could step straight across.
      float lift_needed (const map::SurfaceGeometry& surface,
                         const Vec3& camera,
                         const Vec3& toward,
                         float at_camera,
                         float at_subject) {
        float needed = at_camera + ground (surface, camera);
        for (int i = 1; i <= 10; ++i) {
          const float t = i / 12.0f;
          const Vec3 tap = camera + (toward - camera) * t;
          const float clearance =
            at_subject + (at_camera - at_subject) * (1 - t);
          const float g = clearance + ground (surface, tap);
          needed = max (needed, (g - toward[1] * t) / (1 - t));
        }
        return needed;
      }

      // Keeps the look point out of the slope as look-ahead carries it
      // across rolling terrain.
      void floor_target (const map::SurfaceGeometry& surface,
                         Vec3& target,
                         Vec3& velocity) {
        const float floor = 0.35f + ground (surface, target);
        if (target[1] < floor) {
          target[1] = floor;
          if (velocity[1] < 0)
            velocity[1] = 0;
        }
      }
    }

    void ChaseCamera::turn (float yaw, float pitch) {
      // The camera swings at once and keeps its velocity: the springs
      // damp toward the subject's own motion, which the swing does not
      // change.
      const Vec3 up (0, 1, 0);
      if (yaw != 0) {
        // Turning right is a negative rotation about up, as the walker's.
        const radians_t angle = -yaw * u::rad;
        m_avg_orientation = Quaternion::rotate (m_avg_orientation, up, angle);
        if (m_is_uninitialized)
          return;
        Vec3& target = position_value (m_target);
        Vec3& camera = position_value (m_position);
        camera = target + Quaternion::rotate (camera - target, up, angle);
      }
      if (pitch != 0 && !m_is_uninitialized) {
        Vec3& target = position_value (m_target);
        Vec3& camera = position_value (m_position);
        const Vec3 offset = camera - target;
        // The same right axis update() raises the camera about.
        Vec3 right = cross (up, Vec3 (-offset[0], 0, -offset[2]));
        if (length2 (right) < 1e-6f)
          return;
        normalize (right);
        camera = target + Quaternion::rotate (offset, right, pitch * u::rad);
      }
    }

    void ChaseCamera::aim (float yaw, float pitch) {
      const float turn_yaw = yaw - m_orbit_yaw;
      const float turn_pitch = pitch - m_orbit_pitch;
      m_orbit_yaw = yaw;
      m_orbit_pitch = pitch;
      // turn() would also swing the carried heading, which aim's offset is
      // measured from; swing the camera alone.
      const Vec3 heading = m_avg_orientation;
      turn (turn_yaw, turn_pitch);
      m_avg_orientation = heading;
    }

    void ChaseCamera::update (position_t position,
                              const Vec3& orientation,
                              velocity_t velocity,
                              seconds_t dt) {
      // dt-correct smoothing (3.1/s reproduces the old 0.05 @ 60Hz)
      float alpha = smoothing_alpha (3.1f / u::s, dt);
      float fast = smoothing_alpha (12.0f / u::s, dt);

      const bool reset = m_is_uninitialized;
      if (reset) {
        alpha = fast = 1;
        m_ahead = displacement (Vec3 ());
      }

      m_avg_orientation =
        linear_vector_interpolate (m_avg_orientation, orientation, alpha);
      normalize (m_avg_orientation);
      m_pivot = position;

      // Sit behind the heading, tilted up from the horizon by the pitch
      // offset around a well-defined horizontal right axis. A subject
      // heading downhill lifts the camera by its slope, to see down over
      // it; one climbing does not lower it under itself.
      const Vec3 up (0, 1, 0);
      const Vec3 o =
        Quaternion::rotate (m_avg_orientation, up, -m_orbit_yaw * u::rad);
      Vec3 level_o (o[0], 0, o[2]);
      if (length2 (level_o) < 1e-6f)
        level_o = Vec3 (0, 0, 1);
      normalize (level_o);
      const Vec3 right = cross (up, level_o);
      const float descent =
        std::max (0.0f,
                  -std::atan2 (static_cast<float> (o[1]),
                               std::sqrt (o[0] * o[0] + o[2] * o[2])));

      const float elevation =
        m_pitch_offset.numerical_value_in (u::rad) + m_orbit_pitch;
      Vec3 d =
        Quaternion::rotate (-level_o, right, (elevation + descent) * u::rad);
      normalize (d);

      // Look a little ahead of the motion so corners open up; the
      // look-ahead is low-passed because velocity is discontinuous. The
      // target keeps up with the subject's level motion by itself (below),
      // so only rising and falling need leading much.
      const Vec3& moving = velocity_value (velocity);
      const Vec3 level (moving[0], 0, moving[2]);
      displacement_t ahead = displacement (
        Vec3 (0.03f * moving[0], 0.15f * moving[1], 0.03f * moving[2]));
      const meters_t ahead_len = magnitude (ahead);
      if (ahead_len > 8.0f * u::m)
        ahead *= 8.0f * u::m / ahead_len;
      m_ahead += (ahead - m_ahead) * fast;

      // Looking up from low down, the look point rises over the subject,
      // which sinks in the frame and leaves the sky above it.
      const float dist = m_distance.numerical_value_in (moppe::u::m);
      const float lift = 0.3f * dist * smoothstep (0.0f, 0.7f, -elevation);
      position_t want_target =
        position + quantity_cast<isq::position_vector> (m_ahead) +
        quantity_cast<isq::position_vector> (displacement (Vec3 (0, lift, 0)));
      const Vec3 scaled_offset = d * dist;
      const position_t want_position =
        position +
        quantity_cast<isq::position_vector> (displacement (scaled_offset));
      if (reset) {
        m_target = want_target;
        m_position = want_position;
        m_target_velocity = moppe::velocity (Vec3 ());
        m_position_velocity = moppe::velocity (Vec3 ());
        m_is_uninitialized = false;
        m_snap = true;
      } else {
        // Both springs are damped against the subject's level motion, so
        // the camera keeps its place about a moving subject -- beside it
        // when the player looks aside -- instead of trailing behind it.
        // Speed only stretches the boom: the camera stands out along it
        // by SPEED_REACH seconds of the speed it has along it.
        Vec3 reach = level;
        Vec3 outward (d[0], 0, d[2]);
        if (length2 (outward) > 1e-6f) {
          normalize (outward);
          const float rate = PI2 * FOLLOW_HZ / (2.0f * FOLLOW_DAMPING);
          reach +=
            outward * (SPEED_REACH * rate * std::abs (dot (level, outward)));
        }
        spring (m_target,
                m_target_velocity,
                want_target,
                moppe::velocity (level),
                2.4f * u::Hz,
                0.92f,
                dt);
        spring (m_position,
                m_position_velocity,
                want_position,
                moppe::velocity (reach),
                FOLLOW_HZ * u::Hz,
                FOLLOW_DAMPING,
                dt);
      }

      // Clamp the HORIZONTAL trail only, against a smoothed speed so
      // collisions can't shrink the window in one frame.
      m_speed +=
        (magnitude (velocity) - m_speed) * smoothing_alpha (6.0f / u::s, dt);

      Vec3& camera_position = position_value (m_position);
      const Vec3& subject_position = position_value (position);
      Vec3& camera_velocity = velocity_value (m_position_velocity);
      Vec3 offset = camera_position - subject_position;
      const float horiz =
        std::sqrt (offset[0] * offset[0] + offset[2] * offset[2]);
      const float max_len =
        dist + SPEED_REACH * m_speed.numerical_value_in (u::m / u::s);
      if (horiz > max_len) {
        const float s = max_len / horiz;
        camera_position[0] = subject_position[0] + offset[0] * s;
        camera_position[2] = subject_position[2] + offset[2] * s;
        // Moving away from the subject, not merely moving, is stopped.
        Vec3 outward (offset[0] / horiz, 0, offset[2] / horiz);
        const float outward_speed = dot (camera_velocity - level, outward);
        if (outward_speed > 0)
          camera_velocity -= outward * outward_speed;
      }
      m_view = m_position;
    }

    void ChaseCamera::limit (const map::SurfaceGeometry& surface,
                             seconds_t dt) {
      floor_target (
        surface, position_value (m_target), velocity_value (m_target_velocity));

      const Vec3 pivot = position_value (m_pivot);
      const Vec3 end = position_value (m_position);
      const Vec3 boom = end - pivot;
      const float span = length (boom);
      if (span < 1e-4f) {
        m_view = m_position;
        return;
      }

      // March out from the pivot to where the ground first comes within
      // a margin of the boom: a hand's breadth at the subject (or what it
      // has itself), half a metre at the camera.
      const float pivot_margin =
        std::min (0.25f, pivot[1] - ground (surface, pivot));
      float clear = span;
      float before = pivot[1] - ground (surface, pivot) - pivot_margin;
      constexpr int taps = 16;
      for (int i = 1; i <= taps; ++i) {
        const float t = static_cast<float> (i) / taps;
        const Vec3 tap = pivot + boom * t;
        const float margin = pivot_margin + (0.5f - pivot_margin) * t;
        const float above = tap[1] - ground (surface, tap) - margin;
        if (above < 0) {
          const float last = static_cast<float> (i - 1) / taps;
          const float share = before > 0 ? before / (before - above) : 0.0f;
          clear = span * (last + (t - last) * share);
          break;
        }
        before = above;
      }
      clear =
        std::max (clear, std::min (span, m_nearest.numerical_value_in (u::m)));

      // The ground takes the boom in at once; it lets out over a second
      // or so, so a hump passed over does not pump the view in and out.
      float boom_length = m_boom.numerical_value_in (u::m);
      if (m_snap || clear < boom_length)
        boom_length = clear;
      else
        boom_length +=
          (clear - boom_length) * smoothing_alpha (2.0f / u::s, dt);
      boom_length = std::min (boom_length, span);
      m_boom = boom_length * u::m;
      m_snap = false;

      // Drawn in as near as it may come and still in the ground, or with
      // a ridge in the way, the camera lifts clear.
      Vec3 view = pivot + boom * (boom_length / span);
      view[1] = std::max (static_cast<float> (view[1]),
                          lift_needed (surface, view, pivot, 0.5f, 0.25f));
      m_view = moppe::position (view);
    }

    void ChaseCamera::keep_above (const map::SurfaceGeometry& surface) {
      Vec3& target = position_value (m_target);
      floor_target (surface, target, velocity_value (m_target_velocity));
      Vec3& camera = position_value (m_view);
      camera[1] = std::max (static_cast<float> (camera[1]),
                            lift_needed (surface, camera, target, 2.2f, 0.3f));
      m_position = m_view;
    }
  }
}
