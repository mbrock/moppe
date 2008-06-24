
#ifndef MOPPE_GL_HH
#define MOPPE_GL_HH

#include <iostream>

#include <moppe/gfx/math.hh>

#ifdef MAC
# include <glut.h>
#else
# include <GL/gl.h>
# include <GL/glut.h>
#endif

#include <stdexcept>

namespace moppe {
  struct gl_error: public std::runtime_error { 
    gl_error (const std::string& what)
      : runtime_error (what)
    { }
  };

  inline void check_gl ()
  {
#ifdef DEBUG
    GLint e = glGetError();
    if (e != GL_NO_ERROR)
      throw gl_error (reinterpret_cast<const char*> (gluErrorString (e)));
#endif
  }

  namespace gl {
    struct Configuration {
      int screen_width;
      int screen_height;
    };

    extern Configuration global_config;

    struct ScopedAttribSaver {
      ScopedAttribSaver  (GLbitfield mask) { glPushAttrib (mask); }
      ~ScopedAttribSaver ()               { glPopAttrib  ();     }
    };

    struct ScopedMatrixSaver {
      ScopedMatrixSaver  () { glPushMatrix (); }
      ~ScopedMatrixSaver () { glPopMatrix  (); }
    };

    struct ScopedOrthographicMode {
      ScopedOrthographicMode ();
      ~ScopedOrthographicMode ();

    private:
      ScopedAttribSaver m_depth_settings;
      ScopedAttribSaver m_enable_settings;
    };

    void draw_glut_text (void *font, int x, int y, const std::string& s);
  }
}

#endif
