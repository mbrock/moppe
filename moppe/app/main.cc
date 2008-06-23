
#include <moppe/app/app.hh>
#include <moppe/gfx/camera.hh>

#include <iostream>

namespace moppe {
  class MoppeGLUT : public app::GLUTApplication {
  public:
    MoppeGLUT ()
      : GLUTApplication ("Moppe", 800, 600),
	m_camera (Vector3D<float> (0, 0, 10),
		  Vector3D<float> (0, 0, 0)),
	m_mouse (800, 600)
    { }

    void setup ()
    {
      glEnable (GL_DEPTH_TEST);
      glEnable (GL_LIGHTING);
      glEnable (GL_LIGHT0);
      glEnable (GL_NORMALIZE);
      glShadeModel (GL_SMOOTH);
    }

    void reshape (int width, int height)
    {
      m_width = width;
      m_height = height;

      std::cout << width << "x" << height << "\n";

      glMatrixMode (GL_PROJECTION);
      glLoadIdentity ();

      glViewport (0, 0, width, height);
      gluPerspective (60.0, 1.0 * width / height, 0.01, 100.0);
      glutPostRedisplay ();

      check_gl ();

      m_mouse.resize (width, height);
    }

    void mouse (int button, int state, int x, int y)
    {
      std::cout << x << "x" << y << "\n";
    }

    void passive_motion (int x, int y)
    {
      std::cout << x << "x" << y << "\n";
      m_mouse.update (x, y);
      glutPostRedisplay ();
    }

    void display ()
    {
      std::cout << "mamma?" << std::endl;
      
      glClear (GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);

      glMatrixMode (GL_MODELVIEW);

      m_camera.set (m_mouse.setting ());
      m_camera.realize ();

      check_gl ();

      GLfloat ambient[] = {0.5, 0.1, 0.1, 1.0};
      glLightModelfv (GL_LIGHT_MODEL_AMBIENT, ambient);

      GLfloat light0_color[] = {0.0, 1.0, 0.5, 1.0};
      GLfloat light0_position[] = {2, 2, 2};
      glLightfv (GL_LIGHT0, GL_DIFFUSE, light0_color);
      glLightfv (GL_LIGHT0, GL_POSITION, light0_position);

      glColor3f (1, 1, 1);

      glTranslatef (0, 0, 0);
      glutSolidSphere (1, 20, 20);

      check_gl ();

      glFlush ();
      glutSwapBuffers ();
    }

  private:
    gfx::Camera m_camera;
    gfx::MouseCameraController m_mouse;
  };
}

int
main (int argc, char **argv)
{
  using namespace moppe;

  MoppeGLUT app;
  app::global_app = &app;

  app.initialize (argc, argv, GLUT_RGBA | GLUT_DOUBLE | GLUT_DEPTH);

  try { app.run_main_loop (); }
  catch (const std::exception& e)
    {
      std::cerr << "\nError: " << e.what () << "\n";
      std::exit (-1);
    }
}
