#ifndef MOPPE_CAMERA_HH
#define MOPPE_CAMERA_HH

#include <moppe/app/gl.hh>
#include <moppe/gfx/math.hh>

#include <boost/format.hpp>

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

    void draw_debug_text () {
      std::string text =
	(boost::format ("Looking at %1%.\nFrom %2%.")
	 % m_target % m_position).str ();
      gl::draw_glut_text (GLUT_BITMAP_HELVETICA_18,
			  20, 20, text);
    }

  private:
    Vector3D m_position;
    Vector3D m_target;

    const Vector3D m_original_position;
    const Vector3D m_original_target;
  };
}
}

#endif
