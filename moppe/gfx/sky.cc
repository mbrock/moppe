#include <moppe/gfx/sky.hh>

namespace moppe {
namespace gfx {
  Sky::Sky (const std::string& filename)
    : m_texture (filename)
  { }

  void
  Sky::load () {
    m_texture.load ();
  }

  void
  Sky::render () const {
    glDisable (GL_DEPTH_TEST);
    m_texture.bind (0);

    GLUquadricObj *q (gluNewQuadric ());
    gluQuadricDrawStyle (q, GLU_FILL);
    gluQuadricNormals (q, GLU_SMOOTH);
    gluQuadricTexture (q, GL_TRUE);
    gluSphere (q, 2.0, 20, 20);
    gluDeleteQuadric (q);

    glEnable (GL_DEPTH_TEST);
  }
}
}
