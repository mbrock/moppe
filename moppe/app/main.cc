
#include <moppe/app/app.hh>
#include <moppe/gfx/camera.hh>
#include <moppe/gfx/mouse.hh>
#include <moppe/map/generate.hh>
#include <moppe/gfx/terrain.hh>
#include <moppe/gfx/sky.hh>
#include <moppe/mov/vehicle.hh>

#include <iostream>

#include <ctime>

namespace moppe {
  using namespace map;
  using namespace app;

  const Vector3D map_size (4000 * one_meter,
			   900 * one_meter,
			   4000 * one_meter);

  const int resolution = 257;

  const Vector3D fog (0.5, 0.5, 0.5);

  class MoppeGLUT: public GLUTApplication {
  public:
    MoppeGLUT ()
      : GLUTApplication ("Moppe", 1000, 768),
	m_camera (80, 5 * one_meter),
	m_mouse (800, 600),
	m_map1 (resolution, resolution,
		map_size,
		0 + ::time (0)),
	m_terrain_renderer (m_map1),
	m_vehicle (Vector3D (50 * one_meter, 600 * one_meter,
			     50 * one_meter), 45, m_map1,
		   5000, 150),
	m_sky ("textures/sky.tga")
    { }

    void setup () {
#ifndef __APPLE__
      if (GLEW_ARB_vertex_shader && GLEW_ARB_fragment_shader)
	std::cout << "ARB Shader support found!\n";
      else
	std::cout << "No ARB shader support found.\n";
#endif

      glEnable (GL_DEPTH_TEST);
      glEnable (GL_CULL_FACE);
      glCullFace (GL_BACK);
      glShadeModel (GL_SMOOTH);
      glEnable (GL_TEXTURE_2D);

      setup_lights ();

      std::cout << "Randomizing maps...";
      m_map1.randomize_plasmally (0.8);
      std::cout << "done!\n";

      m_mouse.set_pitch_limits (-15, 10);

      m_terrain_renderer.regenerate ();
      m_terrain_renderer.setup_shader ();

      m_sky.load ();

      idle ();
    }

    void mouse (int button, int state, int x, int y) {
    }

    void idle () {
      static const float dt = 1 / 30.0;
      const float elapsed = m_timer.elapsed ();

      if (elapsed >= dt)
	{
	  m_timer.reset ();
	  m_vehicle.update (elapsed);
	  m_camera.update (m_vehicle.position (),
			   m_vehicle.orientation (),
			   elapsed);
	  m_camera.limit (m_map1);
		
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
      gluPerspective (100.0, 1.0 * width / height, 0.1, 800);
      glutPostRedisplay ();

      check_gl ();

      m_mouse.resize (width, height);
    }

    void keyboard  (unsigned char code,
		    int mx, int my,
		    KeyStatus status)
    {
      float factor = status == KEY_SPECIAL_PRESSED;
      std::cout << status << std::endl;

      switch (code)
	{
	case GLUT_KEY_LEFT:
	  m_vehicle.set_yaw (-90 * factor);
	  break;

	case GLUT_KEY_RIGHT:
	  m_vehicle.set_yaw (90 * factor);
	  break;

	case GLUT_KEY_UP:
	  m_vehicle.set_thrust (1 * factor);
	  break;

	case GLUT_KEY_DOWN:
	  m_vehicle.set_thrust (-1 * factor);
	  break;
	}
    }

    void passive_motion (int x, int y) {
      m_mouse.update (x, y);
      glutPostRedisplay ();
    }

    void render_scene () {
      glClearColor (fog.x, fog.y, fog.z, 0);
      glClear (GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);

      glMatrixMode (GL_MODELVIEW);
      m_camera.realize ();
      
//       {
// 	gl::ScopedMatrixSaver matrix;
// 	gl::translate (m_camera.position ());
// 	//	m_sky.render ();
//       }
      
      m_terrain_renderer.render ();
      m_terrain_renderer.translate ();
      m_vehicle.render ();      
    }

    void display () {
      glClearColor (fog.x, fog.y, fog.z, 0);

      render_scene ();

      m_vehicle.draw_debug_text ();
      glutSwapBuffers ();
    }

  private:
    void setup_lights () {
      GLfloat ambient[] = {0.5, 0.5, 0.5, 1.0};
      glLightModelfv (GL_LIGHT_MODEL_AMBIENT, ambient);

      GLfloat light0_color[] = {0.8, 0.8, 0.8, 1.0};
      GLfloat light0_specular[] = {1.0, 1.0, 0.0, 1.0};
      GLfloat light0_position[] = {2, 40, 2};
      glLightfv (GL_LIGHT0, GL_DIFFUSE, light0_color);
      glLightfv (GL_LIGHT0, GL_SPECULAR, light0_specular);
      glLightfv (GL_LIGHT0, GL_POSITION, light0_position);
    }

  private:
    gfx::ThirdPersonCamera m_camera;
    gfx::MouseCameraController m_mouse;

    Timer m_timer;
    map::RandomHeightMap m_map1;

    gfx::TerrainRenderer m_terrain_renderer;
    mov::Vehicle m_vehicle;
    gfx::Sky m_sky;
  };
}

int
main (int argc, char **argv)
{
  using namespace moppe;

  MoppeGLUT app;
  app::global_app = &app;

  app.initialize (argc, argv, 
		  GLUT_RGBA | GLUT_DOUBLE | GLUT_DEPTH);

  try { app.run_main_loop (); }
  catch (const std::exception& e)
    {
      std::cerr << "\nError: " << e.what () << "\n";
      std::exit (-1);
    }
}
