
#include <moppe/app/app.hh>

#include <iostream>

namespace moppe {
  class MoppeGLUT : public app::GLUTApplication {
  public:
    MoppeGLUT ()
      : GLUTApplication ("Moppe", 800, 600)
    { }

    void reshape (int width, int height)
    {
      m_width = width;
      m_height = height;

      std::cout << width << "x" << height << "\n";

      glMatrixMode (GL_PROJECTION);
      glLoadIdentity ();

      glViewport (0, 0, width, height);
      gluPerspective (90.0, (float) width / height, 0.1, 100.0);
    }

    void display ()
    {
      std::cout << "mamma?" << std::endl;
      
      glClear (GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);

      glMatrixMode (GL_MODELVIEW);
      glLoadIdentity ();

      gluLookAt (0.0, 10.0, 0.0, 
		 0.0, 0.0, 0.0,
		 0.0, 1.0, 0.0);

      glColor3f (1, 1, 1);

      glutSolidSphere (0.5, 10, 10);

//       glBegin (GL_QUADS);
//       glVertex3f (-1, 0, -1);
//       glVertex3f (-1, 0, 1);
//       glVertex3f (1, 0, 1);
//       glVertex3f (1, 0, -1);
//       glEnd ();

      glutSwapBuffers ();
    }
  };
}

int
main (int argc, char **argv)
{
  using namespace moppe;

  MoppeGLUT app;
  app::global_app = &app;

  app.initialize (argc, argv, GLUT_RGBA | GLUT_DOUBLE);
  app.run_main_loop ();
}
