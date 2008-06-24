#include <moppe/gfx/mouse.hh>

namespace moppe {
namespace gfx {
  MouseCameraController::MouseCameraController (int width, int height)
    : m_width	(width),
      m_height	(height),
      m_xp	(-1),
      m_yp	(-1),
      m_valid	(false),
      m_setting	(0, 0),
      m_min_pitch (degrees_to_radians (-90)),
      m_max_pitch (degrees_to_radians (90))
  { }

  void MouseCameraController::resize (int width, int height) {
    m_valid	= false;
    m_width	= width;
    m_height	= height;
  }

  void MouseCameraController::update (int x, int y) {
    if (m_valid)
      {
	float dx = (x - m_xp) * PI / m_width;
	float dy = (y - m_yp) * PI / m_height;
	
	m_setting.yaw   -= dx;
	m_setting.pitch += dy;

	clamp (m_setting.pitch, m_min_pitch, m_max_pitch);
      }
    
    m_xp = x;
    m_yp = y;
    m_valid = true;
  }

  void MouseCameraController::set_pitch_limits (float min,
						float max)
  {
    m_min_pitch = degrees_to_radians (min);
    m_max_pitch = degrees_to_radians (max);
  }
}
}
