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
        Vector3D position {};
        Vector3D target {};
        Vector3D avg_orientation {};
        Vector3D ahead {};
        Vector3D position_velocity {};
        Vector3D target_velocity {};
        float speed {};
        bool is_uninitialized {};
      };

      ChaseCamera (degrees_t pitch_offset, meters_t distance)
          : m_pitch_offset (pitch_offset), m_distance (distance), m_speed (0),
            m_is_uninitialized (true) {}

      void update (const Vector3D& position,
                   const Vector3D& orientation,
                   const Vector3D& velocity,
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
      void place (const Vector3D& eye, const Vector3D& target) {
        m_position = eye;
        m_target = target;
        m_position_velocity = Vector3D ();
        m_target_velocity = Vector3D ();
        m_is_uninitialized = false;
      }

      Mat4 view_matrix () const {
        return Mat4::look_at (m_position, m_target, Vector3D (0, 1, 0));
      }

      Vector3D position () const {
        return m_position;
      }

      Vector3D forward () const {
        return (m_target - m_position).normalized ();
      }

    private:
      radians_t m_pitch_offset;
      meters_t m_distance;

      Vector3D m_position;
      Vector3D m_target;
      Vector3D m_avg_orientation;
      Vector3D m_ahead;
      Vector3D m_position_velocity;
      Vector3D m_target_velocity;
      float m_speed;
      bool m_is_uninitialized;
      float m_horizontal_scale = 1.0f;
      float m_vertical_scale = 1.0f;
    };
  }
}

#endif
