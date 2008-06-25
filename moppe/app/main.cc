
#include <moppe/app/app.hh>
#include <moppe/gfx/camera.hh>
#include <moppe/gfx/mouse.hh>
#include <moppe/map/generate.hh>
#include <moppe/gfx/terrain.hh>

#include <iostream>

#include <ctime>

namespace moppe {
  using namespace map;
  using namespace app;

  const Vector3D map_size (100 * one_meter,
			   20 * one_meter,
			   100 * one_meter);

  class MoppeGLUT: public GLUTApplication {
  public:
    MoppeGLUT ()
      : GLUTApplication ("Moppe", 800, 600),
	m_camera (Vector3D (0, 30 * one_meter, -30 * one_meter),
		  Vector3D (0, 0, 0)),
	m_mouse (800, 600),
	m_map1 (new RandomHeightMap (129, 129,
				     map_size,
				     0 + ::time (0))),
	m_map2 (new RandomHeightMap (129, 129,
				     map_size,
				     1 + ::time (0))),
	m_map3 (m_map1, m_map2, m_map1->size ()),
	m_terrain_renderer (m_map3)
    { }

    void setup () {
      glEnable (GL_DEPTH_TEST);
      glEnable (GL_LIGHTING);
      glEnable (GL_LIGHT0);
      //      glEnable (GL_NORMALIZE);
      //      glShadeModel (GL_SMOOTH);

      std::cout << "Randomizing maps...";
      m_map1->randomize_plasmally (0.95);
      m_map2->randomize_plasmally (0.8);
      std::cout << "done!\n";

      m_mouse.set_pitch_limits (-15, 10);
    }

    void mouse (int button, int state, int x, int y) {
      if (state != GLUT_UP)
	return;

      m_map1 = m_map2;
      m_map2 = boost::shared_ptr<RandomHeightMap>
	(new RandomHeightMap (129, 129,
			      map_size,
			      ::time (0)));
      m_map2->randomize_plasmally (0.995);
      m_map3.change_maps (m_map1, m_map2);
      m_map3.set_blending_factor (0);
    }

    void idle () {
      static const float dt    = 1 / 30.0;
      static const float total = 3;

      if (!m_map3.done ())
	if (m_timer.elapsed () >= dt)
	  {
	    m_timer.reset ();
	    m_map3.increase_blending_factor (dt / total);

	    if (m_map3.done ())
	      m_terrain_renderer.regenerate ();

	    glutPostRedisplay ();
	  }
    }

    void reshape (int width, int height) {
      gl::global_config.screen_width = width;
      gl::global_config.screen_height = height;

      m_width = width;
      m_height = height;

      glMatrixMode (GL_PROJECTION);
      glLoadIdentity ();

      glViewport (0, 0, width, height);
      gluPerspective (100.0, 1.0 * width / height, 0.01, 100.0);
      glutPostRedisplay ();

      check_gl ();

      m_mouse.resize (width, height);
    }

    void passive_motion (int x, int y) {
      m_mouse.update (x, y);
      glutPostRedisplay ();
    }

    void display () {
      glClearColor (0, 0, 0, 0);
      glColor3f (1, 1, 1);

      glClear (GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
      glMatrixMode (GL_MODELVIEW);

      m_camera.set (m_mouse.setting ());
      m_camera.realize ();

      if (m_map3.done ())
	m_terrain_renderer.render ();
      else
	m_terrain_renderer.render_directly ();

      m_camera.draw_debug_text ();
      m_mouse.draw_debug_text ();

      check_gl ();

      glutSwapBuffers ();
    }

  private:
    void setup_lights () {
      GLfloat ambient[] = {0.5, 0.1, 0.1, 1.0};
      glLightModelfv (GL_LIGHT_MODEL_AMBIENT, ambient);

      GLfloat light0_color[] = {0.0, 1.0, 0.5, 1.0};
      GLfloat light0_position[] = {2, 2, 2};
      glLightfv (GL_LIGHT0, GL_DIFFUSE, light0_color);
      glLightfv (GL_LIGHT0, GL_POSITION, light0_position);
    }

  private:
    gfx::Camera m_camera;
    gfx::MouseCameraController m_mouse;

    Timer m_timer;
    boost::shared_ptr<map::RandomHeightMap> m_map1;
    boost::shared_ptr<map::RandomHeightMap> m_map2;
    map::InterpolatingHeightMap m_map3;

    gfx::TerrainRenderer m_terrain_renderer;
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
