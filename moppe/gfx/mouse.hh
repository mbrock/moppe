#ifndef MOPPE_MOUSE_HH
#define MOPPE_MOUSE_HH

#include <moppe/gfx/camera.hh>

namespace moppe {
namespace gfx {
  class MouseCameraController {
  public:
    MouseCameraController (int width, int height);

    void resize (int width, int height) {
      m_valid	= false;
      m_width	= width;
      m_height	= height;
    }

    CameraSetting setting () const { return m_setting; }

    void update (int x, int y);

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
