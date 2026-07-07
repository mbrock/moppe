
#include <moppe/app/app.hh>
#include <moppe/gfx/camera.hh>
#include <moppe/gfx/mouse.hh>
#include <moppe/map/generate.hh>
#include <moppe/gfx/terrain.hh>
#include <moppe/gfx/sky.hh>
#include <moppe/gfx/ocean.hh>
#include <moppe/mov/vehicle.hh>

#include <algorithm>
#include <cmath>
#include <iostream>
#include <ctime>
#include <random>

namespace moppe
{
  using namespace map;
  using namespace app;

  const Vector3D map_size(5000 * one_meter,
                          650 * one_meter,
                          5000 * one_meter);

  const int resolution = 2049; // Higher resolution for smoother terrain

  // Sea level: valleys below this are flooded
  const float water_level = 50 * one_meter;

  // Dynamic fog color will be set based on sky horizon colors
  Vector3D fog(0.5, 0.5, 0.5);

  // Simple scattered trees and bushes, compiled into one display
  // list so the whole forest is a single draw call.
  class Vegetation
  {
  public:
    Vegetation() : m_list(0) {}

    void generate(const map::HeightMap& map, int trees, int bushes,
                  unsigned seed)
    {
      std::mt19937 rng(seed);
      std::uniform_real_distribution<float> u(0.0f, 1.0f);

      const Vector3D size = map.size();

      if (m_list == 0)
        m_list = glGenLists(1);

      glNewList(m_list, GL_COMPILE);

      int placed = 0;
      for (int i = 0; i < trees * 20 && placed < trees; ++i)
      {
        float x = size.x * (0.02f + 0.96f * u(rng));
        float z = size.z * (0.02f + 0.96f * u(rng));
        float y = map.interpolated_height(x, z);

        // Trees like gentle grassy ground below the rock line,
        // and nothing grows in the ocean
        if (y > 0.32f * map_size.y) continue;
        if (y < water_level + 5) continue;
        if (map.interpolated_normal(x, z).y < 0.8f) continue;

        draw_tree(x, y, z, 0.8f + 1.0f * u(rng), u(rng));
        ++placed;
      }

      placed = 0;
      for (int i = 0; i < bushes * 20 && placed < bushes; ++i)
      {
        float x = size.x * (0.02f + 0.96f * u(rng));
        float z = size.z * (0.02f + 0.96f * u(rng));
        float y = map.interpolated_height(x, z);

        // Bushes climb a little higher and tolerate more slope
        if (y > 0.45f * map_size.y) continue;
        if (y < water_level + 5) continue;
        if (map.interpolated_normal(x, z).y < 0.72f) continue;

        draw_bush(x, y, z, 0.6f + 0.8f * u(rng), u(rng));
        ++placed;
      }

      glEndList();
    }

    void render(const Vector3D& fog_color) const
    {
      if (m_list == 0)
        return;

      gl::ScopedAttribSaver attribs(GL_ENABLE_BIT | GL_LIGHTING_BIT |
                                    GL_CURRENT_BIT | GL_FOG_BIT |
                                    GL_TEXTURE_BIT);

      // Fixed-function rendering: textures off, colors as materials
      for (int unit = 3; unit >= 0; --unit)
      {
        glActiveTexture(GL_TEXTURE0 + unit);
        glDisable(GL_TEXTURE_2D);
      }
      glEnable(GL_COLOR_MATERIAL);
      glColorMaterial(GL_FRONT_AND_BACK, GL_AMBIENT_AND_DIFFUSE);

      // Approximate the terrain shader's haze so distant trees fade
      // into the horizon instead of popping out of the fog
      GLfloat fc[4] = {fog_color.x, fog_color.y, fog_color.z, 1.0f};
      glEnable(GL_FOG);
      glFogi(GL_FOG_MODE, GL_EXP2);
      glFogf(GL_FOG_DENSITY, 0.0005f);
      glFogfv(GL_FOG_COLOR, fc);

      glCallList(m_list);
    }

  private:
    static void draw_tree(float x, float y, float z, float s,
                          float tint)
    {
      glPushMatrix();
      glTranslatef(x, y, z);
      glScalef(s, s, s);

      glColor3f(0.42f, 0.28f, 0.14f);
      glPushMatrix();
      glTranslatef(0, 1.2f, 0);
      glScalef(0.5f, 2.4f, 0.5f);
      glutSolidCube(1.0);
      glPopMatrix();

      glColor3f(0.08f + 0.1f * tint, 0.35f + 0.15f * tint, 0.1f);
      glPushMatrix();
      glTranslatef(0, 2.0f, 0);
      glRotatef(-90, 1, 0, 0);
      glutSolidCone(2.2, 5.5, 8, 2);
      glPopMatrix();

      glPopMatrix();
    }

    static void draw_bush(float x, float y, float z, float s,
                          float tint)
    {
      glPushMatrix();
      glTranslatef(x, y + 0.3f * s, z);
      glColor3f(0.1f + 0.12f * tint, 0.3f + 0.18f * tint, 0.08f);
      glScalef(1.4f * s, 0.9f * s, 1.4f * s);
      glutSolidSphere(1.0, 8, 6);
      glPopMatrix();
    }

    GLuint m_list;
  };

  // Spinning golden pickup stars scattered over the terrain; some
  // hover high enough that only a rocket jump reaches them.
  class Stars
  {
  public:
    Stars() : m_collected(0) {}

    void generate(const map::HeightMap& map, int count, unsigned seed)
    {
      std::mt19937 rng(seed);
      std::uniform_real_distribution<float> u(0.0f, 1.0f);
      const Vector3D size = map.size();

      m_stars.clear();
      while ((int)m_stars.size() < count)
      {
        Star s;
        s.pos.x = size.x * (0.03f + 0.94f * u(rng));
        s.pos.z = size.z * (0.03f + 0.94f * u(rng));

        float ground = map.interpolated_height(s.pos.x, s.pos.z);
        if (ground < water_level + 2) continue; // land only

        // Every fourth star hangs high up: rocket-jump territory
        bool high = (m_stars.size() % 4 == 0);
        s.pos.y = ground + (high ? 14.0f + 8.0f * u(rng) : 2.5f);
        s.phase = 360.0f * u(rng);
        s.respawn = 0;
        m_stars.push_back(s);
      }
    }

    // Checks pickups; returns how many were grabbed this tick
    int update(const Vector3D& vehicle_pos, float dt)
    {
      int picked = 0;
      for (size_t i = 0; i < m_stars.size(); ++i)
      {
        Star& s = m_stars[i];
        if (s.respawn > 0)
        {
          s.respawn -= dt;
          continue;
        }
        if ((s.pos - vehicle_pos).length2() < 4.5f * 4.5f)
        {
          s.respawn = 60.0f; // comes back later
          ++m_collected;
          ++picked;
          m_last_pos = s.pos;
        }
      }
      return picked;
    }

    int collected() const { return m_collected; }
    Vector3D last_pickup() const { return m_last_pos; }

    void render(float time) const
    {
      gl::ScopedAttribSaver attribs(GL_ENABLE_BIT | GL_CURRENT_BIT |
                                    GL_TEXTURE_BIT);
      for (int unit = 3; unit >= 0; --unit)
      {
        glActiveTexture(GL_TEXTURE0 + unit);
        glDisable(GL_TEXTURE_2D);
      }
      glDisable(GL_LIGHTING);

      for (size_t i = 0; i < m_stars.size(); ++i)
      {
        const Star& s = m_stars[i];
        if (s.respawn > 0)
          continue;

        gl::ScopedMatrixSaver m;
        glTranslatef(s.pos.x,
                     s.pos.y + 0.35f * sin(time * 2.0f + s.phase),
                     s.pos.z);
        glRotatef(time * 150.0f + s.phase, 0, 1, 0);
        glColor3f(1.0f, 0.85f, 0.15f);
        glutSolidTorus(0.16, 0.75, 10, 18);
        glColor3f(1.0f, 0.95f, 0.6f);
        glutSolidSphere(0.3, 8, 8);
      }
    }

  private:
    struct Star
    {
      Vector3D pos;
      float phase;
      float respawn;
    };

    std::vector<Star> m_stars;
    int m_collected;
    Vector3D m_last_pos;
  };

  // Short-lived billboard puffs: drift dust, landing dirt, water
  // spray, and star-pickup sparkles.
  class Dust
  {
  public:
    Dust() : m_rng(99) {}

    void emit(const Vector3D& pos, const Vector3D& vel, int count,
              const Vector3D& color)
    {
      std::uniform_real_distribution<float> u(-1.0f, 1.0f);
      for (int i = 0; i < count && m_particles.size() < 500; ++i)
      {
        Particle p;
        p.pos = pos + Vector3D(u(m_rng), 0.4f * u(m_rng), u(m_rng)) * 0.7f;
        p.vel = vel + Vector3D(3.0f * u(m_rng),
                               2.5f + 2.0f * u(m_rng),
                               3.0f * u(m_rng));
        p.max_life = 0.45f + 0.35f * (0.5f + 0.5f * u(m_rng));
        p.life = p.max_life;
        p.size = 0.5f + 0.3f * u(m_rng);
        p.color = color;
        m_particles.push_back(p);
      }
    }

    void update(float dt)
    {
      size_t j = 0;
      for (size_t i = 0; i < m_particles.size(); ++i)
      {
        Particle p = m_particles[i];
        p.life -= dt;
        if (p.life <= 0)
          continue;
        p.vel *= std::exp(-2.0f * dt); // air drag
        p.pos += p.vel * dt;
        m_particles[j++] = p;
      }
      m_particles.resize(j);
    }

    void render() const
    {
      if (m_particles.empty())
        return;

      // Billboard axes straight from the current view matrix
      float mv[16];
      glGetFloatv(GL_MODELVIEW_MATRIX, mv);
      const Vector3D right(mv[0], mv[4], mv[8]);
      const Vector3D up(mv[1], mv[5], mv[9]);

      gl::ScopedAttribSaver attribs(GL_ENABLE_BIT | GL_CURRENT_BIT |
                                    GL_TEXTURE_BIT |
                                    GL_DEPTH_BUFFER_BIT);
      for (int unit = 3; unit >= 0; --unit)
      {
        glActiveTexture(GL_TEXTURE0 + unit);
        glDisable(GL_TEXTURE_2D);
      }
      glDisable(GL_LIGHTING);
      glDepthMask(GL_FALSE); // soft puffs, no z-fighting each other

      glBegin(GL_QUADS);
      for (size_t i = 0; i < m_particles.size(); ++i)
      {
        const Particle& p = m_particles[i];
        const float a = p.life / p.max_life;
        const float s = p.size * (1.6f - 0.6f * a); // grow as it fades

        glColor4f(p.color.x, p.color.y, p.color.z, 0.55f * a);
        gl::vertex(p.pos - right * s - up * s);
        gl::vertex(p.pos + right * s - up * s);
        gl::vertex(p.pos + right * s + up * s);
        gl::vertex(p.pos - right * s + up * s);
      }
      glEnd();
    }

  private:
    struct Particle
    {
      Vector3D pos, vel, color;
      float life, max_life, size;
    };

    std::vector<Particle> m_particles;
    std::mt19937 m_rng;
  };

  // Schools of little fish circling in the deeper water.
  class Fish
  {
  public:
    void generate(const map::HeightMap& map, float water,
                  int schools, unsigned seed)
    {
      std::mt19937 rng(seed);
      std::uniform_real_distribution<float> u(0.0f, 1.0f);
      const Vector3D size = map.size();

      m_fish.clear();
      int made = 0, tries = 0;
      while (made < schools && tries++ < schools * 300)
      {
        const float ax = size.x * (0.05f + 0.9f * u(rng));
        const float az = size.z * (0.05f + 0.9f * u(rng));
        const float ground = map.interpolated_height(ax, az);
        if (ground > water - 6.0f)
          continue; // schools want reasonably deep water

        const int n = 8 + (int)(u(rng) * 5);
        for (int i = 0; i < n; ++i)
        {
          One f;
          f.cx = ax + 12.0f * (u(rng) - 0.5f);
          f.cz = az + 12.0f * (u(rng) - 0.5f);
          f.y = ground + 1.5f
              + (water - 3.0f - ground - 1.5f) * u(rng);
          f.radius = 3.0f + 6.0f * u(rng);
          f.speed = 0.5f + 0.7f * u(rng);
          f.phase = 6.2832f * u(rng);
          f.size = 0.6f + 0.8f * u(rng);
          f.hue = u(rng);
          m_fish.push_back(f);
        }
        ++made;
      }
    }

    void render(float time) const
    {
      if (m_fish.empty())
        return;

      gl::ScopedAttribSaver attribs(GL_ENABLE_BIT | GL_LIGHTING_BIT |
                                    GL_CURRENT_BIT | GL_TEXTURE_BIT);
      for (int unit = 3; unit >= 0; --unit)
      {
        glActiveTexture(GL_TEXTURE0 + unit);
        glDisable(GL_TEXTURE_2D);
      }
      glEnable(GL_COLOR_MATERIAL);
      glColorMaterial(GL_FRONT_AND_BACK, GL_AMBIENT_AND_DIFFUSE);

      for (size_t i = 0; i < m_fish.size(); ++i)
      {
        const One& f = m_fish[i];
        const float a = f.phase + time * f.speed;

        gl::ScopedMatrixSaver m;
        glTranslatef(f.cx + cos(a) * f.radius,
                     f.y + 0.3f * sin(time * 1.3f + f.phase * 3.0f),
                     f.cz + sin(a) * f.radius);
        // face along the swim circle's tangent
        glRotatef(-a * 57.2958f, 0, 1, 0);
        glScalef(f.size, f.size, f.size);

        // body: orange to silvery blue depending on the fish
        glColor3f(1.0f - 0.3f * f.hue,
                  0.5f + 0.3f * f.hue,
                  0.15f + 0.75f * f.hue);
        glPushMatrix();
        glScalef(0.28f, 0.22f, 0.6f);
        glutSolidSphere(1.0, 8, 6);
        glPopMatrix();

        // wiggling tail fin
        glPushMatrix();
        glTranslatef(0, 0, -0.55f);
        glRotatef(30.0f * sin(time * 6.0f + f.phase), 0, 1, 0);
        glRotatef(180, 0, 1, 0);
        glutSolidCone(0.16, 0.45, 6, 2);
        glPopMatrix();
      }
    }

  private:
    struct One
    {
      float cx, cz, y, radius, speed, phase, size, hue;
    };

    std::vector<One> m_fish;
  };

  class MoppeGLUT : public GLUTApplication
  {
  public:
    MoppeGLUT()
        : GLUTApplication("Moppe", 1000, 768),
          m_camera(80, 5 * one_meter),
          m_mouse(800, 600),
          m_map1(resolution, resolution,
                 map_size,
                 0 + ::time(0)),
          m_terrain_renderer(m_map1),
          m_vehicle(Vector3D(50 * one_meter, 600 * one_meter,
                             50 * one_meter),
                    45, m_map1,
                    5000, 150),
          m_sky("textures/sky.tga"),
          m_ocean(water_level,
                  Vector3D(map_size.x / 2, 0, map_size.z / 2),
                  5500 * one_meter),
          m_uw_vert(GL_VERTEX_SHADER_ARB, "shaders/underwater.vert"),
          m_uw_frag(GL_FRAGMENT_SHADER_ARB, "shaders/underwater.frag"),
          m_last_shadow_update(-1.0f),
          m_total_time(0.0f),
          m_blur_tex(0),
          m_blur_valid(false),
          m_shake(0.0f),
          m_fx_rng(7)
    {
    }

    void setup()
    {
#ifndef __APPLE__
      if (GLEW_ARB_vertex_shader && GLEW_ARB_fragment_shader)
        std::cout << "ARB Shader support found!\n";
      else
        std::cout << "No ARB shader support found.\n";
#endif

      glEnable(GL_DEPTH_TEST);
      glEnable(GL_CULL_FACE);
      glCullFace(GL_BACK);
      glShadeModel(GL_SMOOTH);
      glEnable(GL_TEXTURE_2D);

      // Enable alpha blending for terrain-sky transitions
      glEnable(GL_BLEND);
      glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);

      // Anti-alias lines (vehicle markers); polygon/point smoothing
      // hits slow driver paths and is handled better by MSAA anyway
      glEnable(GL_LINE_SMOOTH);
      glHint(GL_LINE_SMOOTH_HINT, GL_NICEST);

      // Enable anisotropic filtering for better distant texture quality if supported
      float maxAniso = 0.0f;
      glGetFloatv(GL_MAX_TEXTURE_MAX_ANISOTROPY_EXT, &maxAniso);
      if (maxAniso > 0.0f)
      {
        std::cout << "Anisotropic filtering supported, max value: " << maxAniso << std::endl;
        glTexParameterf(GL_TEXTURE_2D, GL_TEXTURE_MAX_ANISOTROPY_EXT, maxAniso);
      }

      // Enable mipmapping for better distance visuals
      glHint(GL_PERSPECTIVE_CORRECTION_HINT, GL_NICEST);
      glHint(GL_GENERATE_MIPMAP_HINT, GL_NICEST);

      setup_lights();

      std::cout << "Generating terrain..." << std::flush;
      m_map1.randomize_geologically();
      // Slight lowland squash; ~10-15% of the map ends up as ocean
      m_map1.exponentiate(1.15);
      std::cout << " eroding..." << std::flush;
      m_map1.erode_hydraulically(1500000);
      // Talus angle ~40 degrees at 2.4m cells and 650m height scale
      m_map1.erode_thermally(2, 0.003f);
      m_map1.recompute_normals();
      std::cout << " done!\n";

      m_mouse.set_pitch_limits(-15, 10);

      m_terrain_renderer.regenerate();
      m_terrain_renderer.setup_shader();

      std::cout << "Planting vegetation...";
      m_vegetation.generate(m_map1, 2200, 1500, 1234);
      std::cout << "done!\n";

      m_star_field.generate(m_map1, 80, 555);

      m_sky.load();
      m_ocean.load();
      m_vehicle.set_water_level(water_level);

      m_fish_school.generate(m_map1, water_level, 16, 777);

      m_uw_vert.load();
      m_uw_frag.load();
      m_uw_prog.load();
      m_uw_prog.attach(m_uw_vert);
      m_uw_prog.attach(m_uw_frag);
      m_uw_prog.link();
      m_uw_prog.print_log();

      idle();
    }

    void mouse(int button, int state, int x, int y)
    {
    }

    void idle()
    {
      static const float dt = 1 / 60.0;
      const float elapsed = m_timer.elapsed();

      if (elapsed >= dt)
      {
        m_timer.reset();
        m_total_time += elapsed;
        const float total_time = m_total_time;

        // Eternal daylight: the sun stays high and fixed
        static const float sun_height = 0.92f;

        m_sky.set_time(total_time); // keeps the clouds drifting
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

        // Fog color: sky horizon shifted toward a milky pale blue
        // for a softer, hazier atmosphere
        Vector3D horizon = m_sky.get_horizon_color();
        fog = horizon * 0.75f + Vector3D(0.93, 0.95, 1.0) * 0.25f;
        m_terrain_renderer.set_fog_color(fog);

        // The GL light position must be re-specified each tick under
        // the current modelview so the sun stays fixed in the world
        update_dynamic_lighting(sun_height, 80.0f);

        // The sun never moves, so one shadow-map render lasts forever
        if (m_last_shadow_update < 0.0f)
        {
          const float sun_elevation = (sun_height - 0.5f) * 3.14159f;
          const float sun_angle = 0.8f; // matches update_dynamic_lighting
          Vector3D light_dir(
              -cos(sun_elevation) * sin(sun_angle),
              -sin(sun_elevation),
              -cos(sun_elevation) * cos(sun_angle));
          light_dir.normalize();

          m_terrain_renderer.set_shadow_strength(0.85f);
          m_terrain_renderer.update_shadow_map(light_dir);
          m_last_shadow_update = total_time;
        }

        m_vehicle.update(elapsed);

        // -- gameplay feedback: dust, spray, stars, camera shake
        const Vector3D vpos = m_vehicle.position();
        const bool in_water = vpos.y < water_level + 1.0f;
        const Vector3D dust_color(0.72, 0.63, 0.48);
        const Vector3D spray_color(0.85, 0.92, 1.0);

        // Drift kicks up dirt from the rear wheel (or spray)
        if (m_vehicle.grounded() && m_vehicle.drift_speed() > 6.0f)
        {
          Vector3D back = vpos - m_vehicle.orientation() * 1.4f;
          back.y = vpos.y - 0.7f;
          int n = std::min(6, (int)(m_vehicle.drift_speed() * 0.25f));
          m_dust.emit(back, m_vehicle.velocity() * 0.15f, n,
                      in_water ? spray_color : dust_color);
        }

        // Wading fast throws up a bow wave
        if (in_water && m_vehicle.velocity().length() > 15.0f)
          m_dust.emit(vpos + Vector3D(0, -0.5, 0),
                      m_vehicle.velocity() * 0.3f, 3, spray_color);

        // Hard landings shake the camera and burst dirt outward
        const float impact = m_vehicle.pop_impact();
        if (impact > 8.0f)
        {
          m_shake = std::min(1.2f, 0.05f * impact);
          m_dust.emit(vpos + Vector3D(0, -0.7, 0),
                      m_vehicle.velocity() * 0.2f, 18,
                      in_water ? spray_color : dust_color);
        }

        // Star pickups sparkle gold
        if (m_star_field.update(vpos, elapsed) > 0)
          m_dust.emit(m_star_field.last_pickup(),
                      Vector3D(0, 3, 0), 16,
                      Vector3D(1.0, 0.85, 0.2));

        m_dust.update(elapsed);
        m_shake *= std::exp(-4.0f * elapsed);

        m_camera.update(m_vehicle.position(),
                        m_vehicle.orientation(),
                        elapsed);
        m_camera.limit(m_map1);

        glutPostRedisplay();
      }
    }

    void reshape(int width, int height)
    {
      gl::global_config.screen_width = width;
      gl::global_config.screen_height = height;

      m_width = width;
      m_height = height;

      glMatrixMode(GL_PROJECTION);
      glLoadIdentity();

      glViewport(0, 0, width, height);
      gluPerspective(100.0, 1.0 * width / height, 0.5, 9000);
      glutPostRedisplay();

      allocate_blur_texture();

      check_gl();

      m_mouse.resize(width, height);
    }

    // (Re)create the screen-sized texture that holds the previous
    // frame for the motion-blur feedback effect
    void allocate_blur_texture()
    {
      if (m_blur_tex == 0)
        glGenTextures(1, &m_blur_tex);

      glActiveTexture(GL_TEXTURE0);
      glBindTexture(GL_TEXTURE_2D, m_blur_tex);
      glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
      glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
      glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
      glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
      glTexImage2D(GL_TEXTURE_2D, 0, GL_RGB8, m_width, m_height, 0,
                   GL_RGB, GL_UNSIGNED_BYTE, NULL);
      m_blur_valid = false;
    }

    // Radial speed blur. Plain frame-ghosting is invisible with a
    // chase camera (consecutive frames are nearly identical), so
    // instead the previous frame is layered back ZOOMED outward
    // from the screen center; the feedback loop turns that into
    // radial streaks that read as speed.
    void apply_motion_blur()
    {
      if (m_blur_tex == 0)
        return;

      const float kmh = m_vehicle.velocity().length() * 3.6f;
      float k = (kmh - 90.0f) / 160.0f; // fades in 90, full at 250
      k = std::max(0.0f, std::min(1.0f, k));

      gl::ScopedAttribSaver attribs(GL_ENABLE_BIT | GL_CURRENT_BIT |
                                    GL_TEXTURE_BIT |
                                    GL_DEPTH_BUFFER_BIT);

      for (int unit = 3; unit >= 1; --unit)
      {
        glActiveTexture(GL_TEXTURE0 + unit);
        glDisable(GL_TEXTURE_2D);
      }
      glActiveTexture(GL_TEXTURE0);
      glBindTexture(GL_TEXTURE_2D, m_blur_tex);

      if (m_blur_valid && k > 0.01f)
      {
        glEnable(GL_TEXTURE_2D);
        glDisable(GL_LIGHTING);
        glDisable(GL_DEPTH_TEST);
        glDisable(GL_FOG);
        glTexEnvi(GL_TEXTURE_ENV, GL_TEXTURE_ENV_MODE, GL_MODULATE);

        glMatrixMode(GL_PROJECTION);
        glPushMatrix();
        glLoadIdentity();
        glMatrixMode(GL_MODELVIEW);
        glPushMatrix();
        glLoadIdentity();

        // Each ghost layer is enlarged a little more and drawn a
        // little fainter; the enlargement is what makes the streaks
        for (int i = 1; i <= 3; ++i)
        {
          const float zoom = 1.0f + 0.012f * i * k;
          const float alpha = 0.5f * k / i;

          glColor4f(1, 1, 1, alpha);
          glBegin(GL_QUADS);
          glTexCoord2f(0, 0); glVertex2f(-zoom, -zoom);
          glTexCoord2f(1, 0); glVertex2f(zoom, -zoom);
          glTexCoord2f(1, 1); glVertex2f(zoom, zoom);
          glTexCoord2f(0, 1); glVertex2f(-zoom, zoom);
          glEnd();
        }

        glMatrixMode(GL_PROJECTION);
        glPopMatrix();
        glMatrixMode(GL_MODELVIEW);
        glPopMatrix();
      }

      glCopyTexSubImage2D(GL_TEXTURE_2D, 0, 0, 0, 0, 0,
                          m_width, m_height);
      m_blur_valid = true;
    }

    // Full-screen wavy blue grade whenever the camera goes under
    // the sea: grab the frame so far, redraw it through the
    // underwater shader
    void apply_underwater()
    {
      if (m_blur_tex == 0 || m_camera.position().y >= water_level)
        return;

      gl::ScopedAttribSaver attribs(GL_ENABLE_BIT | GL_CURRENT_BIT |
                                    GL_TEXTURE_BIT |
                                    GL_DEPTH_BUFFER_BIT);

      glActiveTexture(GL_TEXTURE0);
      glBindTexture(GL_TEXTURE_2D, m_blur_tex);
      glCopyTexSubImage2D(GL_TEXTURE_2D, 0, 0, 0, 0, 0,
                          m_width, m_height);

      glDisable(GL_DEPTH_TEST);
      glDisable(GL_LIGHTING);
      glDisable(GL_BLEND);

      m_uw_prog.use();
      m_uw_prog.set_int("scene", 0);
      m_uw_prog.set_float("time", m_total_time);

      glBegin(GL_QUADS);
      glTexCoord2f(0, 0); glVertex2f(-1, -1);
      glTexCoord2f(1, 0); glVertex2f(1, -1);
      glTexCoord2f(1, 1); glVertex2f(1, 1);
      glTexCoord2f(0, 1); glVertex2f(-1, 1);
      glEnd();

      m_uw_prog.unuse();
    }

    // Game HUD: speedometer dial, rocket charge bar, star counter
    void draw_hud()
    {
      const float kmh = m_vehicle.velocity().length() * 3.6f;
      const float frac = std::min(1.0f, kmh / 300.0f);
      const float PI = 3.14159265f;

      // dial center, in top-left-origin pixel coordinates
      const float cx = m_width - 130.0f;
      const float cy = m_height - 130.0f;
      const float R = 95.0f;

      {
        gl::ScopedOrthographicMode ortho;
        gl::ScopedAttribSaver attribs(GL_ENABLE_BIT | GL_CURRENT_BIT |
                                      GL_LINE_BIT);
        glEnable(GL_BLEND);

        // dial backplate
        glColor4f(0.05f, 0.08f, 0.12f, 0.6f);
        glBegin(GL_TRIANGLE_FAN);
        glVertex2f(cx, cy);
        for (int i = 0; i <= 32; ++i)
        {
          const float a = 2.0f * PI * i / 32.0f;
          glVertex2f(cx + R * cos(a), cy + R * sin(a));
        }
        glEnd();

        // speed arc: sweeps 240 degrees, green through red
        const int segs = (int)(40 * frac) + 1;
        glBegin(GL_TRIANGLE_STRIP);
        for (int i = 0; i <= segs; ++i)
        {
          const float f = frac * i / segs;
          const float a = (210.0f - 240.0f * f) * PI / 180.0f;
          glColor4f(0.2f + 0.8f * f, 1.0f - 0.85f * f, 0.15f, 0.9f);
          glVertex2f(cx + 0.97f * R * cos(a), cy - 0.97f * R * sin(a));
          glVertex2f(cx + 0.84f * R * cos(a), cy - 0.84f * R * sin(a));
        }
        glEnd();

        // ticks every 30 km/h
        glLineWidth(2);
        glColor4f(0.9f, 0.9f, 0.95f, 0.9f);
        glBegin(GL_LINES);
        for (int i = 0; i <= 10; ++i)
        {
          const float a = (210.0f - 24.0f * i) * PI / 180.0f;
          glVertex2f(cx + 0.80f * R * cos(a), cy - 0.80f * R * sin(a));
          glVertex2f(cx + 0.70f * R * cos(a), cy - 0.70f * R * sin(a));
        }
        glEnd();

        // needle
        {
          const float a = (210.0f - 240.0f * frac) * PI / 180.0f;
          glLineWidth(4);
          glColor4f(1.0f, 0.25f, 0.15f, 0.95f);
          glBegin(GL_LINES);
          glVertex2f(cx, cy);
          glVertex2f(cx + 0.78f * R * cos(a), cy - 0.78f * R * sin(a));
          glEnd();
        }

        // hub
        glColor4f(0.9f, 0.9f, 0.95f, 1.0f);
        glBegin(GL_TRIANGLE_FAN);
        glVertex2f(cx, cy);
        for (int i = 0; i <= 12; ++i)
        {
          const float a = 2.0f * PI * i / 12.0f;
          glVertex2f(cx + 6 * cos(a), cy + 6 * sin(a));
        }
        glEnd();

        // rocket charge bar, pulsing when ready
        {
          const float charge = m_vehicle.rocket_charge();
          const float bx = m_width - 340.0f;
          const float by = m_height - 44.0f;
          const float bw = 180.0f, bh = 14.0f;

          glColor4f(0.05f, 0.08f, 0.12f, 0.6f);
          glBegin(GL_QUADS);
          glVertex2f(bx, by);
          glVertex2f(bx + bw, by);
          glVertex2f(bx + bw, by + bh);
          glVertex2f(bx, by + bh);
          glEnd();

          const float pulse = (charge >= 1.0f)
              ? 0.7f + 0.3f * sin(m_total_time * 6.0f)
              : 0.9f;
          glColor4f(0.3f, 0.8f * pulse, pulse, 0.9f);
          glBegin(GL_QUADS);
          glVertex2f(bx, by);
          glVertex2f(bx + bw * charge, by);
          glVertex2f(bx + bw * charge, by + bh);
          glVertex2f(bx, by + bh);
          glEnd();
        }

        // golden star icon, top left
        {
          const float sx = 36, sy = 36;
          glColor4f(1.0f, 0.85f, 0.15f, 0.95f);
          glBegin(GL_TRIANGLE_FAN);
          glVertex2f(sx, sy);
          for (int i = 0; i <= 10; ++i)
          {
            const float a = (-90.0f + i * 36.0f) * PI / 180.0f;
            const float r = (i % 2 == 0) ? 17.0f : 7.5f;
            glVertex2f(sx + r * cos(a), sy + r * sin(a));
          }
          glEnd();
        }
      }

      // text labels (draw_glut_text manages its own ortho state)
      glColor3f(1.0f, 0.85f, 0.2f);
      gl::draw_glut_text(GLUT_BITMAP_TIMES_ROMAN_24, 60, 44,
                         "x " +
                         std::to_string(m_star_field.collected()));

      glColor3f(0.95f, 0.97f, 1.0f);
      gl::draw_glut_text(GLUT_BITMAP_TIMES_ROMAN_24,
                         (int)(cx - 20), (int)(cy + 50),
                         std::to_string((int)kmh));
      glColor3f(0.7f, 0.75f, 0.8f);
      gl::draw_glut_text(GLUT_BITMAP_HELVETICA_12,
                         (int)(cx - 18), (int)(cy + 68), "km/h");

      glColor3f(0.6f, 0.9f, 1.0f);
      gl::draw_glut_text(GLUT_BITMAP_HELVETICA_12,
                         (int)(m_width - 340), (int)(m_height - 50),
                         m_vehicle.rocket_charge() >= 1.0f
                             ? "ROCKET READY"
                             : "ROCKET");
    }

    void keyboard(unsigned char code,
                  int mx, int my,
                  KeyStatus status)
    {
      // Arrow keys arrive as GLUT_KEY_* "special" codes, which
      // numerically collide with ASCII ('d' == GLUT_KEY_LEFT), so
      // they must be dispatched by status
      const bool special = (status == KEY_SPECIAL_PRESSED ||
                            status == KEY_SPECIAL_RELEASED);
      const bool pressed = (status == KEY_PRESSED ||
                            status == KEY_SPECIAL_PRESSED);
      const float factor = pressed ? 1.0f : 0.0f;

      if (special)
      {
        switch (code)
        {
        case GLUT_KEY_LEFT:
          m_vehicle.set_yaw(-90 * factor);
          break;
        case GLUT_KEY_RIGHT:
          m_vehicle.set_yaw(90 * factor);
          break;
        case GLUT_KEY_UP:
          m_vehicle.set_thrust(1 * factor);
          break;
        case GLUT_KEY_DOWN:
          m_vehicle.set_thrust(-1 * factor);
          break;
        }
        return;
      }

      switch (code)
      {
      case 'a': case 'A':
        m_vehicle.set_yaw(-90 * factor);
        break;

      case 'd': case 'D':
        m_vehicle.set_yaw(90 * factor);
        break;

      case 'w': case 'W':
        m_vehicle.set_thrust(1 * factor);
        break;

      case 's': case 'S':
        m_vehicle.set_thrust(-1 * factor);
        break;

      case ' ': // Rocket jump!
        if (pressed)
          m_vehicle.rocket_jump();
        break;

      case 27: // ESC quits the fullscreen game
        std::exit(0);
        break;
      }
    }

    void passive_motion(int x, int y)
    {
      m_mouse.update(x, y);
      glutPostRedisplay();
    }

    void render_scene()
    {
      // Use dynamic fog color for background clear with full alpha
      glClearColor(fog.x, fog.y, fog.z, 1.0);
      glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);

      glMatrixMode(GL_MODELVIEW);
      m_camera.realize();

      // Hard-landing camera shake: a small random kick, applied to
      // the view matrix, decaying over a few tenths of a second.
      // Fades out when the camera is close to the ground so the
      // view can't dip below the terrain.
      if (m_shake > 0.005f)
      {
        const Vector3D cam = m_camera.position();
        const float ground =
            m_map1.interpolated_height(cam.x, cam.z);
        const float clearance = cam.y - ground;
        const float room =
            std::min(1.0f, std::max(0.0f, (clearance - 2.0f) / 8.0f));

        std::uniform_real_distribution<float> u(-1.0f, 1.0f);
        glRotatef(m_shake * room * u(m_fx_rng), 0, 0, 1);
        glRotatef(m_shake * room * u(m_fx_rng), 1, 0, 0);
      }

      {
        gl::ScopedMatrixSaver matrix;
        gl::translate(m_camera.position());
        m_sky.render();
      }

      m_terrain_renderer.render();
      m_vegetation.render(fog);
      m_star_field.render(m_total_time);
      m_terrain_renderer.translate();
      m_vehicle.render();
      m_fish_school.render(m_total_time);

      // Translucent water goes last so the seabed, fish, and a
      // submerged bike show through it, then dust so spray sits on
      // top of the surface
      m_ocean.render(m_total_time, fog);
      m_dust.render();
    }

    void display()
    {
      // Use dynamic fog color for background clear
      glClearColor(fog.x, fog.y, fog.z, 0);

      render_scene();
      apply_underwater();
      apply_motion_blur();
      draw_hud();

      glutSwapBuffers();
    }

  private:
    void setup_lights()
    {
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
    }

    void update_dynamic_lighting(float sun_height, float time)
    {
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
      float length = sqrt(x * x + y * y + z * z);
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

      if (is_sunset)
      {
        // Calculate sunset intensity
        float sunset_intensity;
        if (sun_height < 0.5f)
        {
          sunset_intensity = 1.0f - fabs(sun_height - 0.2f) / 0.1f;
        }
        else
        {
          sunset_intensity = 1.0f - fabs(sun_height - 0.8f) / 0.1f;
        }
        sunset_intensity = std::max(0.0f, std::min(1.0f, sunset_intensity));

        // Blend between day/night and sunset colors
        float day_factor = (sun_height < 0.5f) ? sun_height / 0.5f : (1.0f - (sun_height - 0.5f) / 0.5f);
        Vector3D base_color = night_color * (1.0f - day_factor) + day_color * day_factor;

        light_color = base_color * (1.0f - sunset_intensity) + sunset_color * sunset_intensity;
        ambient_color = light_color * 0.4f;
        specular_color = Vector3D(1.0f, 0.9f, 0.8f) * (0.7f + sunset_intensity * 0.3f);
      }
      else
      {
        // Normal day/night cycle
        light_color = night_color * (1.0f - sun_height) + day_color * sun_height;

        // Ambient is stronger during day, weaker at night
        float ambient_strength = 0.25f + sun_height * 0.35f;
        ambient_color = light_color * ambient_strength;

        // Specular is stronger during day
        float spec_strength = 0.4f + sun_height * 0.6f;
        specular_color = Vector3D(0.8f, 0.8f, 0.9f) * spec_strength;
      }

      // Apply ambient light
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

  private:
    gfx::ThirdPersonCamera m_camera;
    gfx::MouseCameraController m_mouse;

    Timer m_timer;
    map::RandomHeightMap m_map1;

    gfx::TerrainRenderer m_terrain_renderer;
    mov::Vehicle m_vehicle;
    gfx::Sky m_sky;
    gfx::Ocean m_ocean;
    Vegetation m_vegetation;
    Stars m_star_field;
    Dust m_dust;
    Fish m_fish_school;

    gl::Shader m_uw_vert;
    gl::Shader m_uw_frag;
    gl::ShaderProgram m_uw_prog;

    float m_last_shadow_update;
    float m_total_time;

    GLuint m_blur_tex;
    bool m_blur_valid;

    float m_shake;
    std::mt19937 m_fx_rng;
  };
}

int main(int argc, char **argv)
{
  using namespace moppe;

  MoppeGLUT app;
  app::global_app = &app;

  app.initialize(argc, argv,
                 GLUT_RGBA | GLUT_DOUBLE | GLUT_DEPTH);

  try
  {
    app.run_main_loop();
  }
  catch (const std::exception &e)
  {
    std::cerr << "\nError: " << e.what() << "\n";
    std::exit(-1);
  }
}
