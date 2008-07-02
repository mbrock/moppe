
#ifndef MOPPE_GL_HH
#define MOPPE_GL_HH

#include <iostream>
#include <cstdlib>

#include <moppe/gfx/math.hh>

#ifdef MAC
# include <glut.h>
# include <glu.h>
# include <OpenGL/glext.h>
#else
# include <GL/glew.h>
# include <GL/gl.h>
# include <GL/glut.h>
# include <GL/glu.h>
#endif

#include <stdexcept>

namespace moppe {
  struct gl_error: public std::runtime_error { 
    gl_error (const std::string& what)
      : runtime_error (what),
	m_what (what)
    { }

    ~gl_error () throw () {}

    const char* what () const throw () { return m_what.c_str (); }

    std::string m_what;
  };

  inline void check_gl ()
  {
#ifdef DEBUG
    GLint e = glGetError();
    if (e != GL_NO_ERROR)
      {
	std::cerr << gluErrorString (e) << std::endl;
	::abort ();
	//      throw gl_error (reinterpret_cast<const char*> (gluErrorString (e)));
      }
#endif
  }

  namespace gl {
    struct Configuration {
      int screen_width;
      int screen_height;
    };

    extern Configuration global_config;

    void vertex (const Vector3D&);
    void normal (const Vector3D&);
    void translate (const Vector3D&);

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
    void draw_direction (const Vector3D& v);
    
    class FrameBufferObject {
    public:
      void create (int w, int h) {
	glGenFramebuffersEXT (1, &m_id);
	
	bind ();
	
	glGenTextures (1, &m_tex);
	glBindTexture (GL_TEXTURE_2D, m_tex);
	glTexParameteri (GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
	glTexImage2D (GL_TEXTURE_2D, 0, GL_RGBA8, w, h, 0,
		      GL_RGBA, GL_UNSIGNED_BYTE, NULL);
	glFramebufferTexture2DEXT (GL_FRAMEBUFFER_EXT, GL_COLOR_ATTACHMENT0_EXT,
				   GL_TEXTURE_2D, m_tex, 0);
	
	int status = glCheckFramebufferStatusEXT(GL_FRAMEBUFFER_EXT);
	if (status != GL_FRAMEBUFFER_COMPLETE_EXT)
	  throw std::runtime_error ("nah");
	
	unbind ();
      }
      
      void bind () {
	glBindFramebufferEXT (GL_FRAMEBUFFER_EXT, m_id);
      }
      
      void unbind () {
	glBindFramebufferEXT (GL_FRAMEBUFFER_EXT, 0);
      }
      
    private:
      GLuint m_id, m_tex;
    };
  }
}

#endif
