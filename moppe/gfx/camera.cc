#include <moppe/gfx/camera.hh>

namespace moppe {
namespace gfx {
  void Camera::realize ()
  {
    gl::ScopedAttribSaver matrix_mode (GL_TRANSFORM_BIT);

    glMatrixMode (GL_MODELVIEW);
    glLoadIdentity ();
    
    gluLookAt (m_position.x, m_position.y, m_position.z,
	       m_target.x, m_target.y, m_target.z,
	       0, 1, 0);
  }

  void Camera::set (const CameraSetting& setting)
  {
    const Vector3D<float> pitch_axis (1, 0, 0);
    const Vector3D<float> yaw_axis   (0, 1, 0);

    Quaternion<float> qy
      (Quaternion<float>::rotation (yaw_axis, setting.yaw));
    Quaternion<float> qp
      (Quaternion<float>::rotation (pitch_axis, setting.pitch));

    m_position = Quaternion<float>::rotate (m_original_position, qy * qp);
  }
}
}
