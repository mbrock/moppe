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
    Camera (Vector3D<float> position, Vector3D<float> target)
      : m_position (position),
	m_target (target),
	m_original_position (position)
    { }

    void realize ();
    void set (const CameraSetting& setting);

  private:
    Vector3D<float> m_position;
    const Vector3D<float> m_original_position;
    Vector3D<float> m_target;
  };

  class MouseCameraController {
  public:
    MouseCameraController (int width, int height)
      : m_width (width),
	m_height (height),
	m_valid (false),
	m_xp (-1),
	m_yp (-1),
	m_setting (0, 0)
    { }

    void resize (int width, int height)
    {
      m_valid = false;
      m_width = width;
      m_height = height;
    }

    CameraSetting setting () const { return m_setting; }

    void on_leave () { m_valid = false; }

    void update (int x, int y)
    {
      if (m_valid)
	{
	  float dx = (x - m_xp) * PI2 / m_width;
	  float dy = (y - m_yp) * PI2 / m_height;

	  m_setting.yaw += dx;
	  m_setting.pitch += dy;

	  std::cout << "+ " << dx << "x" << dy << "\n";
	}

      m_xp = x;
      m_yp = y;
      m_valid = true;
    }

  private:
    int m_width;
    int m_height;

    bool m_valid;

    int m_xp;
    int m_yp;

    CameraSetting m_setting;
  };
}
}

#endif
