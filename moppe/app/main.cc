
#include <moppe/app/app.hh>
#include <moppe/gfx/camera.hh>
#include <moppe/gfx/mouse.hh>
#include <moppe/map/generate.hh>
#include <moppe/gfx/terrain.hh>
#include <moppe/mov/vehicle.hh>

#include <iostream>

#include <ctime>

namespace moppe {
  using namespace map;
  using namespace app;

  const Vector3D map_size (2000 * one_meter,
			   600 * one_meter,
			   2000 * one_meter);

  const int resolution = 257;

  const Vector3D fog (0.5, 0.9, 0.5);

  class MoppeGLUT: public GLUTApplication {
  public:
    MoppeGLUT ()
      : GLUTApplication ("Moppe", 800, 600),
	m_camera (80, 5 * one_meter),
	m_mouse (800, 600),
	m_map1 (new RandomHeightMap (resolution, resolution,
				     map_size,
				     0 + ::time (0))),
	m_map2 (new RandomHeightMap (resolution, resolution,
				     map_size,
				     1 + ::time (0))),
	m_map3 (m_map1, m_map2, m_map1->size ()),
	m_terrain_renderer (*m_map1),
	m_vehicle (Vector3D (0.2, 0.0, 0.2), 45, m_map3)
    { }

    void setup () {
      glEnable (GL_DEPTH_TEST);
      glEnable (GL_LIGHTING);
      glEnable (GL_LIGHT0);
      glShadeModel (GL_SMOOTH);

      setup_lights ();

      std::cout << "Randomizing maps...";
      m_map1->randomize_plasmally (0.95);
      m_map2->randomize_plasmally (0.8);
      std::cout << "done!\n";

      m_mouse.set_pitch_limits (-15, 10);

      m_terrain_renderer.regenerate ();
      m_vehicle.set_speed (30 * one_meter);

      idle ();
    }

    void mouse (int button, int state, int x, int y) {
      if (state != GLUT_UP)
	return;

      m_map1 = m_map2;
      m_map2 = boost::shared_ptr<RandomHeightMap>
	(new RandomHeightMap (resolution, resolution,
			      map_size,
			      ::time (0)));
      m_map2->randomize_plasmally (0.995);
      m_map3.change_maps (m_map1, m_map2);
      m_map3.set_blending_factor (0);
    }

    void idle () {
      static const float dt    = 1 / 30.0;
      //      static const float total = 3;

      if (m_timer.elapsed () >= dt)
	{
// 	  if (!m_map3.done ())
// 	    {
// 	      m_timer.reset ();
// 	      m_map3.increase_blending_factor (dt / total);
	      
// 	      if (m_map3.done ())
// 		m_terrain_renderer.regenerate ();
// 	    }

	  m_vehicle.update (dt);
	  m_camera.update (m_vehicle.position (),
			   m_vehicle.orientation (),
			   dt);
	  m_camera.limit (m_map3);
		
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
      gluPerspective (60.0, 1.0 * width / height, 0.0025, 100.0);
      glutPostRedisplay ();

      check_gl ();

      m_mouse.resize (width, height);
    }

    void passive_motion (int x, int y) {
      m_mouse.update (x, y);
      glutPostRedisplay ();
    }

    void display () {
      glClearColor (fog.x, fog.y, fog.z, 0);
      glColor3f (1, 1, 1);

      glClear (GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
      glMatrixMode (GL_MODELVIEW);

      m_camera.realize ();

      //      if (m_map3.done ())
	m_terrain_renderer.render ();
//       else
// 	m_terrain_renderer.render_directly ();

      m_terrain_renderer.translate ();
      m_vehicle.render ();

      //      m_vehicle.draw_debug_text ();
      //       m_camera.draw_debug_text ();
      //      m_mouse.draw_debug_text ();

      check_gl ();

      glutSwapBuffers ();
    }

  private:
    void setup_lights () {
      GLfloat ambient[] = {0.0, 0.5, 0.0, 1.0};
      glLightModelfv (GL_LIGHT_MODEL_AMBIENT, ambient);

      GLfloat light0_color[] = {0.2, 0.6, 0.0, 1.0};
      GLfloat light0_position[] = {2, 4000 * one_meter, 2};
      glLightfv (GL_LIGHT0, GL_DIFFUSE, light0_color);
      glLightfv (GL_LIGHT0, GL_POSITION, light0_position);

      GLfloat fog_color[] = {fog.x, fog.y, fog.z, 1.0};

      glEnable (GL_FOG);
      glFogi (GL_FOG_MODE, GL_EXP2);
      glFogfv (GL_FOG_COLOR, fog_color);
      glFogf (GL_FOG_DENSITY, 0.4);
      glHint (GL_FOG_HINT, GL_NICEST);
    }

  private:
    gfx::ThirdPersonCamera m_camera;
    gfx::MouseCameraController m_mouse;

    Timer m_timer;
    boost::shared_ptr<map::RandomHeightMap> m_map1;
    boost::shared_ptr<map::RandomHeightMap> m_map2;
    map::InterpolatingHeightMap m_map3;

    gfx::TerrainRenderer m_terrain_renderer;

    mov::Vehicle m_vehicle;
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
