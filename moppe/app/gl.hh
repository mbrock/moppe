
#ifndef MOPPE_GL_HH
#define MOPPE_GL_HH

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
    struct ScopedAttribSaver {
       ScopedAttribSaver (GLbitfield mask) { glPushAttrib (mask); }
      ~ScopedAttribSaver ()                { glPopAttrib  ();     }
    };
  }
}

#endif
