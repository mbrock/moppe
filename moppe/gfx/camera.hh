#ifndef MOPPE_CAMERA_HH
#define MOPPE_CAMERA_HH

#include <moppe/app/gl.hh>
#include <moppe/gfx/math.hh>

#include <iostream>

namespace moppe {
namespace gfx {
  struct CameraSetting {
    CameraSetting (float pitch, float yaw)
      : pitch (pitch), yaw (yaw)
    { }

    float pitch;
    float yaw;
  };

  class Camera {
  public:
    Camera (Vector3D position, Vector3D target)
      : m_position          (position),
	m_target            (target),
	m_original_position (position),
	m_original_target   (target)
    { }

    void realize ();
    void set (const CameraSetting& setting);

  private:
    Vector3D m_position;
    Vector3D m_target;

    const Vector3D m_original_position;
    const Vector3D m_original_target;
  };
}
}

#endif
