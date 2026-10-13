#ifndef MOPPE_GAME_CHASE_CAMERA_HH
#define MOPPE_GAME_CHASE_CAMERA_HH

#include <moppe/gfx/mat4.hh>
#include <moppe/gfx/math.hh>
#include <moppe/map/surface.hh>

namespace moppe {
  namespace game {
    // The chase camera from gfx::ThirdPersonCamera, GL-free: instead
    // of realize()-ing onto the GL matrix stack it hands out a view
    // matrix. Its follow spring and terrain corridor are frame-rate
    // independent.
    //
    // It is a boom swung about a pivot on the subject. The boom's far end
    // follows on a spring, and that end may well be in the ground when the
    // player looks up from low down; what the camera shows is the boom
    // shortened to where the ground first comes between (limit), so a
    // camera looking up from beside the subject's feet comes in close
    // rather than climbing back over its head.
    class ChaseCamera {
    public:
      struct State {
        position_t position {};
        position_t target {};
        position_t pivot {};
        position_t view {};
        Vec3 avg_orientation {};
        displacement_t ahead {};
        velocity_t position_velocity {};
        velocity_t target_velocity {};
        speed_t speed {};
        meters_t boom {};
        float orbit_yaw {};
        float orbit_pitch {};
        bool is_uninitialized {};
        bool snap {};
      };

      ChaseCamera (degrees_t pitch_offset, meters_t distance)
          : m_pitch_offset (pitch_offset), m_distance (distance),
            m_nearest (0.35f * distance), m_speed (0 * u::m / u::s),
            m_is_uninitialized (true) {}

      // How far behind and how steeply above the subject the camera
      // sits: a figure on foot wants a closer, lower view than a bike.
      // `nearest` is as close as the ground may pull it in.
      void frame (degrees_t pitch_offset, meters_t distance, meters_t nearest) {
        m_pitch_offset = pitch_offset;
        m_distance = distance;
        m_nearest = nearest;
      }

      // Swings the camera about its look point at once, `yaw` radians
      // rightward and `pitch` radians higher, rather than through the
      // follow spring, so the mouse and the right stick feel direct. The
      // carried heading turns too, for a subject that turned with it.
      void turn (float yaw, float pitch);

      // Where the player has looked away from the default view behind the
      // subject: `yaw` radians around to the right, `pitch` radians above
      // the framing's own elevation. Changes take effect at once (turn).
      void aim (float yaw, float pitch);

      // Follows a subject whose pivot -- the point the boom swings about --
      // is at `position`, heading along `orientation`.
      void update (position_t position,
                   const Vec3& orientation,
                   velocity_t velocity,
                   seconds_t dt);

      // Shortens the boom to where the ground first comes between the
      // pivot and the camera, at once, and lets it out again gently over
      // `dt`; a camera that cannot come any closer lifts clear instead.
      void limit (const map::SurfaceGeometry& surface, seconds_t dt = 0 * u::s);

      // Lifts a placed camera clear of the ground and of any ridge
      // between it and what it looks at, without moving it otherwise.
      void keep_above (const map::SurfaceGeometry& surface);

      // Forgets where it was: the next update stands it straight at its
      // place behind the subject.
      void reset () {
        m_is_uninitialized = true;
      }

      State state () const {
        return { m_position,
                 m_target,
                 m_pivot,
                 m_view,
                 m_avg_orientation,
                 m_ahead,
                 m_position_velocity,
                 m_target_velocity,
                 m_speed,
                 m_boom,
                 m_orbit_yaw,
                 m_orbit_pitch,
                 m_is_uninitialized,
                 m_snap };
      }

      void restore (const State& state) {
        m_position = state.position;
        m_target = state.target;
        m_pivot = state.pivot;
        m_view = state.view;
        m_avg_orientation = state.avg_orientation;
        m_ahead = state.ahead;
        m_position_velocity = state.position_velocity;
        m_target_velocity = state.target_velocity;
        m_speed = state.speed;
        m_boom = state.boom;
        m_orbit_yaw = state.orbit_yaw;
        m_orbit_pitch = state.orbit_pitch;
        m_is_uninitialized = state.is_uninitialized;
        m_snap = state.snap;
      }

      // Directly position the camera (first-person mode bypasses the
      // chase smoothing entirely).
      void place (const Vec3& eye, const Vec3& target) {
        m_position = moppe::position (eye);
        m_view = m_position;
        m_target = moppe::position (target);
        m_pivot = m_target;
        m_position_velocity = moppe::velocity (Vec3 ());
        m_target_velocity = moppe::velocity (Vec3 ());
        m_is_uninitialized = false;
        m_snap = true;
      }

      Mat4 view_matrix () const {
        return Mat4::look_at (
          position_value (m_view), position_value (m_target), Vec3 (0, 1, 0));
      }

      Vec3 position () const {
        return position_value (m_view);
      }

      Vec3 forward () const {
        return normalized (position_value (m_target) - position_value (m_view));
      }

    private:
      radians_t m_pitch_offset;
      meters_t m_distance;
      meters_t m_nearest;

      // The far end of the boom as the spring carries it, the point it
      // looks at, the subject point it swings about, and the camera as
      // shown, drawn in along the boom out of the ground.
      position_t m_position;
      position_t m_target;
      position_t m_pivot;
      position_t m_view;
      Vec3 m_avg_orientation;
      displacement_t m_ahead;
      velocity_t m_position_velocity;
      velocity_t m_target_velocity;
      speed_t m_speed;
      // How much of the boom the ground leaves the camera.
      meters_t m_boom {};
      float m_orbit_yaw = 0;
      float m_orbit_pitch = 0;
      bool m_is_uninitialized;
      // The boom's length is taken as it is, not eased toward.
      bool m_snap = true;
    };
  }
}

#endif
