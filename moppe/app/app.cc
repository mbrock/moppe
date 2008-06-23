
#include <moppe/app/app.hh>

namespace moppe {
namespace app {
  GLUTApplication::GLUTApplication (const std::string& title, int w, int h)
    : m_title (title),
      m_width (w),
      m_height (h)
  { }

  GLUTApplication::~GLUTApplication ()
  { }

  void
  GLUTApplication::initialize (int &argc, char **argv, int mode)
  {
    ::glutInit (&argc, argv);
    ::glutInitDisplayMode (mode);
  }

  static void global_display_func ()
  { global_app->display (); }

  static void global_reshape_func (int w, int h)
  { global_app->reshape (w, h); }

#define DEF_GKF(t, s, k)				     \
  static void s (t code, int mx, int my) \
  { global_app->keyboard (code, mx, my, k); }

  DEF_GKF (unsigned char, global_keyboard_func, KEY_PRESSED);
  DEF_GKF (unsigned char, global_keyboard_up_func, KEY_RELEASED);
  DEF_GKF (int, global_special_func, KEY_SPECIAL_PRESSED);
  DEF_GKF (int, global_special_up_func, KEY_SPECIAL_RELEASED);

  void
  GLUTApplication::run_main_loop ()
  {
    glutCreateWindow (m_title.c_str ());
    glutReshapeWindow (m_width, m_height);
    
    glutDisplayFunc (global_display_func);
    glutReshapeFunc (global_reshape_func);
    glutKeyboardFunc (global_keyboard_func);
    glutKeyboardUpFunc (global_keyboard_up_func);
    glutSpecialFunc (global_special_func);
    glutSpecialUpFunc (global_special_func);

    glutMainLoop ();
  }

  GLUTApplication* global_app = 0;
}
}

