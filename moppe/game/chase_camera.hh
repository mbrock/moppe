#ifndef MOPPE_GAME_CHASE_CAMERA_HH
#define MOPPE_GAME_CHASE_CAMERA_HH

#include <moppe/gfx/mat4.hh>
#include <moppe/gfx/math.hh>
#include <moppe/map/generate.hh>

namespace moppe {
  namespace game {
    // The chase camera from gfx::ThirdPersonCamera, GL-free: instead
    // of realize()-ing onto the GL matrix stack it hands out a view
    // matrix. Its follow spring and terrain corridor are frame-rate
    // independent.
    class ChaseCamera {
    public:
      struct State {
        position_t position {};
        position_t target {};
        Vec3 avg_orientation {};
        displacement_t ahead {};
        velocity_t position_velocity {};
        velocity_t target_velocity {};
        speed_t speed {};
        bool is_uninitialized {};
      };

      ChaseCamera (degrees_t pitch_offset, meters_t distance)
          : m_pitch_offset (pitch_offset), m_distance (distance),
            m_speed (0 * u::m / u::s), m_is_uninitialized (true) {}

      void update (position_t position,
                   const Vec3& orientation,
                   velocity_t velocity,
                   seconds_t dt);
      void limit (const map::HeightMap& map);

      void set_landscape_scale (float horizontal, float vertical) {
        m_horizontal_scale = horizontal;
        m_vertical_scale = vertical;
      }

      State state () const {
        return { m_position,
                 m_target,
                 m_avg_orientation,
                 m_ahead,
                 m_position_velocity,
                 m_target_velocity,
                 m_speed,
                 m_is_uninitialized };
      }

      void restore (const State& state) {
        m_position = state.position;
        m_target = state.target;
        m_avg_orientation = state.avg_orientation;
        m_ahead = state.ahead;
        m_position_velocity = state.position_velocity;
        m_target_velocity = state.target_velocity;
        m_speed = state.speed;
        m_is_uninitialized = state.is_uninitialized;
      }

      // Directly position the camera (first-person mode bypasses the
      // chase smoothing entirely).
      void place (const Vec3& eye, const Vec3& target) {
        m_position = moppe::position (eye);
        m_target = moppe::position (target);
        m_position_velocity = moppe::velocity (Vec3 ());
        m_target_velocity = moppe::velocity (Vec3 ());
        m_is_uninitialized = false;
      }

      Mat4 view_matrix () const {
        return Mat4::look_at (position_value (m_position),
                              position_value (m_target),
                              Vec3 (0, 1, 0));
      }

      Vec3 position () const {
        return position_value (m_position);
      }

      Vec3 forward () const {
        return normalized (position_value (m_target) -
                           position_value (m_position));
      }

    private:
      radians_t m_pitch_offset;
      meters_t m_distance;

      position_t m_position;
      position_t m_target;
      Vec3 m_avg_orientation;
      displacement_t m_ahead;
      velocity_t m_position_velocity;
      velocity_t m_target_velocity;
      speed_t m_speed;
      bool m_is_uninitialized;
      float m_horizontal_scale = 1.0f;
      float m_vertical_scale = 1.0f;
    };
  }
}

#endif
