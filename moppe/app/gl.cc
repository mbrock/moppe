
#include <moppe/app/gl.hh>

#include <iostream>

namespace moppe {
namespace gl {
  Configuration global_config;

  void vertex (const Vector3D& v)
  { glVertex3f (v.x, v.y, v.z); }

  void normal (const Vector3D& v)
  { glNormal3f (v.x, v.y, v.z); }

  void translate (const Vector3D& v)
  { glTranslatef (v.x, v.y, v.z); }

  ScopedOrthographicMode::ScopedOrthographicMode ()
    : m_depth_settings (GL_DEPTH_BUFFER_BIT),
      m_enable_settings (GL_ENABLE_BIT)
  {
    ScopedAttribSaver m_transform (GL_TRANSFORM_BIT);
    
    glDisable (GL_DEPTH_TEST);
    glDisable (GL_TEXTURE_2D);
    glDisable (GL_LIGHTING);

    glMatrixMode (GL_PROJECTION);
    glPushMatrix ();
    glLoadIdentity ();

    gluOrtho2D (0, global_config.screen_width,
		0, global_config.screen_height);

    // Invert Y axis so top is 0!
    glScalef (1, -1, 1);
    // Move origin to top left!
    glTranslatef (0, -global_config.screen_height, 0);
  }

  ScopedOrthographicMode::~ScopedOrthographicMode () {
    ScopedAttribSaver m_transform (GL_TRANSFORM_BIT);

    glMatrixMode (GL_PROJECTION);
    glPopMatrix ();
  }

  void draw_glut_text (void *font, int x, int y, const std::string& s) {
    gl::ScopedOrthographicMode ortho;    
    gl::ScopedMatrixSaver model_view_matrix;

    glLoadIdentity ();
    glRasterPos2i (x, y);

    for (std::string::const_iterator i = s.begin ();
	 i != s.end ();
	 ++i)
      glutBitmapCharacter (font, *i);
  }

  void draw_direction (const Vector3D& v) {
    glBegin (GL_LINES);
    vertex (Vector3D ());
    vertex (v * 10);
    glEnd ();
  }
}
}
