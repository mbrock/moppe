
#ifndef MOPPE_GL_HH
#define MOPPE_GL_HH

#include <glut.h>

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
}

#endif
