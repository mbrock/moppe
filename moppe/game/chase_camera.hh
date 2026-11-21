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
        direction_t avg_orientation {};
        displacement_t ahead {};
        velocity_t position_velocity {};
        velocity_t target_velocity {};
        speed_t speed {};
        meters_t boom {};
        radians_t orbit_yaw {};
        radians_t orbit_pitch {};
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

      // Swings the camera about its look point at once, `yaw`
      // rightward and `pitch` higher, rather than through the
      // follow spring, so the mouse and the right stick feel direct. The
      // carried heading turns too, for a subject that turned with it.
      void turn (radians_t yaw, radians_t pitch);

      // Where the player has looked away from the default view behind the
      // subject: `yaw` around to the right, `pitch` above
      // the framing's own elevation. Changes take effect at once (turn).
      void aim (radians_t yaw, radians_t pitch);

      // Follows a subject whose pivot -- the point the boom swings about --
      // is at `position`, heading along `orientation`.
      void update (position_t position,
                   direction_t orientation,
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
      // Keeps the eye off the water's surface at `level`, under it with
      // `below` and over it otherwise, so the lens is never half in.
      void hold_off_water (meters_t level, bool below) {
        constexpr meters_t clear = 0.35f * u::m;
        Vec3 view = position_value (m_view);
        const float y = view[1];
        view[1] = below ? std::min (y, meters_value (level - clear))
                        : std::max (y, meters_value (level + clear));
        m_view = moppe::position (view);
      }

      void place (position_t eye, position_t target) {
        m_position = eye;
        m_view = m_position;
        m_target = target;
        m_pivot = m_target;
        m_position_velocity = moppe::velocity (Vec3 ());
        m_target_velocity = moppe::velocity (Vec3 ());
        m_is_uninitialized = false;
        m_snap = true;
      }

      // A room the camera stays inside while the subject is in it: an
      // upright box about `centre` (on its floor), `half_x` along `x` (a
      // horizontal unit vector) and `half_z` across, from `floor` to
      // `ceiling` over the centre.
      struct Room {
        position_t centre {};
        direction_t x = unit_direction (Vec3 (1, 0, 0));
        meters_t half_x = 0.0f * u::m;
        meters_t half_z = 0.0f * u::m;
        meters_t floor = 0.0f * u::m;
        meters_t ceiling = 0.0f * u::m;
      };

      // Brings the camera in from beyond the room's walls when what it
      // swings about is inside, so a wall never stands between them.
      void confine (const Room& room);

      Mat4 view_matrix () const {
        return Mat4::look_at (
          position_value (m_view), position_value (m_target), Vec3 (0, 1, 0));
      }

      position_t position () const {
        return m_view;
      }

      direction_t forward () const {
        return unit_direction (
          normalized (position_value (m_target) - position_value (m_view)));
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
      direction_t m_avg_orientation;
      displacement_t m_ahead;
      velocity_t m_position_velocity;
      velocity_t m_target_velocity;
      speed_t m_speed;
      // How much of the boom the ground leaves the camera.
      meters_t m_boom {};
      radians_t m_orbit_yaw = 0.0f * u::rad;
      radians_t m_orbit_pitch = 0.0f * u::rad;
      bool m_is_uninitialized;
      // The boom's length is taken as it is, not eased toward.
      bool m_snap = true;
    };
  }
}

#endif
