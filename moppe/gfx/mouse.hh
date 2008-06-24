#ifndef MOPPE_MOUSE_HH
#define MOPPE_MOUSE_HH

#include <moppe/gfx/camera.hh>

#include <boost/format.hpp>

namespace moppe {
namespace gfx {
  // Receives mouse movement updates to produce camera settings.
  class MouseCameraController {
  public:
    MouseCameraController (int width, int height);

    // Call this when the window size changes.
    void resize (int width, int height);
    // Update the camera setting.
    void update (int x, int y);

    // Return the current desired camera setting.
    CameraSetting setting () const { return m_setting; }

    // Set the smallest and largest pitch angles.
    void set_pitch_limits (degrees_t min, degrees_t max);

    void draw_debug_text () {
      std::string text =
	(boost::format ("Pitch %1%.   Yaw %2%.")
	 % m_setting.pitch % m_setting.yaw).str ();
      gl::draw_glut_text (GLUT_BITMAP_HELVETICA_18,
			  20, 40, text);
    }

  private:
    // Window size.
    int m_width;
    int m_height;

    // The previous mouse coordinates.
    int m_xp;
    int m_yp;

    // Do we have valid xp and yp?
    bool m_valid;

    // Current setting.
    CameraSetting m_setting;

    // Pitch limits.
    radians_t m_min_pitch;
    radians_t m_max_pitch;
  };
}
}

#endif
