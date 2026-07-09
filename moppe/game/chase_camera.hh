#ifndef MOPPE_GAME_CHASE_CAMERA_HH
#define MOPPE_GAME_CHASE_CAMERA_HH

#include <moppe/gfx/math.hh>
#include <moppe/gfx/mat4.hh>
#include <moppe/map/generate.hh>

namespace moppe {
namespace game {
  // The chase camera from gfx::ThirdPersonCamera, GL-free: instead
  // of realize()-ing onto the GL matrix stack it hands out a view
  // matrix.  All smoothing/clamping math is unchanged.
  class ChaseCamera {
  public:
    ChaseCamera (degrees_t pitch_offset, meters_t distance)
      : m_pitch_offset (degrees_to_radians (pitch_offset)),
	m_distance (distance),
	m_speed (0),
	m_dt (1 / 60.0f),
	m_is_uninitialized (true)
    { }

    void update (const Vector3D& position,
		 const Vector3D& orientation,
		 const Vector3D& velocity,
		 seconds_t dt);
    void limit (const map::HeightMap& map);

    // Directly position the camera (first-person mode bypasses the
    // chase smoothing entirely).
    void place (const Vector3D& eye, const Vector3D& target) {
      m_position = eye;
      m_target = target;
      m_is_uninitialized = false;
    }

    Mat4 view_matrix () const {
      return Mat4::look_at (m_position, m_target,
			    Vector3D (0, 1, 0));
    }

    Vector3D position () const { return m_position; }

    Vector3D forward () const
    { return (m_target - m_position).normalized (); }

  private:
    radians_t m_pitch_offset;
    meters_t  m_distance;

    Vector3D m_position;
    Vector3D m_target;
    Vector3D m_avg_orientation;
    Vector3D m_ahead;
    float    m_speed;
    float    m_dt;

    bool m_is_uninitialized;
  };
}
}

#endif
