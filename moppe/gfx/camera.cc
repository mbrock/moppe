#include <moppe/gfx/camera.hh>

namespace moppe {
namespace gfx {
  void Camera::realize ()
  {
    gl::ScopedAttribSaver matrix_mode (GL_TRANSFORM_BIT);

    glMatrixMode (GL_MODELVIEW);
    glLoadIdentity ();
    
    gluLookAt (m_position.x, m_position.y, m_position.z,
	       m_target.x,   m_target.y,   m_target.z,
	       0, 1, 0);
  }

  void Camera::set (const CameraSetting& setting)
  {
    const Vector3D pitch_axis (1, 0, 0);
    const Vector3D yaw_axis   (0, 1, 0);

    Quaternion qy (Quaternion::rotation (yaw_axis, setting.yaw));
    Quaternion qp (Quaternion::rotation (pitch_axis, setting.pitch));

    m_position = m_original_position + 
      Quaternion::rotate (Vector3D (0, 1 * one_meter, 0), qy * qp);
  }

  void
  ThirdPersonCamera::update (const Vector3D& position,
			     const Vector3D& orientation,
			     seconds_t dt)
  {
    float alpha = 0.05f;

    if (m_is_uninitialized)
      {
	alpha = 1;
	m_is_uninitialized = false;
      }

    // TODO: consider dt
    m_avg_orientation = linear_vector_interpolate (m_avg_orientation,
						   orientation,
						   alpha);
    m_avg_orientation.normalize ();

    Vector3D o = m_avg_orientation;
    Vector3D d = 
      Quaternion::rotate (-o, o.cross (o.scaled (Vector3D (1, -1, 1))), 
			  m_pitch_offset);
    d.normalize ();

    m_target = position;
    m_position = linear_vector_interpolate (m_position,
					    position + d * m_distance,
					    alpha);
  }

  void
  ThirdPersonCamera::limit (const map::HeightMap& map)
  {
    float min_y = map.interpolated_height (m_position.x, m_position.z);
    m_position.y = max (min_y + 5 * one_meter, m_position.y);
  }

  void
  ThirdPersonCamera::realize () const
  {
    gl::ScopedAttribSaver matrix_mode (GL_TRANSFORM_BIT);

    glMatrixMode (GL_MODELVIEW);
    glLoadIdentity ();
    
    gluLookAt (m_position.x, m_position.y, m_position.z,
	       m_target.x,   m_target.y,   m_target.z,
	       0, 1, 0);
  }
}
}
