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

  const Vector3D map_size (8000 * one_meter,
			   2000 * one_meter,
			   8000 * one_meter);

  const int resolution = 1025; // Higher resolution for smoother terrain

  // Dynamic fog color will be set based on sky horizon colors
  Vector3D fog (0.5, 0.5, 0.5);

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
	m_sky ("textures/sky.tga"),
        m_vehicle_light_enabled(true)
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
      
      // Enable alpha blending for terrain-sky transitions
      glEnable(GL_BLEND);
      glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
      
      // Enable anti-aliasing for smoother edges
      glEnable(GL_POINT_SMOOTH);
      glEnable(GL_LINE_SMOOTH);
      glEnable(GL_POLYGON_SMOOTH);
      glHint(GL_POINT_SMOOTH_HINT, GL_NICEST);
      glHint(GL_LINE_SMOOTH_HINT, GL_NICEST);
      glHint(GL_POLYGON_SMOOTH_HINT, GL_NICEST);
      
      // Enable anisotropic filtering for better distant texture quality if supported
      float maxAniso = 0.0f;
      glGetFloatv(GL_MAX_TEXTURE_MAX_ANISOTROPY_EXT, &maxAniso);
      if (maxAniso > 0.0f) {
        std::cout << "Anisotropic filtering supported, max value: " << maxAniso << std::endl;
        glTexParameterf(GL_TEXTURE_2D, GL_TEXTURE_MAX_ANISOTROPY_EXT, maxAniso);
      }
      
      // Enable mipmapping for better distance visuals
      glHint(GL_PERSPECTIVE_CORRECTION_HINT, GL_NICEST);
      glHint(GL_GENERATE_MIPMAP_HINT, GL_NICEST);

      setup_lights ();

      std::cout << "Randomizing maps...";
      m_map1.randomize_plasmally (0.9);
      std::cout << "done!\n";

      m_map1.apply_bowl_edge(0.3, 0.05);

      m_mouse.set_pitch_limits (-15, 10);

      m_terrain_renderer.regenerate ();
      m_terrain_renderer.setup_shader ();

      m_sky.load ();

      idle ();
    }

    void mouse (int button, int state, int x, int y) {
    }

    void idle () {
      static const float dt = 1 / 60.0;
      const float elapsed = m_timer.elapsed ();
      static float total_time = 0.0f;
      static float last_shadow_update = 0.0f;

      if (elapsed >= dt)
	{
	  m_timer.reset ();
	  total_time += elapsed;
          
          // Update sky parameters for day/night cycle
          float day_length = 240.0f; // 4 minutes per day for a slower, more enjoyable cycle
//          float day_time = fmod(total_time, day_length) / day_length;

          float day_time = 0.3f;
          
          float sun_height = sin((day_time * 3.14159f * 2.0f) - 3.14159f * 0.5f);
          
          // Make sun height range from 0.0 to 1.0 (night to day)
          sun_height = (sun_height + 1.0f) * 0.5f;
          
          // Update sky parameters
          m_sky.set_time(total_time);
          m_sky.set_sun_height(sun_height);
          
          // Create more interesting cloud patterns:
          // 1. Base cloudiness that changes slowly over time
          float base_cloudiness = sin(total_time * 0.0003f) * 0.4f + 0.5f;
          
          // 2. Add weather front that passes by occasionally
          float weather_front = 0.3f * pow(sin(total_time * 0.0008f), 2.0f);
          
          // 3. Small rapid variations for more dynamic feeling
          float small_variations = sin(total_time * 0.02f) * 0.05f;
          
          // Combine all factors for final cloudiness
          float cloudiness = base_cloudiness + weather_front + small_variations;
          cloudiness = std::max(0.0f, std::min(1.0f, cloudiness)); // Clamp between 0 and 1
          
          m_sky.set_cloudiness(cloudiness);
          
          // Update fog color to match the sky horizon
          Vector3D horizon_color = m_sky.get_horizon_color();
          
          // Update global fog color
          fog = horizon_color;
          
          // Update the terrain shader's fog color
          m_terrain_renderer.set_fog_color(horizon_color);
          
          // Update lighting based on sun position
          update_dynamic_lighting(sun_height, total_time);
          
          // Update spotlight intensity based on time of day
          m_terrain_renderer.set_spotlight_intensity(sun_height);
          
          // Using shadow mapping when available, with fallback to normal-based shadows
          // when framebuffer objects aren't supported
          
          // Calculate sun direction from spherical coordinates
          float sun_elevation = (sun_height - 0.5f) * 3.14159f; // -PI/2 to PI/2
          float sun_angle = total_time * 1.0f; // Rotation around y-axis
          Vector3D light_dir(
              -cos(sun_elevation) * sin(sun_angle),
              -sin(sun_elevation),
              -cos(sun_elevation) * cos(sun_angle)
          );
          light_dir.normalize();
          
          // Adjust shadow strength based on sun height
          // Even stronger shadows for more dramatic effect
          float shadow_strength = 0.7f + sun_height * 0.3f;
          
          // Keep stronger shadows even at sun angles close to horizon
          // This creates dramatic long shadows at sunrise/sunset
          if (sun_height < 0.2f && sun_height > 0.02f) {
              // Enhanced shadows at sunrise/sunset
              shadow_strength = 0.9f;
          } else if (sun_height <= 0.02f) {
              // Fade out shadows at night
              shadow_strength *= sun_height / 0.02f;
          }
          
          // Enable the alternative shadowing technique
          m_terrain_renderer.set_shadow_strength(shadow_strength);
          
          // Pass sun direction to the shader for normal-based shadowing
          m_terrain_renderer.set_light_direction(light_dir);
          
          // Update shadow map using the current sun position
          // only once per second
          if (total_time - last_shadow_update >= 1.0f) {
            m_terrain_renderer.update_shadow_map(light_dir);
            last_shadow_update = total_time;
          }
          
          // Update vehicle spotlight in the terrain shader
          update_vehicle_spotlight();
          
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
      gluPerspective (100.0, 1.0 * width / height, 0.1, 5000);
      glutPostRedisplay ();

      check_gl ();

      m_mouse.resize (width, height);
    }

    void keyboard  (unsigned char code,
		    int mx, int my,
		    KeyStatus status)
    {
      float factor = status == KEY_SPECIAL_PRESSED;
      //std::cout << status << std::endl;

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
  case 'l':
  case 'L':
    // Toggle vehicle light on/off
    if (status == KEY_PRESSED) {
      m_vehicle_light_enabled = !m_vehicle_light_enabled;
      // Update the vehicle's headlight status
      m_vehicle.set_headlight(m_vehicle_light_enabled);
      
      // The actual light will be updated in the next render frame
      // when update_vehicle_spotlight() is called
    }
    break;
	}
    }

    void passive_motion (int x, int y) {
      m_mouse.update (x, y);
      glutPostRedisplay ();
    }

    void render_scene () {
      // Use dynamic fog color for background clear with full alpha
      glClearColor (fog.x, fog.y, fog.z, 1.0);
      glClear (GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);

      glMatrixMode (GL_MODELVIEW);
      m_camera.realize ();
      
      // Make sure the terrain shader has the latest fog color
      m_terrain_renderer.set_fog_color(fog);
      
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
      // Use dynamic fog color for background clear
      glClearColor (fog.x, fog.y, fog.z, 0);

      render_scene ();

      m_vehicle.draw_debug_text ();
      glutSwapBuffers ();
    }

  private:
    void setup_lights() {
      // Enable lighting to make terrain appear more rounded
      glEnable(GL_LIGHTING);
      glEnable(GL_LIGHT0);
      glEnable(GL_LIGHT1); // Second light for fill
      
      // Enable smooth shading for rounded appearance
      glShadeModel(GL_SMOOTH);
      
      // Enable separate specular color for better highlights
      glLightModeli(GL_LIGHT_MODEL_COLOR_CONTROL, GL_SEPARATE_SPECULAR_COLOR);
      
      // Set initial lighting - will be updated dynamically
      // Just calling with default values to initialize the lights
      update_dynamic_lighting(0.5f, 0.0f);
      
      // Initialize vehicle spotlight in shader
      update_vehicle_spotlight();
    }
    
    void update_dynamic_lighting(float sun_height, float time) {
      // Calculate sun position based on height and time
      // Sun follows a circular path, height controls elevation
      float sun_angle = time * 0.01f; // Rotation around y-axis
      
      // Calculate sun direction vector
      float sun_elevation = (sun_height - 0.5f) * 3.14159f; // -PI/2 to PI/2
      
      // Convert spherical to cartesian coordinates
      float x = cos(sun_elevation) * sin(sun_angle);
      float y = sin(sun_elevation);
      float z = cos(sun_elevation) * cos(sun_angle);
      
      // Normalize direction
      float length = sqrt(x*x + y*y + z*z);
      x /= length;
      y /= length;
      z /= length;
      
      // Sun position (directional light)
      GLfloat light0_position[] = {x, y, z, 0.0}; // Directional
      
      // Calculate light colors based on time of day
      // Daylight - bright white/yellow
      Vector3D day_color(1.0f, 0.98f, 0.9f);
      
      // Night - dim blue moonlight
      Vector3D night_color(0.2f, 0.2f, 0.3f);
      
      // Sunset/sunrise - warm orange
      Vector3D sunset_color(1.0f, 0.6f, 0.3f);
      
      // Ambient changes with time of day
      Vector3D ambient_color;
      Vector3D light_color;
      Vector3D specular_color;
      
      // Check if we're in sunset/sunrise range (0.1-0.3 or 0.7-0.9)
      bool is_sunset = (sun_height > 0.1f && sun_height < 0.3f) || 
                       (sun_height > 0.7f && sun_height < 0.9f);
      
      if (is_sunset) {
        // Calculate sunset intensity
        float sunset_intensity;
        if (sun_height < 0.5f) {
          sunset_intensity = 1.0f - fabs(sun_height - 0.2f) / 0.1f;
        } else {
          sunset_intensity = 1.0f - fabs(sun_height - 0.8f) / 0.1f;
        }
        sunset_intensity = std::max(0.0f, std::min(1.0f, sunset_intensity));
        
        // Blend between day/night and sunset colors
        float day_factor = (sun_height < 0.5f) ? sun_height / 0.5f : (1.0f - (sun_height - 0.5f) / 0.5f);
        Vector3D base_color = night_color * (1.0f - day_factor) + day_color * day_factor;
        
        light_color = base_color * (1.0f - sunset_intensity) + sunset_color * sunset_intensity;
        ambient_color = light_color * 0.4f;
        specular_color = Vector3D(1.0f, 0.9f, 0.8f) * (0.7f + sunset_intensity * 0.3f);
      } else {
        // Normal day/night cycle
        light_color = night_color * (1.0f - sun_height) + day_color * sun_height;
        
        // Ambient is stronger during day, weaker at night
        float ambient_strength = 0.25f + sun_height * 0.35f;
        ambient_color = light_color * ambient_strength;
        
        // Specular is stronger during day
        float spec_strength = 0.4f + sun_height * 0.6f;
        specular_color = Vector3D(0.8f, 0.8f, 0.9f) * spec_strength;
      }
      
      // // Apply ambient light
      // GLfloat ambient[4] = {ambient_color.x, ambient_color.y, ambient_color.z, 1.0f};
      // glLightModelfv(GL_LIGHT_MODEL_AMBIENT, ambient);
      
      // Apply main directional light (sun/moon)
      GLfloat light0_diffuse[4] = {light_color.x, light_color.y, light_color.z, 1.0f};
      GLfloat light0_specular[4] = {specular_color.x, specular_color.y, specular_color.z, 1.0f};
      
      glLightfv(GL_LIGHT0, GL_POSITION, light0_position);
      glLightfv(GL_LIGHT0, GL_DIFFUSE, light0_diffuse);
      glLightfv(GL_LIGHT0, GL_SPECULAR, light0_specular);
      
      // // Add a fill light from the opposite direction (sky light)
      // // Fill light position - opposite of main light but always above horizon
      // GLfloat light1_position[4] = {-x, std::max(0.2f, -y), -z, 0.0f};
      
      // // Fill light is always blueish sky color, but intensity changes with sun height
      // Vector3D fill_color = Vector3D(0.5f, 0.6f, 0.9f) * std::max(0.2f, sun_height * 0.4f);
      // GLfloat light1_diffuse[4] = {fill_color.x, fill_color.y, fill_color.z, 1.0f};
      // GLfloat light1_specular[4] = {0.0f, 0.0f, 0.0f, 1.0f}; // No specular for fill light
      
      // glLightfv(GL_LIGHT1, GL_POSITION, light1_position);
      // glLightfv(GL_LIGHT1, GL_DIFFUSE, light1_diffuse);
      // glLightfv(GL_LIGHT1, GL_SPECULAR, light1_specular);
      
      // Set terrain material properties for better interaction with our lighting
      GLfloat mat_diffuse[4] = {0.9f, 0.9f, 0.9f, 1.0f};
      GLfloat mat_specular[4] = {0.5f, 0.5f, 0.5f, 1.0f};
      GLfloat mat_shininess[1] = {64.0f};
      
      glMaterialfv(GL_FRONT, GL_AMBIENT_AND_DIFFUSE, mat_diffuse);
      glMaterialfv(GL_FRONT, GL_SPECULAR, mat_specular);
      glMaterialfv(GL_FRONT, GL_SHININESS, mat_shininess);
    }

    void update_vehicle_spotlight() {
      // Get vehicle position and orientation
      Vector3D vehicle_pos = m_vehicle.position();
      Vector3D vehicle_dir = m_vehicle.orientation();
      
      // Position spotlight slightly above and in front of the vehicle
      Vector3D light_pos = vehicle_pos + Vector3D(0, 1 * one_meter, 0) + vehicle_dir * 1 * one_meter;
      
      // Reverse the direction for the shader
      Vector3D reversed_dir = -vehicle_dir;
      
      // Update the terrain shader with the vehicle spotlight parameters
      m_terrain_renderer.set_vehicle_spotlight(
        m_vehicle_light_enabled,
        light_pos,
        reversed_dir
      );
    }

  private:
    gfx::ThirdPersonCamera m_camera;
    gfx::MouseCameraController m_mouse;

    Timer m_timer;
    map::RandomHeightMap m_map1;

    gfx::TerrainRenderer m_terrain_renderer;
    mov::Vehicle m_vehicle;
    gfx::Sky m_sky;
    bool m_vehicle_light_enabled;
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
