#include <moppe/gfx/mouse.hh>

namespace moppe {
namespace gfx {
  MouseCameraController::MouseCameraController (int width, int height)
    : m_width	(width),
      m_height	(height),
      m_valid	(false),
      m_xp	(-1),
      m_yp	(-1),
      m_setting	(0, 0)
  { }

  void MouseCameraController::update (int x, int y) {
    if (m_valid)
      {
	float dx = (x - m_xp) * PI / m_width;
	float dy = (y - m_yp) * PI / m_height;
	
	m_setting.yaw -= dx;
	m_setting.pitch += dy;
	
	if (m_setting.pitch > 1)
	  m_setting.pitch = 1;
	else if (m_setting.pitch < -1)
	  m_setting.pitch = -1;
      }
    
    m_xp = x;
    m_yp = y;
    m_valid = true;
  }
}
}
