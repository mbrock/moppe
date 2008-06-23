#ifndef MOPPE_CAMERA_HH
#define MOPPE_CAMERA_HH

#include <moppe/app/gl.hh>
#include <moppe/gfx/math.hh>

namespace moppe {
namespace gfx {
  class Camera {
  public:
    Camera (Vector3D<float> position, Vector3D<float> target)
      : m_position (position),
	m_target (target)
    { }

    void realize ();
    void set_pitch_and_yaw (float pitch, float yaw);

  private:
    Vector3D<float> m_position;
    Vector3D<float> m_target;
  };
}
}

#endif
