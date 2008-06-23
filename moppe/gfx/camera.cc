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

    m_target = Quaternion::rotate (m_original_target, qy * qp);
  }
}
}
