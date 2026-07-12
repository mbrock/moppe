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
        Vec3 position {};
        Vec3 target {};
        Vec3 avg_orientation {};
        Vec3 ahead {};
        Vec3 position_velocity {};
        Vec3 target_velocity {};
        float speed {};
        bool is_uninitialized {};
      };

      ChaseCamera (degrees_t pitch_offset, meters_t distance)
          : m_pitch_offset (pitch_offset), m_distance (distance), m_speed (0),
            m_is_uninitialized (true) {}

      void update (const Vec3& position,
                   const Vec3& orientation,
                   const Vec3& velocity,
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
        m_position = eye;
        m_target = target;
        m_position_velocity = Vec3 ();
        m_target_velocity = Vec3 ();
        m_is_uninitialized = false;
      }

      Mat4 view_matrix () const {
        return Mat4::look_at (m_position, m_target, Vec3 (0, 1, 0));
      }

      Vec3 position () const {
        return m_position;
      }

      Vec3 forward () const {
        return normalized (m_target - m_position);
      }

    private:
      radians_t m_pitch_offset;
      meters_t m_distance;

      Vec3 m_position;
      Vec3 m_target;
      Vec3 m_avg_orientation;
      Vec3 m_ahead;
      Vec3 m_position_velocity;
      Vec3 m_target_velocity;
      float m_speed;
      bool m_is_uninitialized;
      float m_horizontal_scale = 1.0f;
      float m_vertical_scale = 1.0f;
    };
  }
}

#endif
