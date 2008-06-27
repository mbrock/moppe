
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

  const Vector3D map_size (2000 * one_meter,
			   600 * one_meter,
			   2000 * one_meter);

  const int resolution = 129;

  const Vector3D fog (0.5, 0.5, 0.5);

  class MoppeGLUT: public GLUTApplication {
  public:
    MoppeGLUT ()
      : GLUTApplication ("Moppe", 1027, 768),
	m_camera (80, 5 * one_meter),
	m_mouse (800, 600),
	m_map1 (resolution, resolution,
		map_size,
		0 + ::time (0)),
// 	m_map2 (new RandomHeightMap (resolution, resolution,
// 				     map_size,
// 				     1 + ::time (0))),
// 	m_map3 (m_map1, m_map2, m_map1->size ()),
	m_terrain_renderer (m_map1),
	m_vehicle (Vector3D (50 * one_meter, 600 * one_meter,
			     50 * one_meter), 45, m_map1,
		   2500, 150),
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
      m_map1.randomize_plasmally (1.0);
      //      m_map2->randomize_plasmally (0.8);
      std::cout << "done!\n";

      m_mouse.set_pitch_limits (-15, 10);

      m_terrain_renderer.regenerate ();
      m_terrain_renderer.setup_shader ();

      m_sky.load ();

      idle ();
    }

    void mouse (int button, int state, int x, int y) {
      if (state != GLUT_UP)
	return;

//       m_map1 = m_map2;
//       m_map2 = boost::shared_ptr<RandomHeightMap>
// 	(new RandomHeightMap (resolution, resolution,
// 			      map_size,
// 			      ::time (0)));
//       m_map2->randomize_plasmally (0.995);
//       m_map3.change_maps (m_map1, m_map2);
//       m_map3.set_blending_factor (0);
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
      gluPerspective (90.0, 1.0 * width / height, 0.1, 10000 * one_meter);
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
	  m_vehicle.set_yaw (-45 * factor);
	  break;

	case GLUT_KEY_RIGHT:
	  m_vehicle.set_yaw (45 * factor);
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

    void render_scene (float x) {
      glClearColor (fog.x, fog.y, fog.z, 0);
      glClear (GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);

      glMatrixMode (GL_MODELVIEW);
      m_camera.realize ();
      
      Vector3D displacement (m_vehicle.velocity () * x / 20);
      gl::translate (displacement);

      {
	gl::ScopedMatrixSaver matrix;
	gl::translate (m_camera.position ());
	m_sky.render ();
      }
      
      m_terrain_renderer.render ();
      m_terrain_renderer.translate ();
      m_vehicle.render ();      
    }

    void display () {
      glClearColor (fog.x, fog.y, fog.z, 0);
      //      glClear (GL_ACCUM_BUFFER_BIT);

      int passes = 1;
      for (int i = 0; i < passes; ++i)
	{
	  render_scene (0.0);
	  //	  glAccum (GL_ACCUM, 1.0 / passes);
	}

      //      glAccum (GL_RETURN, 1.0);
      
      m_vehicle.draw_debug_text ();

      glutSwapBuffers ();
    }

  private:
    void setup_lights () {
      GLfloat ambient[] = {0.5, 0.5, 0.5, 1.0};
      glLightModelfv (GL_LIGHT_MODEL_AMBIENT, ambient);

      GLfloat light0_color[] = {1.0, 1.0, 1.0, 1.0};
      GLfloat light0_specular[] = {1.0, 1.0, 0.0, 1.0};
      GLfloat light0_position[] = {2, 40, 2};
      glLightfv (GL_LIGHT0, GL_DIFFUSE, light0_color);
      glLightfv (GL_LIGHT0, GL_SPECULAR, light0_specular);
      glLightfv (GL_LIGHT0, GL_POSITION, light0_position);

      GLfloat fog_color[] = {fog.x, fog.y, fog.z, 1.0};

      glEnable (GL_FOG);
      glFogi (GL_FOG_MODE, GL_EXP2);
      glFogfv (GL_FOG_COLOR, fog_color);
      glFogf (GL_FOG_DENSITY, 0.00001);
      glHint (GL_FOG_HINT, GL_NICEST);
    }

  private:
    gfx::ThirdPersonCamera m_camera;
    gfx::MouseCameraController m_mouse;

    Timer m_timer;
    map::RandomHeightMap m_map1;
//     boost::shared_ptr<map::RandomHeightMap> m_map2;
//     map::InterpolatingHeightMap m_map3;

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
