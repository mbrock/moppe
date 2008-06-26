
#ifndef MOPPE_APP_HH
#define MOPPE_APP_HH

#include <GL/glew.h>
#include <moppe/app/gl.hh>

#include <sys/time.h>

#include <string>

namespace moppe {
namespace app {
  enum KeyStatus { 
    KEY_PRESSED,         KEY_RELEASED,
    KEY_SPECIAL_PRESSED, KEY_SPECIAL_RELEASED
  };

  class GLUTApplication {
  public:
    GLUTApplication (const std::string& title, int w, int h);
    virtual ~GLUTApplication ();

    void initialize    (int &argc, char **argv, int mode);
    void run_main_loop ();

    virtual void setup     ()
    { /* Override me. */ }

    virtual void display   ()
    { /* Override me. */ }

    virtual void reshape   (int width, int height)
    { /* Override me. */ }

    virtual void keyboard  (unsigned char code,
			    int mx, int my,
			    KeyStatus status)
    { /* Override me. */ }

    virtual void mouse     (int button, int state, int mx, int my)
    { /* Override me. */ }

    virtual void passive_motion (int mx, int my)
    { /* Override me. */ }

    virtual void idle ()
    { /* Override me. */ }

  protected:
    const std::string m_title;

    int m_width;
    int m_height;
  };

  extern GLUTApplication *global_app;

  class Timer {
  public:
    Timer () { reset (); }

    double elapsed () { return time () - t0; }
    void   reset   () { t0 = time (); }

  private:
    double time () const {
      ::timeval t;
      gettimeofday (&t, 0);
      return t.tv_sec + t.tv_usec / 1000000.0;
    }

    double t0;
  };
}
}

#endif
