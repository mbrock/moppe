
#ifndef MOPPE_APP_HH
#define MOPPE_APP_HH

#include <glut.h>

#include <string>

namespace moppe {
namespace app {
  enum KeyStatus { 
    KEY_PRESSED, KEY_RELEASED,
    KEY_SPECIAL_PRESSED, KEY_SPECIAL_RELEASED
  };

  class GLUTApplication {
  public:
    GLUTApplication (const std::string& title, int w, int h);
    virtual ~GLUTApplication ();

    void initialize (int &argc, char **argv, int mode);
    void run_main_loop ();

    virtual void display   ()
    { /* Override me. */ }

    virtual void reshape   (int width, int height)
    { /* Override me. */ }

    virtual void keyboard  (unsigned char code,
			    int mx, int my,
			    KeyStatus status)
    { /* Override me. */ }

  protected:
    const std::string m_title;

    int m_width;
    int m_height;
  };

  extern GLUTApplication *global_app;
}
}

#endif
