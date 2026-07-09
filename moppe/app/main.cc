
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
#include <cstdio>
#include <iostream>
#include <ctime>
#include <random>

namespace moppe
{
  using namespace map;
  using namespace app;

  // World parameters; --pico swaps these for the real Pico Island
  Vector3D map_size(5000 * one_meter,
                    650 * one_meter,
                    5000 * one_meter);

  int resolution = 2049; // Higher resolution for smoother terrain

  // Sea level: valleys below this are flooded
  float water_level = 50 * one_meter;

  // Haze density; larger worlds get clearer air so the big sights
  // stay visible
  float fog_scale = 0.0004f;

  // Ride the real Pico Island (Copernicus GLO-30 DEM) instead of
  // procedurally generated terrain
  bool pico_mode = false;

  // Urban stunt island: buildings, ramps, and traffic
  bool city_mode = false;

  static Vector3D spawn_position()
  {
    if (pico_mode)
      return Vector3D(0.34 * map_size.x, 3000, 0.55 * map_size.z);
    if (city_mode)
      return Vector3D(map_size.x / 2 + 20, 100, map_size.z / 2 + 20);
    return Vector3D(50 * one_meter, 600 * one_meter, 50 * one_meter);
  }

  // Dynamic fog color will be set based on sky horizon colors
  Vector3D fog(0.5, 0.5, 0.5);

  // THE sun: one fixed direction shared by the GL light, the
  // shadow map, the sky's sun disc, and the ocean glint
  const float sun_azimuth = 0.8f; // radians around y

  static Vector3D sun_direction_for(float height)
  {
    const float el = (height - 0.5f) * 3.14159f;
    return Vector3D(cos(el) * sin(sun_azimuth), sin(el),
                    cos(el) * cos(sun_azimuth));
  }

  // Scattered trees and bushes, bucketed into sector display
  // lists so only the sectors near the camera are drawn.
  class Vegetation
  {
  public:
    static const int VSEC = 6;

    Vegetation()
    {
      for (int s = 0; s < VSEC * VSEC; ++s)
        m_lists[s] = 0;
    }

    void generate(const map::HeightMap& map, int trees, int bushes,
                  unsigned seed)
    {
      std::mt19937 rng(seed);
      std::uniform_real_distribution<float> u(0.0f, 1.0f);

      const Vector3D size = map.size();

      // pick all the spots first so they can be sector-bucketed
      struct Plant
      {
        bool tree;
        float x, y, z, s, tint;
      };
      std::vector<Plant> plants;

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

        Plant p = { true, x, y, z, 0.8f + 1.0f * u(rng), u(rng) };
        plants.push_back(p);
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

        Plant p = { false, x, y, z, 0.6f + 0.8f * u(rng), u(rng) };
        plants.push_back(p);
        ++placed;
      }

      for (int s = 0; s < VSEC * VSEC; ++s)
      {
        if (m_lists[s] == 0)
          m_lists[s] = glGenLists(1);
        glNewList(m_lists[s], GL_COMPILE);
        for (size_t i = 0; i < plants.size(); ++i)
        {
          const Plant& p = plants[i];
          if (sector_of(p.x, p.z) != s)
            continue;
          if (p.tree)
            draw_tree(p.x, p.y, p.z, p.s, p.tint);
          else
            draw_bush(p.x, p.y, p.z, p.s, p.tint);
        }
        glEndList();
      }
    }

    static int sector_of(float x, float z)
    {
      int sx = (int)(x / (map_size.x / VSEC));
      int sz = (int)(z / (map_size.z / VSEC));
      sx = std::max(0, std::min(VSEC - 1, sx));
      sz = std::max(0, std::min(VSEC - 1, sz));
      return sz * VSEC + sx;
    }

    void render(const Vector3D& fog_color, float fog_density,
                const Vector3D& cam) const
    {
      if (m_lists[0] == 0)
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
      glFogf(GL_FOG_DENSITY, fog_density);
      glFogfv(GL_FOG_COLOR, fc);

      // Only sectors within fog-visibility range get drawn
      const float sec = map_size.x / VSEC;
      const float reach = 1.9f / fog_density + sec * 0.71f;
      for (int sz = 0; sz < VSEC; ++sz)
        for (int sx = 0; sx < VSEC; ++sx)
        {
          const float dx = cam.x - (sx + 0.5f) * sec;
          const float dz = cam.z - (sz + 0.5f) * sec;
          if (dx * dx + dz * dz < reach * reach)
            glCallList(m_lists[sz * VSEC + sx]);
        }
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

    GLuint m_lists[VSEC * VSEC];
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

    void render(float time, const Vector3D& cam) const
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

        const float dx = cam.x - s.pos.x, dz = cam.z - s.pos.z;
        if (dx * dx + dz * dz > 900.0f * 900.0f)
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
    Dust() : m_particles(), m_rng(99), m_tex(0) {}

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
        p.max_life = 0.5f + 0.4f * (0.5f + 0.5f * u(m_rng));
        p.life = p.max_life;
        p.size = 0.7f + 0.4f * u(m_rng);
        p.rot = 3.14159f * u(m_rng);
        p.rot_v = 2.2f * u(m_rng);
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
        p.rot += p.rot_v * dt;
        m_particles[j++] = p;
      }
      m_particles.resize(j);
    }

    void render()
    {
      if (m_particles.empty())
        return;

      ensure_texture();

      // Billboard axes straight from the current view matrix
      float mv[16];
      glGetFloatv(GL_MODELVIEW_MATRIX, mv);
      const Vector3D right(mv[0], mv[4], mv[8]);
      const Vector3D up(mv[1], mv[5], mv[9]);

      gl::ScopedAttribSaver attribs(GL_ENABLE_BIT | GL_CURRENT_BIT |
                                    GL_TEXTURE_BIT |
                                    GL_DEPTH_BUFFER_BIT);
      for (int unit = 3; unit >= 1; --unit)
      {
        glActiveTexture(GL_TEXTURE0 + unit);
        glDisable(GL_TEXTURE_2D);
      }
      glActiveTexture(GL_TEXTURE0);
      glEnable(GL_TEXTURE_2D);
      glBindTexture(GL_TEXTURE_2D, m_tex);
      glTexEnvi(GL_TEXTURE_ENV, GL_TEXTURE_ENV_MODE, GL_MODULATE);
      glDisable(GL_LIGHTING);
      glDepthMask(GL_FALSE); // soft puffs, no z-fighting each other

      glBegin(GL_QUADS);
      for (size_t i = 0; i < m_particles.size(); ++i)
      {
        const Particle& p = m_particles[i];
        const float a = p.life / p.max_life;
        const float s = p.size * (1.7f - 0.7f * a); // grow as it fades

        // spin the billboard so puffs tumble
        const float ca = cos(p.rot), sa = sin(p.rot);
        const Vector3D r2 = (right * ca + up * sa) * s;
        const Vector3D u2 = (up * ca - right * sa) * s;

        glColor4f(p.color.x, p.color.y, p.color.z, 0.75f * a);
        glTexCoord2f(0, 0);
        gl::vertex(p.pos - r2 - u2);
        glTexCoord2f(1, 0);
        gl::vertex(p.pos + r2 - u2);
        glTexCoord2f(1, 1);
        gl::vertex(p.pos + r2 + u2);
        glTexCoord2f(0, 1);
        gl::vertex(p.pos - r2 + u2);
      }
      glEnd();
    }

  private:
    // A soft round sprite, generated once: white with a smooth
    // radial alpha falloff, so puffs read as smoke instead of
    // hard-edged squares
    void ensure_texture()
    {
      if (m_tex != 0)
        return;

      const int N = 64;
      std::vector<unsigned char> img(N * N * 4);
      for (int y = 0; y < N; ++y)
        for (int x = 0; x < N; ++x)
        {
          const float dx = (x + 0.5f) / N * 2 - 1;
          const float dy = (y + 0.5f) / N * 2 - 1;
          float a = std::max(
              0.0f, 1.0f - std::sqrt(dx * dx + dy * dy));
          a = a * a * (3.0f - 2.0f * a); // smooth shoulder

          unsigned char* px = &img[(y * N + x) * 4];
          px[0] = px[1] = px[2] = 255;
          px[3] = (unsigned char)(255.0f * a);
        }

      glGenTextures(1, &m_tex);
      glBindTexture(GL_TEXTURE_2D, m_tex);
      glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER,
                      GL_LINEAR);
      glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER,
                      GL_LINEAR);
      glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S,
                      GL_CLAMP_TO_EDGE);
      glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T,
                      GL_CLAMP_TO_EDGE);
      glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, N, N, 0, GL_RGBA,
                   GL_UNSIGNED_BYTE, &img[0]);
    }

    struct Particle
    {
      Vector3D pos, vel, color;
      float life, max_life, size, rot, rot_v;
    };

    std::vector<Particle> m_particles;
    std::mt19937 m_rng;
    GLuint m_tex;
  };

  // A soft dark disc projected on the ground under a vehicle: the
  // classic cheap stand-in for a real cast shadow, and it reads
  // wonderfully -- especially mid rocket-jump, when it shrinks and
  // fades below you.
  class BlobShadow
  {
  public:
    BlobShadow() : m_tex(0) {}

    void draw(const Vector3D& pos, float radius,
              const map::HeightMap& map)
    {
      ensure_texture();

      const float gy = map.interpolated_height(pos.x, pos.z);
      const float h = pos.y - gy;
      if (h > 30.0f || h < -2.0f)
        return; // too high to matter (or underground somehow)

      const float fade = 1.0f - std::max(0.0f, h) / 30.0f;
      const float r = radius * (1.0f + 0.5f * (1.0f - fade));

      Vector3D n = map.interpolated_normal(pos.x, pos.z);
      Vector3D t1 = n.cross(Vector3D(1, 0, 0));
      if (t1.length2() < 0.01f)
        t1 = n.cross(Vector3D(0, 0, 1));
      t1.normalize();
      Vector3D t2 = n.cross(t1);

      const Vector3D c =
          Vector3D(pos.x, gy, pos.z) + n * 0.14f;

      gl::ScopedAttribSaver attribs(GL_ENABLE_BIT | GL_CURRENT_BIT |
                                    GL_TEXTURE_BIT |
                                    GL_DEPTH_BUFFER_BIT);
      for (int unit = 3; unit >= 1; --unit)
      {
        glActiveTexture(GL_TEXTURE0 + unit);
        glDisable(GL_TEXTURE_2D);
      }
      glActiveTexture(GL_TEXTURE0);
      glEnable(GL_TEXTURE_2D);
      glBindTexture(GL_TEXTURE_2D, m_tex);
      glTexEnvi(GL_TEXTURE_ENV, GL_TEXTURE_ENV_MODE, GL_MODULATE);
      glDisable(GL_LIGHTING);
      glDisable(GL_CULL_FACE);
      glDepthMask(GL_FALSE);
      glEnable(GL_BLEND);

      glColor4f(0.0f, 0.0f, 0.0f, 0.45f * fade);
      glBegin(GL_QUADS);
      glTexCoord2f(0, 0);
      gl::vertex(c - t1 * r - t2 * r);
      glTexCoord2f(1, 0);
      gl::vertex(c + t1 * r - t2 * r);
      glTexCoord2f(1, 1);
      gl::vertex(c + t1 * r + t2 * r);
      glTexCoord2f(0, 1);
      gl::vertex(c - t1 * r + t2 * r);
      glEnd();
    }

  private:
    void ensure_texture()
    {
      if (m_tex != 0)
        return;

      const int N = 64;
      std::vector<unsigned char> img(N * N * 4);
      for (int y = 0; y < N; ++y)
        for (int x = 0; x < N; ++x)
        {
          const float dx = (x + 0.5f) / N * 2 - 1;
          const float dy = (y + 0.5f) / N * 2 - 1;
          float a = std::max(
              0.0f, 1.0f - std::sqrt(dx * dx + dy * dy));
          a = a * a * (3.0f - 2.0f * a);

          unsigned char* px = &img[(y * N + x) * 4];
          px[0] = px[1] = px[2] = 255;
          px[3] = (unsigned char)(255.0f * a);
        }

      glGenTextures(1, &m_tex);
      glBindTexture(GL_TEXTURE_2D, m_tex);
      glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER,
                      GL_LINEAR);
      glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER,
                      GL_LINEAR);
      glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S,
                      GL_CLAMP_TO_EDGE);
      glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T,
                      GL_CLAMP_TO_EDGE);
      glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, N, N, 0, GL_RGBA,
                   GL_UNSIGNED_BYTE, &img[0]);
    }

    GLuint m_tex;
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

    void render(float time, const Vector3D& cam) const
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

        const float cdx = cam.x - f.cx, cdz = cam.z - f.cz;
        if (cdx * cdx + cdz * cdz > 600.0f * 600.0f)
          continue; // invisible underwater from that far

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

  // Grazing horse herds on the grasslands, circling bird flocks in
  // the sky -- dark birds over land, gulls over the sea.
  class Wildlife
  {
  public:
    void generate(const map::HeightMap& map, float water,
                  int herds, int flocks, unsigned seed)
    {
      std::mt19937 rng(seed);
      std::uniform_real_distribution<float> u(0.0f, 1.0f);
      const Vector3D size = map.size();

      m_horses.clear();
      int made = 0, tries = 0;
      while (made < herds && tries++ < herds * 400)
      {
        const float hx = size.x * (0.05f + 0.9f * u(rng));
        const float hz = size.z * (0.05f + 0.9f * u(rng));
        const float hy = map.interpolated_height(hx, hz);

        // Herds like flat grassy meadows
        if (hy < water + 8.0f || hy > 0.30f * map_size.y)
          continue;
        if (map.interpolated_normal(hx, hz).y < 0.88f)
          continue;

        const int n = 3 + (int)(u(rng) * 4);
        for (int i = 0; i < n; ++i)
        {
          Horse h;
          h.x = hx + 18.0f * (u(rng) - 0.5f);
          h.z = hz + 18.0f * (u(rng) - 0.5f);
          h.y = map.interpolated_height(h.x, h.z);
          h.heading = 360.0f * u(rng);
          h.phase = 6.2832f * u(rng);
          h.size = 0.85f + 0.3f * u(rng);
          h.tone = u(rng);
          m_horses.push_back(h);
        }
        ++made;
      }

      m_birds.clear();
      for (int f = 0; f < flocks; ++f)
      {
        const float fx = size.x * (0.05f + 0.9f * u(rng));
        const float fz = size.z * (0.05f + 0.9f * u(rng));
        const float ground = map.interpolated_height(fx, fz);
        const bool over_sea = ground < water;
        const float alt =
            std::max(ground, water) + 35.0f + 70.0f * u(rng);

        const int n = 6 + (int)(u(rng) * 5);
        for (int i = 0; i < n; ++i)
        {
          Bird b;
          b.cx = fx;
          b.cz = fz;
          b.y = alt + 8.0f * (u(rng) - 0.5f);
          b.radius = 18.0f + 22.0f * u(rng);
          b.speed = 0.25f + 0.2f * u(rng);
          b.offset = 6.2832f * u(rng);
          b.flap = 6.2832f * u(rng);
          b.size = 0.8f + 0.5f * u(rng);
          b.gull = over_sea;
          m_birds.push_back(b);
        }
      }
    }

    void render(float time, const Vector3D& cam) const
    {
      if (m_horses.empty() && m_birds.empty())
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

      for (size_t i = 0; i < m_horses.size(); ++i)
      {
        const Horse& h = m_horses[i];
        const float dx = cam.x - h.x, dz = cam.z - h.z;
        if (dx * dx + dz * dz > 700.0f * 700.0f)
          continue;
        draw_horse(h, time);
      }

      // Birds read best as flat silhouettes, visible from below too
      glDisable(GL_LIGHTING);
      glDisable(GL_CULL_FACE);
      for (size_t i = 0; i < m_birds.size(); ++i)
      {
        const Bird& b = m_birds[i];
        const float dx = cam.x - b.cx, dz = cam.z - b.cz;
        if (dx * dx + dz * dz > 1300.0f * 1300.0f)
          continue;
        draw_bird(b, time);
      }
    }

  private:
    struct Horse
    {
      float x, y, z, heading, phase, size, tone;
    };

    struct Bird
    {
      float cx, cz, y, radius, speed, offset, flap, size;
      bool gull;
    };

    static void box(float w, float h, float d)
    {
      glPushMatrix();
      glScalef(w, h, d);
      glutSolidCube(1.0);
      glPopMatrix();
    }

    static void blob(float rx, float ry, float rz)
    {
      glPushMatrix();
      glScalef(rx, ry, rz);
      glutSolidSphere(1.0, 10, 8);
      glPopMatrix();
    }

    static void draw_horse(const Horse& h, float time)
    {
      gl::ScopedMatrixSaver m;
      glTranslatef(h.x, h.y, h.z);
      glRotatef(h.heading, 0, 1, 0);
      glScalef(h.size, h.size, h.size);

      // coat: bay through chestnut, with the odd gray one
      if (h.tone < 0.75f)
        glColor3f(0.30f + 0.35f * h.tone,
                  0.20f + 0.20f * h.tone,
                  0.13f + 0.12f * h.tone);
      else
        glColor3f(0.82f, 0.80f, 0.76f);

      // body
      {
        gl::ScopedMatrixSaver part;
        glTranslatef(0, 1.05f, 0);
        blob(0.30f, 0.34f, 0.75f);
      }

      // legs
      for (int lx = -1; lx <= 1; lx += 2)
        for (int lz = -1; lz <= 1; lz += 2)
        {
          gl::ScopedMatrixSaver part;
          glTranslatef(lx * 0.16f, 0.42f, lz * 0.45f);
          box(0.10f, 0.84f, 0.10f);
        }

      // neck and head, dipping down to graze and back up
      {
        gl::ScopedMatrixSaver part;
        glTranslatef(0, 1.25f, 0.6f);
        const float dip = 0.5f + 0.5f * sin(time * 0.7f + h.phase);
        glRotatef(-45.0f + 92.0f * dip * dip, 1, 0, 0);
        {
          gl::ScopedMatrixSaver seg;
          glTranslatef(0, 0, 0.3f);
          blob(0.11f, 0.14f, 0.42f);
        }
        {
          gl::ScopedMatrixSaver seg;
          glTranslatef(0, 0.02f, 0.72f);
          blob(0.09f, 0.12f, 0.26f);
        }
      }

      // swishing tail
      {
        gl::ScopedMatrixSaver part;
        glTranslatef(0, 1.2f, -0.78f);
        glRotatef(14.0f * sin(time * 2.3f + h.phase), 0, 0, 1);
        glTranslatef(0, -0.25f, 0);
        glColor3f(0.15f, 0.12f, 0.10f);
        blob(0.06f, 0.30f, 0.07f);
      }
    }

    static void draw_bird(const Bird& b, float time)
    {
      const float a = b.offset + time * b.speed;

      gl::ScopedMatrixSaver m;
      glTranslatef(b.cx + cos(a) * b.radius,
                   b.y + 1.5f * sin(time * 0.9f + b.flap),
                   b.cz + sin(a) * b.radius);
      glRotatef(-a * 57.2958f, 0, 1, 0);
      glRotatef(-18.0f, 0, 0, 1); // bank into the circle
      glScalef(b.size, b.size, b.size);

      if (b.gull)
        glColor3f(0.92f, 0.93f, 0.95f);
      else
        glColor3f(0.13f, 0.12f, 0.14f);

      glPushMatrix();
      glScalef(0.10f, 0.09f, 0.30f);
      glutSolidSphere(1.0, 6, 5);
      glPopMatrix();

      // flapping wing triangles
      const float wy = 0.40f * sin(time * 9.0f + b.flap);
      glBegin(GL_TRIANGLES);
      glVertex3f(0, 0, 0.12f);
      glVertex3f(0, 0, -0.10f);
      glVertex3f(-0.55f, wy, -0.02f);
      glVertex3f(0, 0, 0.12f);
      glVertex3f(0, 0, -0.10f);
      glVertex3f(0.55f, wy, -0.02f);
      glEnd();
    }

    std::vector<Horse> m_horses;
    std::vector<Bird> m_birds;
  };

  // --city mode: a flat city island baked into the heightmap
  // (streets, launch ramps, stunt mounds, a beach ring into the
  // sea), buildings as solid collision boxes with drivable roofs,
  // and box-cars cruising the street grid.
  class City
  {
  public:
    City()
        : m_map(0),
          m_streets_list(0),
          m_furniture_list(0),
          m_air_x0(0), m_air_x1(0), m_air_z0(0), m_air_z1(0)
    {
      for (int s = 0; s < SEC * SEC; ++s)
        m_sectors[s] = 0;
    }

    const std::vector<mov::Box>& obstacles() const
    { return m_boxes; }

    // Bowl over any pedestrian the bike plows through; returns how
    // many got launched this tick
    int update_people(const Vector3D& bike, const Vector3D& bike_vel,
                      float time)
    {
      if (!m_map)
        return 0;

      const float speed = bike_vel.length();
      int hits = 0;

      for (size_t i = 0; i < m_people.size(); ++i)
      {
        Person& p = m_people[i];

        if (p.hit_time > 0)
        {
          if (time - p.hit_time > 5.0f)
            p.hit_time = 0; // dusts off and walks on
          continue;
        }
        if (speed < 5.0f)
          continue;

        float wx, wz;
        person_pos(p, time, wx, wz);
        const float ground = m_map->interpolated_height(wx, wz);

        const float dx = bike.x - wx, dz = bike.z - wz;
        if (dx * dx + dz * dz > 2.5f * 2.5f)
          continue;
        if (bike.y > ground + 3.5f)
          continue; // jumped clean over them

        p.hit_time = time;
        p.hx = wx;
        p.hy = ground + 1.0f;
        p.hz = wz;
        p.hvx = bike_vel.x * 0.55f;
        p.hvz = bike_vel.z * 0.55f;
        p.hvy = 6.5f + 0.12f * speed;
        m_last_hit = Vector3D(wx, ground + 1, wz);
        ++hits;
      }
      return hits;
    }

    Vector3D last_hit() const { return m_last_hit; }

    // Take the nearest still-driving car within reach: it leaves
    // traffic and becomes the player's.  Returns its kind, or -1.
    int take_car_near(const Vector3D& pos, float radius, float time,
                      Vector3D& out_pos, Vector3D& out_dir,
                      Vector3D& out_color)
    {
      if (!m_map)
        return -1;

      const float cx = map_size.x / 2, cz = map_size.z / 2;
      for (size_t i = 0; i < m_cars.size(); ++i)
      {
        Car& c = m_cars[i];
        if (!c.active)
          continue;

        const float s =
            fmod(c.phase + c.speed * time, 2 * c.half) - c.half;
        const float wx = c.along_x ? cx + c.dir * s : c.line;
        const float wz = c.along_x ? c.line : cz + c.dir * s;
        const float dx = pos.x - wx, dz = pos.z - wz;
        if (dx * dx + dz * dz > radius * radius)
          continue;

        c.active = false;
        out_pos = Vector3D(
            wx, m_map->interpolated_height(wx, wz) + 1.2f, wz);
        out_dir = c.along_x ? Vector3D(c.dir, 0, 0)
                            : Vector3D(0, 0, c.dir);
        out_color = c.color;
        return c.kind;
      }
      return -1;
    }

    static const float H_CITY;

    void generate(map::RandomHeightMap& map, unsigned seed)
    {
      m_map = &map;
      std::mt19937 rng(seed);
      std::uniform_real_distribution<float> u(0.0f, 1.0f);

      const float cx = map_size.x / 2, cz = map_size.z / 2;
      const float sx = map.scale().x, sz = map.scale().z;
      const int W = map.width(), H = map.height();

      // -- flat slab, smoothed beach ring, shallow sea outside
      for (int gy = 0; gy < H; ++gy)
        for (int gx = 0; gx < W; ++gx)
        {
          const float wx = gx * sx, wz = gy * sz;
          const float d = std::sqrt((wx - cx) * (wx - cx)
                                    + (wz - cz) * (wz - cz));
          float h;
          if (d < CORE)
            h = H_CITY;
          else if (d < CORE + 500)
          {
            float t = (d - CORE) / 500;
            t = t * t * (3 - 2 * t);
            h = H_CITY * (1 - t) - 8 * t;
          }
          else
            h = -8;

          map.set(gx, gy, std::max(0.0f, h) / map_size.y);
        }

      // -- airport zone on the southeast side: keep it clear
      m_air_x0 = cx + 330;
      m_air_x1 = cx + 1170;
      m_air_z0 = cz + 980;
      m_air_z1 = cz + 1130;

      // -- a few big smooth stunt mounds
      for (int i = 0; i < 6; ++i)
      {
        const float ang = 6.2832f * u(rng);
        const float dist = 400 + 900 * u(rng);
        const float mx = cx + cos(ang) * dist;
        const float mz = cz + sin(ang) * dist;
        if (in_airport(mx, mz, 80))
          continue;
        const float R = 60 + 30 * u(rng);
        const float MH = 12 + 8 * u(rng);

        for (int gy = (int)((mz - R) / sz); gy <= (mz + R) / sz; ++gy)
          for (int gx = (int)((mx - R) / sx); gx <= (mx + R) / sx;
               ++gx)
          {
            if (gx < 0 || gy < 0 || gx >= W || gy >= H)
              continue;
            const float wx = gx * sx, wz = gy * sz;
            const float d = std::sqrt((wx - mx) * (wx - mx)
                                      + (wz - mz) * (wz - mz));
            if (d >= R)
              continue;
            const float c = cos(1.5708f * d / R);
            const float h = H_CITY + MH * c * c;
            map.set(gx, gy,
                    std::max(map.get(gx, gy), h / map_size.y));
          }
      }

      // -- launch-ramp wedges on the streets: rise, then a drop
      for (int i = 0; i < 30; ++i)
      {
        const bool along_x = u(rng) < 0.5f;
        const int k = (int)((u(rng) - 0.5f) * 44.0f);
        const float line = (along_x ? cz : cx) + k * PITCH;
        const float at = (along_x ? cx : cz)
            + (u(rng) - 0.5f) * 2600.0f;
        const float dir = (u(rng) < 0.5f) ? 1.0f : -1.0f;

        const float rx = along_x ? at : line;
        const float rz = along_x ? line : at;
        if (std::sqrt((rx - cx) * (rx - cx) + (rz - cz) * (rz - cz))
            > CORE - 200)
          continue;
        if (in_airport(rx, rz, 30))
          continue;

        const float LEN = 22, WID = 9, RH = 7;
        for (int gy = (int)((rz - LEN) / sz); gy <= (rz + LEN) / sz;
             ++gy)
          for (int gx = (int)((rx - LEN) / sx);
               gx <= (rx + LEN) / sx; ++gx)
          {
            if (gx < 0 || gy < 0 || gx >= W || gy >= H)
              continue;
            const float wx = gx * sx, wz = gy * sz;
            const float along =
                (along_x ? wx - rx : wz - rz) * dir;
            const float across = along_x ? wz - rz : wx - rx;
            if (along < -LEN / 2 || along > LEN / 2 ||
                std::abs(across) > WID / 2)
              continue;
            const float t = (along + LEN / 2) / LEN;
            const float h = H_CITY + RH * t;
            map.set(gx, gy,
                    std::max(map.get(gx, gy), h / map_size.y));
          }
      }

      // -- police and fire stations on two reserved blocks
      {
        const float px = cx + (-4 + 0.5f) * PITCH;
        const float pz = cz + (0 + 0.5f) * PITCH;
        m_police.x0 = px - 18;
        m_police.x1 = px + 18;
        m_police.z0 = pz - 13;
        m_police.z1 = pz + 13;
        m_police.top = H_CITY + 12;
        m_boxes.push_back(m_police);

        const float fx = cx + (3 + 0.5f) * PITCH;
        const float fz = cz + (1 + 0.5f) * PITCH;
        m_fire.x0 = fx - 20;
        m_fire.x1 = fx + 20;
        m_fire.z0 = fz - 14;
        m_fire.z1 = fz + 14;
        m_fire.top = H_CITY + 10;
        m_boxes.push_back(m_fire);
      }

      // -- buildings on the blocks between streets, taller downtown
      for (int bi = -27; bi <= 27; ++bi)
        for (int bj = -27; bj <= 27; ++bj)
        {
          if ((bi == -4 && bj == 0) || (bi == 3 && bj == 1))
            continue; // station blocks

          const float bx = cx + (bi + 0.5f) * PITCH;
          const float bz = cz + (bj + 0.5f) * PITCH;
          const float d = std::sqrt((bx - cx) * (bx - cx)
                                    + (bz - cz) * (bz - cz));
          if (d > CORE - 150)
            continue;
          if (in_airport(bx, bz, PITCH))
            continue; // runways need headroom
          if (u(rng) > 0.8f)
            continue; // plaza

          const float hw = (14 + 20 * u(rng)) / 2;
          const float hd = (14 + 20 * u(rng)) / 2;
          const float room = PITCH / 2 - STREET_W / 2 - 3;
          const float ox = (u(rng) - 0.5f) * 2 * (room - hw);
          const float oz = (u(rng) - 0.5f) * 2 * (room - hd);

          const float hmax =
              18 + 110 * std::exp(-(d / 800) * (d / 800));
          const float bh =
              std::max(12.0f, hmax * (0.35f + 0.65f * u(rng)));

          Building b;
          b.box.x0 = bx + ox - hw;
          b.box.x1 = bx + ox + hw;
          b.box.z0 = bz + oz - hd;
          b.box.z1 = bz + oz + hd;
          b.box.top = H_CITY + bh;

          static const float palette[5][3] = {
            { 0.62f, 0.62f, 0.64f }, // concrete
            { 0.72f, 0.65f, 0.55f }, // sandstone
            { 0.55f, 0.32f, 0.26f }, // brick
            { 0.35f, 0.50f, 0.62f }, // glass
            { 0.80f, 0.80f, 0.78f }, // white
          };
          const int p = (int)(u(rng) * 5) % 5;
          const float j = 0.9f + 0.2f * u(rng);
          b.color = Vector3D(palette[p][0] * j, palette[p][1] * j,
                             palette[p][2] * j);

          m_buildings.push_back(b);
          m_boxes.push_back(b.box);
        }

      // -- traffic: plenty of cars, plus police and fire trucks
      for (int i = 0; i < 316; ++i)
      {
        Car c;
        c.along_x = u(rng) < 0.5f;
        c.active = true;
        const int k = (int)((u(rng) - 0.5f) * 40.0f);
        c.dir = (u(rng) < 0.5f) ? 1.0f : -1.0f;
        c.line = (c.along_x ? cz : cx) + k * PITCH + c.dir * 2.6f;
        c.half = std::sqrt(std::max(
            1.0f, (CORE - 120) * (CORE - 120)
                      - (k * PITCH) * (k * PITCH)));
        c.phase = 3000 * u(rng);

        if (i < 10)
          c.kind = 1; // police
        else if (i < 16)
          c.kind = 2; // fire truck
        else
          c.kind = 0;

        c.speed = (c.kind == 1)   ? 16 + 6 * u(rng)
                  : (c.kind == 2) ? 12 + 4 * u(rng)
                                  : 8 + 8 * u(rng);

        static const float carpal[5][3] = {
          { 0.8f, 0.15f, 0.1f }, { 0.15f, 0.3f, 0.7f },
          { 0.85f, 0.85f, 0.85f }, { 0.9f, 0.75f, 0.1f },
          { 0.1f, 0.5f, 0.45f },
        };
        const int p = (int)(u(rng) * 5) % 5;
        c.color = (c.kind == 1)
                      ? Vector3D(0.92f, 0.92f, 0.95f)
                      : (c.kind == 2)
                            ? Vector3D(0.85f, 0.1f, 0.08f)
                            : Vector3D(carpal[p][0], carpal[p][1],
                                       carpal[p][2]);
        m_cars.push_back(c);
      }

      // -- pedestrians strolling the sidewalks
      for (int i = 0; i < 260; ++i)
      {
        Person p;
        p.along_x = u(rng) < 0.5f;
        const int k = (int)((u(rng) - 0.5f) * 40.0f);
        p.dir = (u(rng) < 0.5f) ? 1.0f : -1.0f;
        const float side = (u(rng) < 0.5f) ? 1.0f : -1.0f;
        p.line = (p.along_x ? cz : cx) + k * PITCH
            + side * (STREET_W / 2 + 1.6f);
        p.half = std::sqrt(std::max(
            1.0f, (CORE - 160) * (CORE - 160)
                      - (k * PITCH) * (k * PITCH)));
        p.speed = 1.1f + 0.9f * u(rng);
        p.phase = 3000 * u(rng);
        p.size = 0.9f + 0.2f * u(rng);
        p.hit_time = 0;
        p.hx = p.hy = p.hz = 0;
        p.hvx = p.hvy = p.hvz = 0;

        static const float shirts[6][3] = {
          { 0.85f, 0.2f, 0.15f }, { 0.2f, 0.4f, 0.8f },
          { 0.95f, 0.9f, 0.85f }, { 0.9f, 0.7f, 0.15f },
          { 0.3f, 0.65f, 0.35f }, { 0.55f, 0.3f, 0.6f },
        };
        const int s = (int)(u(rng) * 6) % 6;
        p.shirt = Vector3D(shirts[s][0], shirts[s][1], shirts[s][2]);
        const float tone = u(rng);
        p.skin = Vector3D(0.55f + 0.38f * tone,
                          0.40f + 0.32f * tone,
                          0.30f + 0.28f * tone);
        m_people.push_back(p);
      }
    }

    // Compile buildings and streets into display lists (GL context
    // required, so this runs after generate())
    void load_gl()
    {
      if (!m_map)
        return;

      std::mt19937 wrng(4321); // window lighting pattern
      const float cxx = map_size.x / 2, czz = map_size.z / 2;

      // gather lamp and trash can spots up front so they can be
      // bucketed into sectors along with the buildings
      std::vector<Vector3D> lamps, cans;

      int lamp_index = 0;
      for (int axis = 0; axis < 2; ++axis)
        for (int k = -20; k <= 20; ++k)
        {
          const float line = (axis ? cxx : czz) + k * PITCH;
          for (float s = -CORE; s <= CORE; s += 96)
          {
            const float side = (++lamp_index % 2) ? 1.0f : -1.0f;
            const float off = line + side * (STREET_W / 2 + 0.8f);
            const float wx = axis ? off : cxx + s;
            const float wz = axis ? czz + s : off;

            if (std::sqrt((wx - cxx) * (wx - cxx)
                          + (wz - czz) * (wz - czz)) > CORE - 100)
              continue;
            if (in_airport(wx, wz, 10))
              continue;
            lamps.push_back(Vector3D(
                wx, m_map->interpolated_height(wx, wz), wz));
          }
        }

      for (int ki = -20; ki <= 20; ++ki)
        for (int kj = -20; kj <= 20; ++kj)
        {
          if ((ki + 2 * kj) % 3 != 0)
            continue;
          const float wx = cxx + ki * PITCH + STREET_W / 2 + 1.0f;
          const float wz = czz + kj * PITCH + STREET_W / 2 + 1.2f;
          if (std::sqrt((wx - cxx) * (wx - cxx)
                        + (wz - czz) * (wz - czz)) > CORE - 120)
            continue;
          if (in_airport(wx, wz, 10))
            continue;
          cans.push_back(Vector3D(
              wx, m_map->interpolated_height(wx, wz), wz));
        }

      // Static geometry is bucketed into SEC x SEC sector display
      // lists so distant chunks of the city can be skipped whole
      for (int s = 0; s < SEC * SEC; ++s)
      {
        m_sectors[s] = glGenLists(1);
        glNewList(m_sectors[s], GL_COMPILE);

        for (size_t i = 0; i < m_buildings.size(); ++i)
        {
          const Building& b = m_buildings[i];
          if (sector_of((b.box.x0 + b.box.x1) / 2,
                        (b.box.z0 + b.box.z1) / 2) != s)
            continue;
          emit_building(b, wrng);
        }

        for (size_t i = 0; i < lamps.size(); ++i)
          if (sector_of(lamps[i].x, lamps[i].z) == s)
            emit_lamp(lamps[i]);

        for (size_t i = 0; i < cans.size(); ++i)
          if (sector_of(cans[i].x, cans[i].z) == s)
            emit_can(cans[i]);

        glEndList();
      }

      // streets draped over the terrain (so they follow ramps)
      const float cx = map_size.x / 2, cz = map_size.z / 2;
      m_streets_list = glGenLists(1);
      glNewList(m_streets_list, GL_COMPILE);
      glNormal3f(0, 1, 0);
      for (int axis = 0; axis < 2; ++axis)
        for (int k = -27; k <= 27; ++k)
        {
          const float line = (axis ? cx : cz) + k * PITCH;
          if (std::abs(k * PITCH) > CORE)
            continue;

          glColor3f(0.16f, 0.16f, 0.18f);
          glBegin(GL_QUAD_STRIP);
          for (float s = -CORE; s <= CORE; s += 16)
          {
            const float wx = axis ? line : cx + s;
            const float wz = axis ? cz + s : line;
            if (std::sqrt((wx - cx) * (wx - cx)
                          + (wz - cz) * (wz - cz)) > CORE - 30)
            {
              glEnd();
              glBegin(GL_QUAD_STRIP);
              continue;
            }
            const float y =
                m_map->interpolated_height(wx, wz) + 0.10f;
            if (axis)
            {
              glVertex3f(line - STREET_W / 2, y, wz);
              glVertex3f(line + STREET_W / 2, y, wz);
            }
            else
            {
              glVertex3f(wx, y, line - STREET_W / 2);
              glVertex3f(wx, y, line + STREET_W / 2);
            }
          }
          glEnd();
        }
      glEndList();

      build_furniture();
    }

    static void emit_lamp(const Vector3D& p)
    {
      glColor3f(0.25f, 0.26f, 0.28f);
      glPushMatrix();
      glTranslatef(p.x, p.y + 2.3f, p.z);
      box(0.16f, 4.6f, 0.16f);
      glPopMatrix();

      // warm lamp head, always lit
      glDisable(GL_LIGHTING);
      glColor3f(1.0f, 0.88f, 0.55f);
      glPushMatrix();
      glTranslatef(p.x, p.y + 4.7f, p.z);
      glutSolidSphere(0.32, 8, 6);
      glPopMatrix();
      glEnable(GL_LIGHTING);
    }

    static void emit_can(const Vector3D& p)
    {
      glColor3f(0.25f, 0.35f, 0.28f);
      glPushMatrix();
      glTranslatef(p.x, p.y + 0.45f, p.z);
      box(0.6f, 0.9f, 0.6f);
      glPopMatrix();

      glColor3f(0.16f, 0.2f, 0.17f);
      glPushMatrix();
      glTranslatef(p.x, p.y + 0.95f, p.z);
      box(0.68f, 0.1f, 0.68f);
      glPopMatrix();
    }

    // The airport and the stations: always-visible landmarks
    void build_furniture()
    {
      m_furniture_list = glGenLists(1);
      glNewList(m_furniture_list, GL_COMPILE);

      draw_station(m_police, false);
      draw_station(m_fire, true);

      // -- the airport: runway, centerline, apron, parked jet
      {
        const float ry = H_CITY + 0.12f;
        const float z0 = (m_air_z0 + m_air_z1) / 2 - 25;
        const float z1 = z0 + 50;

        glNormal3f(0, 1, 0);
        glColor3f(0.14f, 0.14f, 0.15f);
        glBegin(GL_QUADS);
        glVertex3f(m_air_x0, ry, z0);
        glVertex3f(m_air_x1, ry, z0);
        glVertex3f(m_air_x1, ry, z1);
        glVertex3f(m_air_x0, ry, z1);
        // apron by the runway start
        glVertex3f(m_air_x0, ry, z0 - 60);
        glVertex3f(m_air_x0 + 130, ry, z0 - 60);
        glVertex3f(m_air_x0 + 130, ry, z0);
        glVertex3f(m_air_x0, ry, z0);
        glEnd();

        // dashed centerline
        glColor3f(0.9f, 0.9f, 0.9f);
        glBegin(GL_QUADS);
        for (float x = m_air_x0 + 20; x < m_air_x1 - 20; x += 30)
        {
          const float mid = (z0 + z1) / 2;
          glVertex3f(x, ry + 0.03f, mid - 0.5f);
          glVertex3f(x + 12, ry + 0.03f, mid - 0.5f);
          glVertex3f(x + 12, ry + 0.03f, mid + 0.5f);
          glVertex3f(x, ry + 0.03f, mid + 0.5f);
        }
        glEnd();

        // one jet parked on the apron
        glPushMatrix();
        glTranslatef(m_air_x0 + 65, H_CITY + 2.2f, z0 - 30);
        glRotatef(90, 0, 1, 0);
        glScalef(1.6f, 1.6f, 1.6f);
        draw_plane();
        glPopMatrix();
      }

      glEndList();
    }

    void render(float time, const Vector3D& cam) const
    {
      if (!m_map)
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

      // Window quads face every direction; skip culling instead of
      // worrying about their winding
      glDisable(GL_CULL_FACE);

      // Only sectors within sight range get drawn at all; beyond
      // ~2km the fog has swallowed them anyway
      const float sec_size = map_size.x / SEC;
      const float reach = 1900.0f + sec_size * 0.71f;
      for (int sz = 0; sz < SEC; ++sz)
        for (int sx = 0; sx < SEC; ++sx)
        {
          const float dx = cam.x - (sx + 0.5f) * sec_size;
          const float dz = cam.z - (sz + 0.5f) * sec_size;
          if (dx * dx + dz * dz < reach * reach)
            glCallList(m_sectors[sz * SEC + sx]);
        }

      glCallList(m_streets_list);
      glCallList(m_furniture_list);

      for (size_t i = 0; i < m_cars.size(); ++i)
        draw_car(m_cars[i], time, cam);
      for (size_t i = 0; i < m_people.size(); ++i)
        draw_person(m_people[i], time, cam);

      draw_flying_plane(0, time);
      draw_flying_plane(1, time);
      for (int i = 0; i < 3; ++i)
        draw_helicopter(i, time);
      draw_parked_vehicles(time);
    }

  private:
    struct Building
    {
      mov::Box box;
      Vector3D color;
    };

    struct Car
    {
      bool along_x;
      bool active; // false once the player has taken it
      int kind;    // 0 civilian, 1 police, 2 fire truck
      float line, dir, speed, phase, half;
      Vector3D color;
    };

    struct Person
    {
      bool along_x;
      float line, dir, speed, phase, size, half;
      Vector3D shirt, skin;
      // set when the bike bowls them over: launch point + velocity
      float hit_time;
      float hx, hy, hz, hvx, hvy, hvz;
    };

    int sector_of(float x, float z) const
    {
      int sx = (int)(x / (map_size.x / SEC));
      int sz = (int)(z / (map_size.z / SEC));
      sx = std::max(0, std::min(SEC - 1, sx));
      sz = std::max(0, std::min(SEC - 1, sz));
      return sz * SEC + sx;
    }

    static void emit_building(const Building& b, std::mt19937& rng)
    {
      const float w = b.box.x1 - b.box.x0;
      const float d = b.box.z1 - b.box.z0;
      const float h = b.box.top - H_CITY;

      glColor3f(b.color.x, b.color.y, b.color.z);
      glPushMatrix();
      glTranslatef((b.box.x0 + b.box.x1) / 2, H_CITY + h / 2,
                   (b.box.z0 + b.box.z1) / 2);
      glScalef(w, h, d);
      glutSolidCube(1.0);
      glPopMatrix();

      // darker roof cap
      glColor3f(b.color.x * 0.5f, b.color.y * 0.5f,
                b.color.z * 0.5f);
      glPushMatrix();
      glTranslatef((b.box.x0 + b.box.x1) / 2, b.box.top + 0.25f,
                   (b.box.z0 + b.box.z1) / 2);
      glScalef(w + 0.6f, 0.5f, d + 0.6f);
      glutSolidCube(1.0);
      glPopMatrix();

      draw_windows(b, rng);

      // the front door (walkable on foot -- see Walker::collide)
      const float dx = (b.box.x0 + b.box.x1) / 2;
      glColor3f(0.09f, 0.08f, 0.09f);
      glNormal3f(0, 0, 1);
      glBegin(GL_QUADS);
      glVertex3f(dx - 1.2f, H_CITY, b.box.z1 + 0.06f);
      glVertex3f(dx + 1.2f, H_CITY, b.box.z1 + 0.06f);
      glVertex3f(dx + 1.2f, H_CITY + 2.6f, b.box.z1 + 0.06f);
      glVertex3f(dx - 1.2f, H_CITY + 2.6f, b.box.z1 + 0.06f);
      glEnd();

      emit_interior(b, rng);
    }

    // Somebody lives here: rug, table and chairs, sofa, TV, a
    // bookshelf full of colorful books, and a warm floor lamp
    static void emit_interior(const Building& b, std::mt19937& rng)
    {
      std::uniform_real_distribution<float> u(0.0f, 1.0f);
      const float ix = (b.box.x0 + b.box.x1) / 2;
      const float iz = (b.box.z0 + b.box.z1) / 2;
      const float f = H_CITY;

      // rug
      glColor3f(0.45f + 0.4f * u(rng), 0.25f + 0.3f * u(rng),
                0.3f + 0.3f * u(rng));
      glPushMatrix();
      glTranslatef(ix, f + 0.04f, iz + 1.5f);
      box(5.0f, 0.07f, 3.6f);
      glPopMatrix();

      // table with four legs
      glColor3f(0.5f, 0.36f, 0.2f);
      glPushMatrix();
      glTranslatef(ix, f + 0.75f, iz + 1.5f);
      box(1.8f, 0.1f, 1.1f);
      glPopMatrix();
      for (int lx = -1; lx <= 1; lx += 2)
        for (int lz = -1; lz <= 1; lz += 2)
        {
          glPushMatrix();
          glTranslatef(ix + lx * 0.75f, f + 0.35f,
                       iz + 1.5f + lz * 0.4f);
          box(0.1f, 0.7f, 0.1f);
          glPopMatrix();
        }

      // two chairs
      glColor3f(0.4f, 0.28f, 0.16f);
      for (int s = -1; s <= 1; s += 2)
      {
        glPushMatrix();
        glTranslatef(ix + s * 1.6f, f + 0.45f, iz + 1.5f);
        box(0.5f, 0.08f, 0.5f);
        glPopMatrix();
        glPushMatrix();
        glTranslatef(ix + s * 1.85f, f + 0.8f, iz + 1.5f);
        box(0.08f, 0.8f, 0.5f);
        glPopMatrix();
      }

      // sofa by the west wall, TV opposite
      glColor3f(0.2f + 0.5f * u(rng), 0.3f + 0.3f * u(rng), 0.55f);
      glPushMatrix();
      glTranslatef(b.box.x0 + 1.6f, f + 0.4f, iz - 1.5f);
      box(1.0f, 0.8f, 2.6f);
      glPopMatrix();
      glPushMatrix();
      glTranslatef(b.box.x0 + 1.15f, f + 0.85f, iz - 1.5f);
      box(0.3f, 1.0f, 2.6f);
      glPopMatrix();

      glColor3f(0.05f, 0.05f, 0.06f);
      glPushMatrix();
      glTranslatef(b.box.x1 - 1.4f, f + 1.3f, iz - 1.5f);
      box(0.15f, 1.2f, 2.0f);
      glPopMatrix();

      // bookshelf on the back wall
      glColor3f(0.35f, 0.24f, 0.14f);
      glPushMatrix();
      glTranslatef(ix - 2.0f, f + 1.1f, b.box.z0 + 0.6f);
      box(2.2f, 2.2f, 0.5f);
      glPopMatrix();
      for (int shelf = 0; shelf < 2; ++shelf)
        for (int bk = 0; bk < 5; ++bk)
        {
          glColor3f(0.3f + 0.65f * u(rng), 0.25f + 0.6f * u(rng),
                    0.3f + 0.6f * u(rng));
          glPushMatrix();
          glTranslatef(ix - 2.7f + bk * 0.36f,
                       f + 0.65f + shelf * 0.8f, b.box.z0 + 0.62f);
          box(0.24f, 0.55f, 0.32f);
          glPopMatrix();
        }

      // a warm lamp so it feels like home
      glColor3f(0.3f, 0.3f, 0.32f);
      glPushMatrix();
      glTranslatef(ix + 2.5f, f + 0.8f, iz - 2.5f);
      box(0.1f, 1.6f, 0.1f);
      glPopMatrix();
      glDisable(GL_LIGHTING);
      glColor3f(1.0f, 0.85f, 0.5f);
      glPushMatrix();
      glTranslatef(ix + 2.5f, f + 1.75f, iz - 2.5f);
      glutSolidSphere(0.28, 8, 6);
      glPopMatrix();
      glEnable(GL_LIGHTING);
    }

    // A grid of window quads on each facade, mostly dark glass
    // with the occasional lit one
    static void draw_windows(const Building& b, std::mt19937& rng)
    {
      std::uniform_real_distribution<float> u(0.0f, 1.0f);
      const float h = b.box.top - H_CITY;

      for (int f = 0; f < 4; ++f)
      {
        const bool xf = f < 2; // facade faces +-x
        const int sign = (f % 2) ? -1 : 1;

        const float w =
            xf ? b.box.z1 - b.box.z0 : b.box.x1 - b.box.x0;
        const int nc = (int)((w - 3.0f) / 4.2f);
        const int nf = (int)((h - 3.0f) / 4.2f);
        if (nc < 1 || nf < 1)
          continue;

        const float wall = xf ? (sign > 0 ? b.box.x1 : b.box.x0)
                              : (sign > 0 ? b.box.z1 : b.box.z0);
        const float off = wall + sign * 0.08f;
        const float a0 =
            (xf ? b.box.z0 + b.box.z1 : b.box.x0 + b.box.x1) / 2
            - (nc - 1) * 4.2f / 2;

        glNormal3f(xf ? (float)sign : 0.0f, 0.0f,
                   xf ? 0.0f : (float)sign);
        glBegin(GL_QUADS);
        for (int fy = 0; fy < nf; ++fy)
          for (int c = 0; c < nc; ++c)
          {
            if (u(rng) < 0.13f)
              glColor3f(1.0f, 0.9f, 0.55f); // someone's home
            else
            {
              const float t = 0.8f + 0.4f * u(rng);
              glColor3f(0.16f * t, 0.22f * t, 0.30f * t);
            }

            const float y0 = H_CITY + 2.2f + fy * 4.2f;
            const float y1 = y0 + 2.4f;
            const float a = a0 + c * 4.2f;
            const float aa = a - 1.0f, ab = a + 1.0f;

            if (xf)
            {
              glVertex3f(off, y0, aa);
              glVertex3f(off, y0, ab);
              glVertex3f(off, y1, ab);
              glVertex3f(off, y1, aa);
            }
            else
            {
              glVertex3f(aa, y0, off);
              glVertex3f(ab, y0, off);
              glVertex3f(ab, y1, off);
              glVertex3f(aa, y1, off);
            }
          }
        glEnd();
      }
    }

    static void box(float w, float h, float d)
    {
      glPushMatrix();
      glScalef(w, h, d);
      glutSolidCube(1.0);
      glPopMatrix();
    }

    void draw_car(const Car& c, float time,
                  const Vector3D& cam) const
    {
      if (!c.active)
        return; // the player drove off with this one

      const float cx = map_size.x / 2, cz = map_size.z / 2;
      const float s =
          fmod(c.phase + c.speed * time, 2 * c.half) - c.half;
      const float wx = c.along_x ? cx + c.dir * s : c.line;
      const float wz = c.along_x ? c.line : cz + c.dir * s;

      const float ddx = cam.x - wx, ddz = cam.z - wz;
      if (ddx * ddx + ddz * ddz > 900.0f * 900.0f)
        return; // too far to matter

      const float y = m_map->interpolated_height(wx, wz);

      gl::ScopedMatrixSaver m;
      glTranslatef(wx, y, wz);
      if (c.along_x)
        glRotatef(c.dir > 0 ? 90 : -90, 0, 1, 0);
      else if (c.dir < 0)
        glRotatef(180, 0, 1, 0);

      draw_car_model(c.kind, c.color, time, c.phase);
    }

    // The vehicle model at the origin, facing +z
    static void draw_car_model(int kind, const Vector3D& color,
                               float time, float flash_phase)
    {
      const bool truck = (kind == 2);

      glColor3f(color.x, color.y, color.z);
      glPushMatrix();
      glTranslatef(0, truck ? 0.8f : 0.55f, 0);
      box(truck ? 2.1f : 1.7f, truck ? 1.5f : 0.85f,
          truck ? 5.6f : 3.6f);
      glPopMatrix();

      glColor3f(0.2f, 0.25f, 0.3f);
      glPushMatrix();
      if (truck)
      {
        glTranslatef(0, 1.6f, 1.9f);
        box(1.9f, 0.7f, 1.4f);
      }
      else
      {
        glTranslatef(0, 1.15f, -0.2f);
        box(1.5f, 0.6f, 1.9f);
      }
      glPopMatrix();

      if (truck)
      {
        // the ladder on top
        glColor3f(0.75f, 0.76f, 0.78f);
        glPushMatrix();
        glTranslatef(0, 1.75f, -0.8f);
        glRotatef(-6, 1, 0, 0);
        box(0.5f, 0.12f, 4.4f);
        glPopMatrix();
      }

      glColor3f(0.08f, 0.08f, 0.1f);
      for (int lx = -1; lx <= 1; lx += 2)
        for (int lz = -1; lz <= 1; lz += 2)
        {
          glPushMatrix();
          glTranslatef(lx * (truck ? 1.0f : 0.8f), 0.3f,
                       lz * (truck ? 1.7f : 1.2f));
          box(0.25f, 0.6f, 0.65f);
          glPopMatrix();
        }

      // flashing light bar for police and fire
      if (kind != 0)
      {
        glDisable(GL_LIGHTING);
        const bool phase_a =
            fmod(time * 3.0f + flash_phase, 1.0f) < 0.5f;
        for (int s = -1; s <= 1; s += 2)
        {
          const bool blue = (s > 0) == phase_a;
          if (blue)
            glColor3f(0.2f, 0.4f, 1.0f);
          else
            glColor3f(1.0f, 0.15f, 0.1f);
          glPushMatrix();
          glTranslatef(s * 0.35f, truck ? 2.05f : 1.55f,
                       truck ? 2.0f : -0.2f);
          box(0.4f, 0.22f, 0.35f);
          glPopMatrix();
        }
        glEnable(GL_LIGHTING);
      }
    }

    // A station house: colored hall, stripe, doors, beacon, and a
    // helipad with an H on the police roof
    void draw_station(const mov::Box& s, bool fire) const
    {
      const float w = s.x1 - s.x0, d = s.z1 - s.z0;
      const float h = s.top - H_CITY;
      const float mx = (s.x0 + s.x1) / 2, mz = (s.z0 + s.z1) / 2;

      if (fire)
        glColor3f(0.72f, 0.18f, 0.12f);
      else
        glColor3f(0.55f, 0.62f, 0.72f);
      glPushMatrix();
      glTranslatef(mx, H_CITY + h / 2, mz);
      box(w, h, d);
      glPopMatrix();

      // stripe around the walls
      if (fire)
        glColor3f(0.95f, 0.85f, 0.2f);
      else
        glColor3f(0.15f, 0.3f, 0.8f);
      glPushMatrix();
      glTranslatef(mx, H_CITY + h * 0.62f, mz);
      box(w + 0.4f, 1.0f, d + 0.4f);
      glPopMatrix();

      // garage doors on the street face
      glColor3f(0.12f, 0.13f, 0.15f);
      const int doors = fire ? 3 : 1;
      for (int i = 0; i < doors; ++i)
      {
        glPushMatrix();
        glTranslatef(mx + (i - (doors - 1) / 2.0f) * 7.5f,
                     H_CITY + 2.3f, s.z1 - 0.1f);
        box(fire ? 5.5f : 4.2f, 4.6f, 0.6f);
        glPopMatrix();
      }

      // beacon pole on a corner
      glColor3f(0.2f, 0.2f, 0.22f);
      glPushMatrix();
      glTranslatef(s.x0 + 2, s.top + 1.0f, s.z0 + 2);
      box(0.14f, 2.0f, 0.14f);
      glPopMatrix();

      glDisable(GL_LIGHTING);
      if (fire)
        glColor3f(1.0f, 0.4f, 0.1f);
      else
        glColor3f(0.25f, 0.5f, 1.0f);
      glPushMatrix();
      glTranslatef(s.x0 + 2, s.top + 2.2f, s.z0 + 2);
      glutSolidSphere(0.35, 8, 6);
      glPopMatrix();
      glEnable(GL_LIGHTING);

      if (!fire)
      {
        // helipad: dark disc with a white H
        glNormal3f(0, 1, 0);
        glColor3f(0.13f, 0.13f, 0.14f);
        glBegin(GL_TRIANGLE_FAN);
        glVertex3f(mx, s.top + 0.06f, mz);
        for (int i = 0; i <= 20; ++i)
        {
          const float a = 6.2832f * i / 20;
          glVertex3f(mx + 5.5f * cos(a), s.top + 0.06f,
                     mz + 5.5f * sin(a));
        }
        glEnd();

        glColor3f(0.92f, 0.92f, 0.95f);
        for (int sgn = -1; sgn <= 1; sgn += 2)
        {
          glPushMatrix();
          glTranslatef(mx + sgn * 1.3f, s.top + 0.10f, mz);
          box(0.5f, 0.06f, 4.0f);
          glPopMatrix();
        }
        glPushMatrix();
        glTranslatef(mx, s.top + 0.10f, mz);
        box(2.1f, 0.06f, 0.5f);
        glPopMatrix();
      }
    }

    // A helicopter at the origin, facing +z
    static void draw_heli_model(const Vector3D& body,
                                float rotor_deg, bool flash,
                                float time)
    {
      glColor3f(body.x, body.y, body.z);
      {
        gl::ScopedMatrixSaver part;
        glTranslatef(0, 0, 0.3f);
        glScalef(1.1f, 0.95f, 1.7f);
        glutSolidSphere(1.0, 10, 8);
      }
      {
        gl::ScopedMatrixSaver part;
        glTranslatef(0, 0.15f, -2.6f);
        box(0.24f, 0.3f, 3.4f);
      }
      {
        gl::ScopedMatrixSaver part;
        glTranslatef(0, 0.7f, -4.1f);
        box(0.15f, 1.0f, 0.5f);
      }

      // skids
      glColor3f(0.2f, 0.2f, 0.22f);
      for (int s = -1; s <= 1; s += 2)
      {
        gl::ScopedMatrixSaver part;
        glTranslatef(s * 0.6f, -1.05f, 0.2f);
        box(0.09f, 0.09f, 2.4f);
      }

      // main rotor, two crossed blades
      glColor3f(0.12f, 0.12f, 0.14f);
      {
        gl::ScopedMatrixSaver part;
        glTranslatef(0, 1.05f, 0.3f);
        glRotatef(rotor_deg, 0, 1, 0);
        box(8.0f, 0.06f, 0.3f);
        glRotatef(90, 0, 1, 0);
        box(8.0f, 0.06f, 0.3f);
      }

      // tail rotor
      {
        gl::ScopedMatrixSaver part;
        glTranslatef(0.2f, 0.3f, -4.1f);
        glRotatef(rotor_deg * 3, 1, 0, 0);
        box(0.06f, 1.3f, 0.16f);
      }

      if (flash)
      {
        glDisable(GL_LIGHTING);
        if (fmod(time * 2.5f, 1.0f) < 0.5f)
          glColor3f(0.25f, 0.5f, 1.0f);
        else
          glColor3f(1.0f, 0.15f, 0.1f);
        gl::ScopedMatrixSaver part;
        glTranslatef(0, -0.6f, 0.9f);
        glutSolidSphere(0.18, 6, 5);
        glEnable(GL_LIGHTING);
      }
    }

    void draw_helicopter(int which, float time) const
    {
      const float cx = map_size.x / 2, cz = map_size.z / 2;
      static const float R[3] = { 500, 800, 1100 };
      static const float ALT[3] = { 130, 170, 210 };
      static const float W[3] = { 0.075f, 0.06f, 0.05f };

      const Vector3D colors[3] = {
        Vector3D(0.92, 0.93, 0.96), // police
        Vector3D(0.80, 0.15, 0.10), // rescue
        Vector3D(0.95, 0.80, 0.15), // traffic reporter
      };

      const float a = time * W[which] + which * 2.1f;

      gl::ScopedMatrixSaver m;
      glTranslatef(cx + cos(a) * R[which], H_CITY + ALT[which],
                   cz + sin(a) * R[which]);
      glRotatef(-a * 57.2958f, 0, 1, 0);
      glRotatef(-12, 0, 0, 1);
      glScalef(1.3f, 1.3f, 1.3f);
      draw_heli_model(colors[which], time * 1080, which < 2, time);
    }

    void draw_parked_vehicles(float time) const
    {
      // police cars out front, flashers going
      for (int i = 0; i < 2; ++i)
      {
        gl::ScopedMatrixSaver m;
        glTranslatef(m_police.x0 + 9 + i * 12, H_CITY,
                     m_police.z1 + 6);
        glRotatef(i ? 100.0f : 80.0f, 0, 1, 0);
        draw_car_model(1, Vector3D(0.92, 0.92, 0.95), time,
                       0.5f * i);
      }

      // the fire truck by its hall
      {
        gl::ScopedMatrixSaver m;
        glTranslatef((m_fire.x0 + m_fire.x1) / 2, H_CITY,
                     m_fire.z1 + 7);
        glRotatef(90, 0, 1, 0);
        draw_car_model(2, Vector3D(0.85, 0.1, 0.08), time, 0.3f);
      }

      // the police helicopter idling on its pad
      {
        gl::ScopedMatrixSaver m;
        glTranslatef((m_police.x0 + m_police.x1) / 2,
                     m_police.top + 1.16f,
                     (m_police.z0 + m_police.z1) / 2);
        glRotatef(35, 0, 1, 0);
        draw_heli_model(Vector3D(0.92, 0.93, 0.96), time * 80,
                        false, time);
      }
    }

    // Where a walking pedestrian is right now
    void person_pos(const Person& p, float time,
                    float& wx, float& wz) const
    {
      const float cx = map_size.x / 2, cz = map_size.z / 2;
      const float s =
          fmod(p.phase + p.speed * time, 2 * p.half) - p.half;
      wx = p.along_x ? cx + p.dir * s : p.line;
      wz = p.along_x ? p.line : cz + p.dir * s;
    }

    void draw_person_body(const Person& p, float swing) const
    {
      // legs swinging in opposite phase
      glColor3f(0.18f, 0.18f, 0.24f);
      for (int leg = -1; leg <= 1; leg += 2)
      {
        gl::ScopedMatrixSaver part;
        glTranslatef(leg * 0.09f, 0.78f, 0);
        glRotatef(swing * leg, 1, 0, 0);
        glTranslatef(0, -0.38f, 0);
        box(0.13f, 0.76f, 0.13f);
      }

      // torso
      glColor3f(p.shirt.x, p.shirt.y, p.shirt.z);
      {
        gl::ScopedMatrixSaver part;
        glTranslatef(0, 1.14f, 0);
        box(0.38f, 0.6f, 0.22f);
      }

      // arms, counter-swinging
      for (int arm = -1; arm <= 1; arm += 2)
      {
        gl::ScopedMatrixSaver part;
        glTranslatef(arm * 0.25f, 1.38f, 0);
        glRotatef(-swing * arm * 0.7f, 1, 0, 0);
        glTranslatef(0, -0.25f, 0);
        box(0.09f, 0.5f, 0.09f);
      }

      // head
      glColor3f(p.skin.x, p.skin.y, p.skin.z);
      {
        gl::ScopedMatrixSaver part;
        glTranslatef(0, 1.62f, 0);
        glutSolidSphere(0.13, 8, 6);
      }
    }

    void draw_person(const Person& p, float time,
                     const Vector3D& cam) const
    {
      {
        float wx, wz;
        person_pos(p, time, wx, wz);
        const float ddx = cam.x - wx, ddz = cam.z - wz;
        if (ddx * ddx + ddz * ddz > 450.0f * 450.0f)
          return; // a pixel at best
      }

      // Bowled over: fly in an arc, tumbling, then lie flat for a
      // moment before getting up again
      if (p.hit_time > 0)
      {
        const float t = time - p.hit_time;
        const float x = p.hx + p.hvx * t;
        const float z = p.hz + p.hvz * t;
        float y = p.hy + p.hvy * t - 4.9f * t * t;

        const float ground = m_map->interpolated_height(x, z);
        const bool flat = y < ground + 0.35f;
        if (flat)
          y = ground + 0.35f;

        gl::ScopedMatrixSaver m;
        glTranslatef(x, y, z);
        if (flat)
          glRotatef(90, 1, 0, 0);
        else
          glRotatef(t * 540.0f, 1, 0.4f, 0.2f);
        glScalef(p.size, p.size, p.size);
        glTranslatef(0, -0.9f, 0); // tumble about the body center
        draw_person_body(p, 0);
        return;
      }

      float wx, wz;
      person_pos(p, time, wx, wz);
      const float y = m_map->interpolated_height(wx, wz);

      gl::ScopedMatrixSaver m;
      glTranslatef(wx, y, wz);
      if (p.along_x)
        glRotatef(p.dir > 0 ? 90 : -90, 0, 1, 0);
      else if (p.dir < 0)
        glRotatef(180, 0, 1, 0);
      glScalef(p.size, p.size, p.size);

      draw_person_body(p, 30.0f * sin(time * 6.5f + p.phase));
    }

    bool in_airport(float x, float z, float margin) const
    {
      return x > m_air_x0 - margin && x < m_air_x1 + margin &&
             z > m_air_z0 - margin && z < m_air_z1 + margin;
    }

    // A stylized jet: fuselage, swept wings, tailplane, red fin
    static void draw_plane()
    {
      glColor3f(0.92f, 0.93f, 0.95f);
      {
        gl::ScopedMatrixSaver part;
        glScalef(1.1f, 1.1f, 8.5f);
        glutSolidSphere(1.0, 10, 8);
      }
      {
        gl::ScopedMatrixSaver part;
        glTranslatef(0, -0.2f, 0.5f);
        box(17.0f, 0.28f, 2.8f);
      }
      {
        gl::ScopedMatrixSaver part;
        glTranslatef(0, 0.3f, -6.8f);
        box(5.5f, 0.22f, 1.5f);
      }
      glColor3f(0.85f, 0.15f, 0.1f);
      {
        gl::ScopedMatrixSaver part;
        glTranslatef(0, 1.4f, -6.9f);
        box(0.22f, 2.4f, 1.7f);
      }
    }

    void draw_flying_plane(int which, float time) const
    {
      const float cx = map_size.x / 2, cz = map_size.z / 2;
      const float r = which ? 1300.0f : 900.0f;
      const float alt = which ? H_CITY + 380 : H_CITY + 280;
      const float w = which ? 0.042f : 0.055f;
      const float a = time * w + which * 3.14159f;

      gl::ScopedMatrixSaver m;
      glTranslatef(cx + cos(a) * r, alt, cz + sin(a) * r);
      glRotatef(-a * 57.2958f, 0, 1, 0);
      glRotatef(-15, 0, 0, 1); // bank into the turn
      glScalef(1.6f, 1.6f, 1.6f);
      draw_plane();
    }

    static const float PITCH;
    static const float STREET_W;
    static const float CORE;

    map::RandomHeightMap* m_map;
    std::vector<Building> m_buildings;
    std::vector<mov::Box> m_boxes;
    static const int SEC = 8;

    std::vector<Car> m_cars;
    std::vector<Person> m_people;
    GLuint m_sectors[SEC * SEC];
    GLuint m_streets_list;
    GLuint m_furniture_list;
    float m_air_x0, m_air_x1, m_air_z0, m_air_z1;
    mov::Box m_police, m_fire;
    Vector3D m_last_hit;
  };

  const float City::H_CITY = 45.0f;
  const float City::PITCH = 64.0f;
  const float City::STREET_W = 10.0f;
  const float City::CORE = 1750.0f;

  // On-foot mode: park the bike, stretch your legs, walk through
  // doors into buildings.  Toggled with the secret 7-5-R combo.
  class Walker
  {
  public:
    Walker()
        : m_pos(), m_heading(0, 0, 1), m_vy(0), m_turn(0),
          m_walk(0), m_anim(0), m_grounded(true)
    {
    }

    void spawn(const Vector3D& pos, const Vector3D& heading)
    {
      m_pos = pos;
      m_heading = Vector3D(heading.x, 0, heading.z);
      if (m_heading.length2() < 0.01f)
        m_heading = Vector3D(0, 0, 1);
      m_heading.normalize();
      m_vy = 0;
      m_turn = 0;
      m_walk = 0;
    }

    void set_turn(float t) { m_turn = t; }
    void set_walk(float w) { m_walk = w; }

    void jump()
    {
      if (m_grounded)
        m_vy = 5.5f;
    }

    void update(float dt, const map::HeightMap& map,
                const std::vector<mov::Box>& boxes)
    {
      if (std::abs(m_turn) > 0.01f)
        m_heading = Quaternion::rotate(m_heading, Vector3D(0, 1, 0),
                                       -m_turn * 2.4f * dt);

      float speed = 6.0f;
      if (m_pos.y < water_level + 0.5f)
        speed = 2.0f; // wading

      m_pos.x += m_heading.x * m_walk * speed * dt;
      m_pos.z += m_heading.z * m_walk * speed * dt;
      m_anim += std::abs(m_walk) * speed * dt;

      collide(boxes);

      // ground is the terrain, or a roof once we're up on one
      float g = map.interpolated_height(m_pos.x, m_pos.z);
      for (size_t i = 0; i < boxes.size(); ++i)
      {
        const mov::Box& b = boxes[i];
        if (m_pos.x > b.x0 && m_pos.x < b.x1 &&
            m_pos.z > b.z0 && m_pos.z < b.z1 &&
            m_pos.y > b.top - 1.0f && b.top > g)
          g = b.top;
      }

      m_vy -= 9.82f * dt;
      m_pos.y += m_vy * dt;
      m_grounded = false;
      if (m_pos.y <= g)
      {
        m_pos.y = g;
        m_vy = 0;
        m_grounded = true;
      }
    }

    Vector3D position() const { return m_pos; }
    Vector3D heading() const { return m_heading; }

    void render(float) const
    {
      gl::ScopedAttribSaver attribs(GL_ENABLE_BIT | GL_LIGHTING_BIT |
                                    GL_CURRENT_BIT | GL_TEXTURE_BIT);
      for (int unit = 3; unit >= 0; --unit)
      {
        glActiveTexture(GL_TEXTURE0 + unit);
        glDisable(GL_TEXTURE_2D);
      }
      glEnable(GL_COLOR_MATERIAL);
      glColorMaterial(GL_FRONT_AND_BACK, GL_AMBIENT_AND_DIFFUSE);

      gl::ScopedMatrixSaver m;
      glTranslatef(m_pos.x, m_pos.y, m_pos.z);
      glRotatef(atan2(m_heading.x, m_heading.z) * 57.2958f,
                0, 1, 0);

      const float swing =
          (std::abs(m_walk) > 0.01f) ? 32.0f * sin(m_anim * 3.6f)
                                     : 0.0f;

      // the rider, dismounted: same blue as the bike
      glColor3f(0.18f, 0.18f, 0.24f);
      for (int leg = -1; leg <= 1; leg += 2)
      {
        gl::ScopedMatrixSaver part;
        glTranslatef(leg * 0.09f, 0.78f, 0);
        glRotatef(swing * leg, 1, 0, 0);
        glTranslatef(0, -0.38f, 0);
        glPushMatrix();
        glScalef(0.13f, 0.76f, 0.13f);
        glutSolidCube(1.0);
        glPopMatrix();
      }

      glColor3f(0.15f, 0.5f, 1.0f);
      {
        gl::ScopedMatrixSaver part;
        glTranslatef(0, 1.14f, 0);
        glPushMatrix();
        glScalef(0.38f, 0.6f, 0.22f);
        glutSolidCube(1.0);
        glPopMatrix();
      }

      for (int arm = -1; arm <= 1; arm += 2)
      {
        gl::ScopedMatrixSaver part;
        glTranslatef(arm * 0.25f, 1.38f, 0);
        glRotatef(-swing * arm * 0.7f, 1, 0, 0);
        glTranslatef(0, -0.25f, 0);
        glPushMatrix();
        glScalef(0.09f, 0.5f, 0.09f);
        glutSolidCube(1.0);
        glPopMatrix();
      }

      // blue helmet, naturally: safety first
      glColor3f(0.15f, 0.45f, 1.0f);
      {
        gl::ScopedMatrixSaver part;
        glTranslatef(0, 1.62f, 0);
        glutSolidSphere(0.15, 10, 8);
      }
    }

  private:
    void collide(const std::vector<mov::Box>& boxes)
    {
      const float r = 0.4f;
      for (size_t i = 0; i < boxes.size(); ++i)
      {
        const mov::Box& b = boxes[i];
        if (m_pos.y >= b.top - 0.1f)
          continue; // up on the roof

        const float dx0 = m_pos.x - (b.x0 - r);
        const float dx1 = (b.x1 + r) - m_pos.x;
        const float dz0 = m_pos.z - (b.z0 - r);
        const float dz1 = (b.z1 + r) - m_pos.z;
        if (dx0 <= 0 || dx1 <= 0 || dz0 <= 0 || dz1 <= 0)
          continue;

        // every building has a door in the middle of its +z wall;
        // people fit through, motorcycles do not
        const float door_x = (b.x0 + b.x1) / 2;
        if (std::abs(m_pos.x - door_x) < 1.1f &&
            std::abs(m_pos.z - b.z1) < 1.8f)
          continue;

        const float px = std::min(dx0, dx1);
        const float pz = std::min(dz0, dz1);
        if (px < pz)
          m_pos.x = (dx0 < dx1) ? b.x0 - r : b.x1 + r;
        else
          m_pos.z = (dz0 < dz1) ? b.z0 - r : b.z1 + r;
      }
    }

    Vector3D m_pos, m_heading;
    float m_vy, m_turn, m_walk, m_anim;
    bool m_grounded;
  };

  class MoppeGLUT : public GLUTApplication
  {
  public:
    MoppeGLUT()
        : GLUTApplication("Moppe", 1000, 768),
          m_camera(18, 6.5 * one_meter),
          m_mouse(800, 600),
          m_map1(resolution, resolution,
                 map_size,
                 0 + ::time(0)),
          m_terrain_renderer(m_map1),
          m_vehicle(spawn_position(),
                    45, m_map1,
                    5000, 150),
          m_car(spawn_position(), 45, m_map1,
                14000, 900),
          m_sky("textures/sky.tga"),
          m_ocean(water_level,
                  Vector3D(map_size.x / 2, 0, map_size.z / 2),
                  pico_mode ? 0.55f * map_size.x
                            : 5500 * one_meter),
          m_uw_vert(GL_VERTEX_SHADER_ARB, "shaders/underwater.vert"),
          m_uw_frag(GL_FRAGMENT_SHADER_ARB, "shaders/underwater.frag"),
          m_last_shadow_update(-1.0f),
          m_total_time(0.0f),
          m_blur_tex(0),
          m_blur_valid(false),
          m_shake(0.0f),
          m_health(100.0f),
          m_fov_k(0.0f),
          m_lives(10),
          m_game_over(false),
          m_fuel(100.0f),
          m_odometer(0.0),
          m_go_input(0.0f),
          m_mode(M_BIKE),
          m_cam_mode(CAM_CHASE),
          m_car_exists(false),
          m_combo(0),
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
      // Scaled fixed-function models (bike, critters, city) need
      // their normals renormalized
      glEnable(GL_NORMALIZE);

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

      if (city_mode)
      {
        std::cout << "Building city..." << std::flush;
        m_city.generate(m_map1, 909);
      }
      else if (pico_mode)
      {
        std::cout << "Loading Pico Island DEM..." << std::flush;
        m_map1.load_raw_u16("data/pico.u16", 0.1f, map_size.y);
      }
      else
      {
        std::cout << "Generating terrain..." << std::flush;
        m_map1.randomize_geologically();
        // Slight lowland squash; ~10-15% ends up as ocean
        m_map1.exponentiate(1.15);
        std::cout << " eroding..." << std::flush;
        m_map1.erode_hydraulically(1500000);
        // Talus angle ~40 degrees at 2.4m cells, 650m height scale
        m_map1.erode_thermally(2, 0.003f);
      }
      m_map1.recompute_normals();
      std::cout << " done!\n";

      m_mouse.set_pitch_limits(-15, 10);

      m_terrain_renderer.regenerate();
      m_terrain_renderer.setup_shader();
      m_terrain_renderer.set_terrain_scales(map_size.y,
                                            water_level / map_size.y,
                                            fog_scale);

      std::cout << "Planting vegetation...";
      m_vegetation.generate(m_map1,
                            pico_mode ? 6000 : city_mode ? 500 : 2200,
                            pico_mode ? 4000 : city_mode ? 300 : 1500,
                            1234);
      std::cout << "done!\n";

      m_star_field.generate(m_map1,
                            pico_mode ? 250 : city_mode ? 130 : 80,
                            555);

      m_sky.load();
      m_ocean.load();
      m_ocean.set_fog_scale(fog_scale);
      m_vehicle.set_water_level(water_level);

      // Generation took seconds; don't hand them to physics as
      // one giant first step
      m_timer.reset();

      m_fish_school.generate(m_map1, water_level,
                             pico_mode ? 40 : 16, 777);
      m_wildlife.generate(m_map1, water_level,
                          pico_mode ? 24 : city_mode ? 3 : 8,
                          pico_mode ? 30 : 10, 4242);

      m_city.load_gl();
      m_vehicle.set_obstacles(&m_city.obstacles());
      m_car.set_obstacles(&m_city.obstacles());
      m_car.set_water_level(water_level);

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
      const float raw_elapsed = m_timer.elapsed();

      if (raw_elapsed >= dt)
      {
        m_timer.reset();

        if (m_game_over)
        {
          glutPostRedisplay();
          return;
        }

        // Clamp hitches: one giant Euler step tunnels through
        // walls and hurls the camera
        const float elapsed = std::min(raw_elapsed, 0.05f);
        m_total_time += elapsed;
        const float total_time = m_total_time;

        // Eternal golden afternoon: the sun stays fixed at a low,
        // raking angle that gives the terrain long real shadows
        static const float sun_height = 0.75f;

        m_sky.set_time(total_time); // keeps the clouds drifting
        m_sky.set_sun_height(sun_height);
        m_sky.set_sun_direction(sun_direction_for(sun_height));

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
        m_sky.set_fog_color(fog);

        // The GL light position must be re-specified each tick under
        // the current modelview so the sun stays fixed in the world
        update_dynamic_lighting(sun_height, 80.0f);

        // The sun never moves, so one shadow-map render lasts forever
        if (m_last_shadow_update < 0.0f)
        {
          m_terrain_renderer.set_shadow_strength(0.85f);
          m_terrain_renderer.update_shadow_map(
              sun_direction_for(sun_height) * -1.0f);
          m_last_shadow_update = total_time;
        }

        m_vehicle.update(elapsed);
        if (m_car_exists)
          m_car.update(elapsed);
        if (m_mode == M_FOOT)
          m_walker.update(elapsed, m_map1, m_city.obstacles());

        // -- gameplay feedback: dust, spray, stars, camera shake
        const Vector3D vpos =
            (m_mode == M_FOOT)  ? m_walker.position()
            : (m_mode == M_CAR) ? m_car.position()
                                : m_vehicle.position();
        mov::Vehicle& av = active_vehicle();

        // parked vehicles' impacts shouldn't linger until remount
        if (m_mode != M_BIKE)
        {
          m_vehicle.pop_impact();
          m_vehicle.pop_fall_drop();
        }
        if (m_car_exists && m_mode != M_CAR)
        {
          m_car.pop_impact();
          m_car.pop_fall_drop();
        }

        const bool in_water = vpos.y < water_level + 1.0f;
        const bool driving = (m_mode != M_FOOT);
        const Vector3D dust_color(0.72, 0.63, 0.48);
        const Vector3D spray_color(0.85, 0.92, 1.0);

        // Drift kicks up dirt from the rear wheel (or spray)
        if (driving && av.grounded() && av.drift_speed() > 6.0f)
        {
          Vector3D back = vpos - av.orientation() * 1.4f;
          back.y = vpos.y - 0.7f;
          int n = std::min(6, (int)(av.drift_speed() * 0.25f));
          m_dust.emit(back, av.velocity() * 0.15f, n,
                      in_water ? spray_color : dust_color);
        }

        // Wading fast throws up a bow wave
        if (driving && in_water && av.velocity().length() > 15.0f)
          m_dust.emit(vpos + Vector3D(0, -0.5, 0),
                      av.velocity() * 0.3f, 3, spray_color);

        // Hard landings shake the camera and burst dirt outward
        const float impact = driving ? av.pop_impact() : 0.0f;
        if (impact > 8.0f)
        {
          m_shake = std::min(0.45f, 0.018f * impact);
          m_dust.emit(vpos + Vector3D(0, -0.7, 0),
                      av.velocity() * 0.2f, 18,
                      in_water ? spray_color : dust_color);
        }

        // Crashes hurt; health trickles back slowly.  Falls from
        // above a hundred meters are simply fatal -- house rule.
        if (impact > 9.0f)
          m_health -= (impact - 9.0f) * 4.5f;
        if (driving && av.pop_fall_drop() > 100.0f)
          m_health = 0.0f;
        m_health = std::min(100.0f, m_health + 1.5f * elapsed);
        if (m_health <= 0.0f)
        {
          m_dust.emit(vpos, Vector3D(0, 6, 0), 40,
                      Vector3D(1.0, 0.5, 0.1));
          --m_lives;
          if (m_lives <= 0)
          {
            m_game_over = true;
          }
          else
          {
            // Halfway through the hearts, the game offers its
            // sympathies out loud
            if (m_lives == 5)
              std::system("say 'Ouchies. That hurts.' &");

            av.reset(spawn_position());
            m_health = 100.0f;
            m_shake = 1.0f;
          }
        }

        // Pedestrians get bowled over, with a puff of alarm
        if (city_mode)
        {
          if (m_city.update_people(vpos,
                                   driving ? av.velocity()
                                           : Vector3D(),
                                   m_total_time) > 0)
            m_dust.emit(m_city.last_hit(), Vector3D(0, 4, 0), 10,
                        Vector3D(0.95, 0.85, 0.4));
        }

        // Star pickups sparkle gold -- and top up the tank
        {
          const int picked = m_star_field.update(vpos, elapsed);
          if (picked > 0)
          {
            m_dust.emit(m_star_field.last_pickup(),
                        Vector3D(0, 3, 0), 16,
                        Vector3D(1.0, 0.85, 0.2));
            m_fuel = std::min(100.0f, m_fuel + 25.0f * picked);
          }
        }

        // Fuel: the throttle burns it; an empty tank limps along
        // at a third power (never fully stranded)
        if (driving)
        {
          m_fuel = std::max(
              0.0f, m_fuel - std::abs(av.thrust()) * 0.9f * elapsed);
          m_odometer += av.velocity().length() * elapsed;

          const float want =
              m_go_input * ((m_fuel <= 0.5f && m_go_input > 0)
                                ? 0.3f
                                : 1.0f);
          av.set_thrust(want);
        }

        m_dust.update(elapsed);
        m_shake *= std::exp(-4.0f * elapsed);

        if (m_cam_mode == CAM_HELMET)
        {
          // Ride inside the rider's head; lightly smoothed so
          // terrain bumps don't rattle the eyeballs
          Vector3D eye, look;
          if (m_mode == M_FOOT)
          {
            eye = m_walker.position() + Vector3D(0, 1.55, 0);
            look = m_walker.heading();
          }
          else
          {
            eye = av.position() + Vector3D(0, 0.95, 0)
                  + av.orientation() * 0.4f;
            look = av.orientation();
          }
          m_fp_eye = m_fp_eye
              + (eye - m_fp_eye)
                    * (1.0f - std::exp(-25.0f * elapsed));
          m_camera.place(m_fp_eye, m_fp_eye + look * 10.0f);
        }
        else
        {
          // Chase cam, or the same rig flipped to the front so it
          // swings around and films the rider head-on
          const float flip = (m_cam_mode == CAM_FRONT) ? -1.0f
                                                       : 1.0f;
          if (m_mode == M_FOOT)
            m_camera.update(m_walker.position() + Vector3D(0, 1, 0),
                            m_walker.heading() * flip, Vector3D(),
                            elapsed);
          else
            m_camera.update(av.position(),
                            av.orientation() * flip,
                            av.velocity(), elapsed);
          m_camera.limit(m_map1);
        }

        // Speed widens the field of view a touch
        {
          const float kmh = driving
              ? av.velocity().length() * 3.6f : 0.0f;
          const float k = std::min(
              1.0f, std::max(0.0f, (kmh - 70.0f) / 180.0f));
          m_fov_k += (k - m_fov_k)
              * (1.0f - std::exp(-5.0f * elapsed));
        }

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
      gluPerspective(100.0, 1.0 * width / height, 0.5,
                     pico_mode ? 30000 : 9000);
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

      const float kmh =
          (m_mode == M_FOOT)
              ? 0.0f
              : active_vehicle().velocity().length() * 3.6f;
      float k = (kmh - 90.0f) / 160.0f; // fades in 90, full at 250
      k = std::max(0.0f, std::min(1.0f, k));

      // Below the blur threshold, skip the full-screen copy too --
      // it costs an MSAA resolve every frame for nothing
      if (k <= 0.01f)
      {
        m_blur_valid = false;
        return;
      }

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
    // ---- instrument cluster drawing helpers -------------------

    static void hud_disc(float cx, float cy, float r,
                         float cr, float cg, float cb, float ca)
    {
      glColor4f(cr, cg, cb, ca);
      glBegin(GL_TRIANGLE_FAN);
      glVertex2f(cx, cy);
      for (int i = 0; i <= 40; ++i)
      {
        const float a = 2.0f * 3.14159f * i / 40;
        glVertex2f(cx + r * cos(a), cy + r * sin(a));
      }
      glEnd();
    }

    // Annulus segment; dial angle convention: 0 deg = +x, angles
    // increase counterclockwise ON SCREEN (y is down, so -sin)
    static void hud_ring(float cx, float cy, float r0, float r1,
                         float a0, float a1,
                         float cr, float cg, float cb, float ca)
    {
      glColor4f(cr, cg, cb, ca);
      glBegin(GL_TRIANGLE_STRIP);
      const int segs = 48;
      for (int i = 0; i <= segs; ++i)
      {
        const float a =
            (a0 + (a1 - a0) * i / segs) * 3.14159f / 180.0f;
        glVertex2f(cx + r1 * cos(a), cy - r1 * sin(a));
        glVertex2f(cx + r0 * cos(a), cy - r0 * sin(a));
      }
      glEnd();
    }

    // A tapered needle with a counterweight tail and a hub
    static void hud_needle(float cx, float cy, float a_deg,
                           float len, float tail, float w,
                           float cr, float cg, float cb)
    {
      const float a = a_deg * 3.14159f / 180.0f;
      const float dx = cos(a), dy = -sin(a);
      const float px = -dy, py = dx;

      glColor4f(cr, cg, cb, 0.96f);
      glBegin(GL_TRIANGLES);
      glVertex2f(cx + px * w, cy + py * w);
      glVertex2f(cx - px * w, cy - py * w);
      glVertex2f(cx + dx * len, cy + dy * len);
      glVertex2f(cx + px * w * 1.5f, cy + py * w * 1.5f);
      glVertex2f(cx - px * w * 1.5f, cy - py * w * 1.5f);
      glVertex2f(cx - dx * tail, cy - dy * tail);
      glEnd();

      hud_disc(cx, cy, w * 2.6f, 0.16f, 0.17f, 0.19f, 1.0f);
      hud_disc(cx, cy, w * 1.4f, 0.75f, 0.77f, 0.80f, 1.0f);
    }

    // Metallic bezel + dark face + glass highlight
    static void hud_dial_face(float cx, float cy, float R)
    {
      hud_ring(cx, cy, R * 0.94f, R * 1.08f, 0, 360,
               0.52f, 0.54f, 0.58f, 0.95f);
      hud_ring(cx, cy, R * 1.02f, R * 1.08f, 0, 360,
               0.24f, 0.25f, 0.27f, 0.95f);
      hud_disc(cx, cy, R * 0.96f, 0.06f, 0.07f, 0.09f, 0.93f);
      // glass reflection across the upper left
      hud_ring(cx, cy, R * 0.45f, R * 0.92f, 115, 205,
               1.0f, 1.0f, 1.0f, 0.055f);
    }

    // A little heart: two lobes and a point
    static void hud_heart(float x, float y, float s, bool full)
    {
      if (full)
        glColor4f(0.95f, 0.2f, 0.25f, 0.95f);
      else
        glColor4f(0.28f, 0.24f, 0.26f, 0.9f);

      glBegin(GL_TRIANGLES);
      glVertex2f(x - 0.72f * s, y - 0.02f * s);
      glVertex2f(x + 0.72f * s, y - 0.02f * s);
      glVertex2f(x, y + 0.9f * s);
      glEnd();

      // reuse the disc helper for the two lobes (color is set)
      glBegin(GL_TRIANGLE_FAN);
      glVertex2f(x - 0.36f * s, y - 0.25f * s);
      for (int i = 0; i <= 12; ++i)
      {
        const float a = 2.0f * 3.14159f * i / 12;
        glVertex2f(x - 0.36f * s + 0.42f * s * cos(a),
                   y - 0.25f * s + 0.42f * s * sin(a));
      }
      glEnd();
      glBegin(GL_TRIANGLE_FAN);
      glVertex2f(x + 0.36f * s, y - 0.25f * s);
      for (int i = 0; i <= 12; ++i)
      {
        const float a = 2.0f * 3.14159f * i / 12;
        glVertex2f(x + 0.36f * s + 0.42f * s * cos(a),
                   y - 0.25f * s + 0.42f * s * sin(a));
      }
      glEnd();
    }

    static void hud_lamp(float x, float y, float r, bool on,
                         float cr, float cg, float cb)
    {
      hud_disc(x, y, r + 2.5f, 0.10f, 0.11f, 0.12f, 0.95f);
      if (on)
      {
        hud_disc(x, y, r + 5.0f, cr, cg, cb, 0.25f); // glow
        hud_disc(x, y, r, cr, cg, cb, 0.98f);
      }
      else
        hud_disc(x, y, r, cr * 0.22f, cg * 0.22f, cb * 0.22f,
                 0.9f);
    }

    // ---- the cluster itself -----------------------------------

    void draw_hud()
    {
      const bool riding = (m_mode != M_FOOT);
      const float kmh =
          riding ? active_vehicle().velocity().length() * 3.6f
                 : 0.0f;
      const float frac = std::min(1.0f, kmh / 300.0f);
      const float charge =
          riding ? active_vehicle().rocket_charge() : 1.0f;
      const float PI = 3.14159265f;

      // In helmet view the cluster sits centered on a solid
      // backing, like the real instruments on the handlebars; in
      // third person it tucks translucently into the corner
      const bool helmet_hud = (m_cam_mode == CAM_HELMET);
      const float X = helmet_hud ? m_width * 0.5f + 235.0f
                                 : m_width - 14.0f;
      const float Y = m_height - (helmet_hud ? 4.0f : 10.0f);
      const float panel_alpha = helmet_hud ? 0.93f : 0.80f;

      // one slim horizontal strip along the bottom:
      // fuel dial, boost dial, lamp column, speedometer
      const float cx = X - 95.0f; // speedometer
      const float cy = Y - 78.0f;
      const float R = 66.0f;
      const float bx2 = X - 262.0f; // boost dial
      const float mx = X - 372.0f;  // fuel dial
      const float fy = Y - 72.0f;   // mini dial row
      const float mr = 40.0f;
      const float lampx = X - 180.0f;

      {
        gl::ScopedOrthographicMode ortho;
        gl::ScopedAttribSaver attribs(GL_ENABLE_BIT | GL_CURRENT_BIT |
                                      GL_LINE_BIT);

        // The ortho helper replaces the projection but leaves the
        // camera's world-space MODELVIEW in place -- without an
        // identity here, every HUD vertex is dragged through the
        // camera transform and lands off-screen.  (Text always
        // worked because draw_glut_text loads its own identity.)
        glMatrixMode(GL_MODELVIEW);
        gl::ScopedMatrixSaver mv;
        glLoadIdentity();

        glEnable(GL_BLEND);
        // The ortho mode's y-flip reverses polygon winding, so
        // culling would silently eat every filled HUD shape
        glDisable(GL_CULL_FACE);

        // The terrain leaves texture units 1-2 enabled; they would
        // tint the dials with stray texels (the ortho helper only
        // covers unit 0)
        for (int unit = 3; unit >= 0; --unit)
        {
          glActiveTexture(GL_TEXTURE0 + unit);
          glDisable(GL_TEXTURE_2D);
        }

        // -- dashboard panel behind everything
        {
          const float x0 = X - 460.0f, y0 = Y - 152.0f;
          const float x1 = X, y1 = Y;
          const float c = 20.0f;
          glColor4f(0.10f, 0.11f, 0.13f, panel_alpha);
          glBegin(GL_POLYGON);
          glVertex2f(x0 + c, y0);
          glVertex2f(x1 - c, y0);
          glVertex2f(x1, y0 + c);
          glVertex2f(x1, y1 - c);
          glVertex2f(x1 - c, y1);
          glVertex2f(x0 + c, y1);
          glVertex2f(x0, y1 - c);
          glVertex2f(x0, y0 + c);
          glEnd();
          // rim light along the top edge
          glLineWidth(2);
          glColor4f(0.55f, 0.60f, 0.66f, 0.4f);
          glBegin(GL_LINE_STRIP);
          glVertex2f(x0, y0 + c);
          glVertex2f(x0 + c, y0);
          glVertex2f(x1 - c, y0);
          glVertex2f(x1, y0 + c);
          glEnd();
        }

        // ==================== SPEEDOMETER ====================
        hud_dial_face(cx, cy, R);

        // redline zone 250-300 (speed s maps to 210 - 240*s/300)
        hud_ring(cx, cy, R * 0.86f, R * 0.95f, -30, 10,
                 0.8f, 0.10f, 0.08f, 0.5f);

        // live speed arc, green through amber to red
        if (frac > 0.003f)
        {
          const int segs = (int)(44 * frac) + 1;
          glBegin(GL_TRIANGLE_STRIP);
          for (int i = 0; i <= segs; ++i)
          {
            const float f = frac * i / segs;
            const float a = (210.0f - 240.0f * f) * PI / 180.0f;
            glColor4f(0.2f + 0.8f * f, 0.95f - 0.8f * f, 0.12f,
                      0.85f);
            glVertex2f(cx + 0.95f * R * cos(a),
                       cy - 0.95f * R * sin(a));
            glVertex2f(cx + 0.86f * R * cos(a),
                       cy - 0.86f * R * sin(a));
          }
          glEnd();
        }

        // graduations: minor every 15, major every 30
        glBegin(GL_LINES);
        for (int s = 0; s <= 300; s += 15)
        {
          const bool major = (s % 30 == 0);
          const float a =
              (210.0f - 240.0f * s / 300.0f) * PI / 180.0f;
          if (major)
            glColor4f(0.92f, 0.93f, 0.95f, 0.95f);
          else
            glColor4f(0.55f, 0.57f, 0.6f, 0.8f);
          const float r0 = major ? 0.72f : 0.77f;
          glVertex2f(cx + r0 * R * cos(a), cy - r0 * R * sin(a));
          glVertex2f(cx + 0.84f * R * cos(a),
                     cy - 0.84f * R * sin(a));
        }
        glEnd();

        // the needle
        hud_needle(cx, cy, 210.0f - 240.0f * frac,
                   0.80f * R, 0.20f * R, 3.6f,
                   0.95f, 0.22f, 0.12f);

        // odometer window
        {
          glColor4f(0.02f, 0.02f, 0.03f, 0.95f);
          glBegin(GL_QUADS);
          glVertex2f(cx - 32, cy + 22);
          glVertex2f(cx + 32, cy + 22);
          glVertex2f(cx + 32, cy + 38);
          glVertex2f(cx - 32, cy + 38);
          glEnd();
        }

        // ==================== FUEL GAUGE ====================
        hud_dial_face(mx, fy, mr);
        // low-fuel zone (left fifth of the 160..20 sweep)
        hud_ring(mx, fy, mr * 0.62f, mr * 0.88f, 132, 160,
                 0.85f, 0.35f, 0.05f, 0.5f);
        glBegin(GL_LINES);
        for (int i = 0; i <= 4; ++i)
        {
          const float a = (160.0f - 35.0f * i) * PI / 180.0f;
          glColor4f(0.9f, 0.9f, 0.95f, 0.9f);
          glVertex2f(mx + 0.66f * mr * cos(a),
                     fy - 0.66f * mr * sin(a));
          glVertex2f(mx + 0.86f * mr * cos(a),
                     fy - 0.86f * mr * sin(a));
        }
        glEnd();
        hud_needle(mx, fy, 160.0f - 140.0f * (m_fuel / 100.0f),
                   0.74f * mr, 0.2f * mr, 2.4f,
                   0.92f, 0.92f, 0.95f);

        // ==================== BOOST GAUGE ====================
        hud_dial_face(bx2, fy, mr);
        hud_ring(bx2, fy, mr * 0.62f, mr * 0.88f, 20,
                 20.0f + 140.0f * charge,
                 0.25f, 0.65f, 1.0f, 0.45f);
        glBegin(GL_LINES);
        for (int i = 0; i <= 4; ++i)
        {
          const float a = (160.0f - 35.0f * i) * PI / 180.0f;
          glColor4f(0.9f, 0.9f, 0.95f, 0.9f);
          glVertex2f(bx2 + 0.66f * mr * cos(a),
                     fy - 0.66f * mr * sin(a));
          glVertex2f(bx2 + 0.86f * mr * cos(a),
                     fy - 0.86f * mr * sin(a));
        }
        glEnd();
        hud_needle(bx2, fy, 160.0f - 140.0f * charge,
                   0.74f * mr, 0.2f * mr, 2.4f,
                   0.35f, 0.75f, 1.0f);

        // ==================== WARNING LAMPS ====================
        const bool blink_slow = fmod(m_total_time * 1.6f, 1.0f) < 0.6f;
        const bool blink_fast = fmod(m_total_time * 3.5f, 1.0f) < 0.5f;

        // boost ready (green), low fuel (amber), damage (red)
        hud_lamp(lampx, Y - 116.0f, 6.5f,
                 riding && charge >= 1.0f && blink_slow,
                 0.2f, 0.95f, 0.35f);
        hud_lamp(lampx, Y - 78.0f, 6.5f,
                 m_fuel < 20.0f && blink_slow,
                 1.0f, 0.6f, 0.05f);
        hud_lamp(lampx, Y - 40.0f, 6.5f,
                 m_health < 35.0f && blink_fast,
                 1.0f, 0.15f, 0.1f);

        // backing plate for the top-left corner readouts
        {
          const float x0 = 10, y0 = 8, x1 = 220, y1 = 114;
          const float c = 12.0f;
          glColor4f(0.10f, 0.11f, 0.13f, 0.62f);
          glBegin(GL_POLYGON);
          glVertex2f(x0 + c, y0);
          glVertex2f(x1 - c, y0);
          glVertex2f(x1, y0 + c);
          glVertex2f(x1, y1 - c);
          glVertex2f(x1 - c, y1);
          glVertex2f(x0 + c, y1);
          glVertex2f(x0, y1 - c);
          glVertex2f(x0, y0 + c);
          glEnd();
        }

        // health bar under the star counter
        {
          const float hx = 24, hy = 62;
          const float hw = 180, hh = 14;
          const float f = std::max(0.0f, m_health / 100.0f);

          glColor4f(0.05f, 0.08f, 0.12f, 0.6f);
          glBegin(GL_QUADS);
          glVertex2f(hx, hy);
          glVertex2f(hx + hw, hy);
          glVertex2f(hx + hw, hy + hh);
          glVertex2f(hx, hy + hh);
          glEnd();

          glColor4f(0.9f - 0.7f * f, 0.15f + 0.7f * f, 0.15f,
                    0.95f);
          glBegin(GL_QUADS);
          glVertex2f(hx, hy);
          glVertex2f(hx + hw * f, hy);
          glVertex2f(hx + hw * f, hy + hh);
          glVertex2f(hx, hy + hh);
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

        // ten lives, worn on the sleeve
        for (int i = 0; i < 10; ++i)
          hud_heart(26.0f + i * 19.0f, 96.0f, 8.5f, i < m_lives);
      }

      // ---- printed text (draw_glut_text manages its own ortho)

      // speed numbers around the dial
      glColor3f(0.88f, 0.9f, 0.93f);
      for (int s = 0; s <= 300; s += 60)
      {
        const float a = (210.0f - 240.0f * s / 300.0f) * PI / 180.0f;
        const std::string label = std::to_string(s);
        gl::draw_glut_text(
            GLUT_BITMAP_HELVETICA_10,
            (int)(cx + 0.58f * R * cos(a) - 3.0f * label.size()),
            (int)(cy - 0.58f * R * sin(a) + 3), label);
      }

      glColor3f(0.65f, 0.68f, 0.72f);
      gl::draw_glut_text(GLUT_BITMAP_HELVETICA_10,
                         (int)(cx - 13), (int)(cy - 18), "km/h");
      gl::draw_glut_text(GLUT_BITMAP_HELVETICA_10,
                         (int)(cx - 17), (int)(cy - 31), "MOPPE");

      // odometer digits
      {
        char buf[16];
        snprintf(buf, sizeof buf, "%07.1f", m_odometer / 1000.0);
        glColor3f(0.85f, 0.9f, 0.85f);
        gl::draw_glut_text(GLUT_BITMAP_8_BY_13,
                           (int)(cx - 28), (int)(cy + 34), buf);
      }

      // gauge labels: E/F on the fuel dial, BOOST below its twin
      glColor3f(0.8f, 0.3f, 0.2f);
      gl::draw_glut_text(GLUT_BITMAP_HELVETICA_10,
                         (int)(mx - mr * 0.95f), (int)(fy - mr * 0.24f),
                         "E");
      glColor3f(0.8f, 0.85f, 0.9f);
      gl::draw_glut_text(GLUT_BITMAP_HELVETICA_10,
                         (int)(mx + mr * 0.82f), (int)(fy - mr * 0.24f),
                         "F");
      glColor3f(0.65f, 0.68f, 0.72f);
      gl::draw_glut_text(GLUT_BITMAP_HELVETICA_10,
                         (int)(mx - 12), (int)(fy + 24), "FUEL");
      gl::draw_glut_text(GLUT_BITMAP_HELVETICA_10,
                         (int)(bx2 - 15), (int)(fy + 24), "BOOST");

      // star counter
      glColor3f(1.0f, 0.85f, 0.2f);
      gl::draw_glut_text(GLUT_BITMAP_TIMES_ROMAN_24, 60, 44,
                         "x " +
                         std::to_string(m_star_field.collected()));

      if (m_mode == M_FOOT)
      {
        glColor3f(0.6f, 0.9f, 1.0f);
        gl::draw_glut_text(GLUT_BITMAP_HELVETICA_12,
                           (int)(X - 450), (int)(Y - 138),
                           "ON FOOT");
      }
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

      // In great pain, only R (ride again) and ESC work
      if (m_game_over)
      {
        if (!special && pressed && (code == 'r' || code == 'R'))
          revive();
        else if (!special && code == 27)
          std::exit(0);
        return;
      }

      if (special)
      {
        switch (code)
        {
        case GLUT_KEY_LEFT:
          input_turn(-1 * factor);
          break;
        case GLUT_KEY_RIGHT:
          input_turn(1 * factor);
          break;
        case GLUT_KEY_UP:
          input_go(1 * factor);
          break;
        case GLUT_KEY_DOWN:
          input_go(-1 * factor);
          break;
        }
        return;
      }

      // The secret dismount combo: 7, then 5, then R
      if (pressed)
      {
        static const char want[3] = { '7', '5', 'r' };
        const char cc =
            (code >= 'A' && code <= 'Z') ? code + 32 : code;
        if (cc == want[m_combo])
        {
          if (++m_combo == 3)
          {
            m_combo = 0;
            toggle_mount();
          }
        }
        else
          m_combo = (cc == '7') ? 1 : 0;
      }

      switch (code)
      {
      case 9: // Tab cycles the camera: chase, front, helmet
        if (pressed)
        {
          m_cam_mode = (m_cam_mode + 1) % 3;
          if (m_cam_mode == CAM_HELMET)
            m_fp_eye = m_camera.position(); // glide in, not teleport
        }
        break;

      case 'a': case 'A':
        input_turn(-1 * factor);
        break;

      case 'd': case 'D':
        input_turn(1 * factor);
        break;

      case 'w': case 'W':
        input_go(1 * factor);
        break;

      case 's': case 'S':
        input_go(-1 * factor);
        break;

      case ' ': // Rocket jump when driving, a hop on foot
        if (pressed)
        {
          if (m_mode == M_FOOT)
            m_walker.jump();
          else
          {
            const float before = active_vehicle().rocket_charge();
            active_vehicle().rocket_jump();
            // A burn drinks a gulp of fuel
            if (before >= 1.0f &&
                active_vehicle().rocket_charge() < 1.0f)
              m_fuel = std::max(0.0f, m_fuel - 6.0f);
          }
        }
        break;

      case 27: // ESC quits the fullscreen game
        std::exit(0);
        break;
      }
    }

    mov::Vehicle& active_vehicle()
    {
      return m_mode == M_CAR ? m_car : m_vehicle;
    }

    void input_turn(float v)
    {
      if (m_mode == M_FOOT)
        m_walker.set_turn(v);
      else
        active_vehicle().set_yaw(90 * v);
    }

    void input_go(float v)
    {
      m_go_input = v;
      if (m_mode == M_FOOT)
        m_walker.set_walk(v > 0 ? v : v * 0.6f);
      else
        active_vehicle().set_thrust(v);
    }

    void toggle_mount()
    {
      if (m_mode != M_FOOT)
      {
        // step off to the side of whatever we're driving
        mov::Vehicle& av = active_vehicle();
        const Vector3D h = av.orientation();
        const Vector3D side(h.z, 0, -h.x);
        m_walker.spawn(av.position()
                           + side * (m_mode == M_CAR ? 2.4f : 1.8f),
                       h);
        av.set_thrust(0);
        av.set_yaw(0);
        m_mode = M_FOOT;
        return;
      }

      // on foot: bike first, then our parked car, then grand theft
      if ((m_walker.position() - m_vehicle.position()).length2()
          < 5.0f * 5.0f)
      {
        m_vehicle.set_thrust(0);
        m_vehicle.set_yaw(0);
        m_mode = M_BIKE;
        return;
      }

      if (m_car_exists &&
          (m_walker.position() - m_car.position()).length2()
              < 6.0f * 6.0f)
      {
        m_car.set_thrust(0);
        m_car.set_yaw(0);
        m_mode = M_CAR;
        return;
      }

      Vector3D cpos, cdir, ccolor;
      const int kind = m_city.take_car_near(
          m_walker.position(), 7.0f, m_total_time, cpos, cdir,
          ccolor);
      if (kind >= 0)
      {
        m_car.reset(cpos);
        m_car.set_heading(cdir);
        m_car.set_body_style(kind + 1, ccolor);
        m_car_exists = true;
        m_mode = M_CAR;
      }
    }

    void passive_motion(int x, int y)
    {
      m_mouse.update(x, y);
      glutPostRedisplay();
    }

    void render_scene()
    {
      // Speed-kicked field of view, re-specified per frame
      glMatrixMode(GL_PROJECTION);
      glLoadIdentity();
      gluPerspective(100.0 + 9.0 * m_fov_k,
                     1.0 * m_width / m_height, 0.5,
                     pico_mode ? 30000 : 9000);

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

      const Vector3D cam = m_camera.position();

      // Terrain first; only chunks within the haze horizon and not
      // behind the camera are drawn (3.0/fogScale is ~99.4% fogged,
      // so the cutoff is invisible against the sky's horizon color)
      m_terrain_renderer.render(cam, m_camera.forward(),
                                3.0f / fog_scale);

      // Sky AFTER the terrain: with depth testing on, the pricey
      // cloud shader only runs where sky is actually visible
      {
        gl::ScopedMatrixSaver matrix;
        gl::translate(cam);
        m_sky.render();
      }

      m_city.render(m_total_time, cam);
      m_vegetation.render(fog, fog_scale * 1.35f, cam);
      m_star_field.render(m_total_time, cam);
      m_terrain_renderer.translate();

      // soft blob shadows under the movers
      m_blob.draw(m_vehicle.position(), 2.2f, m_map1);
      if (m_car_exists)
        m_blob.draw(m_car.position(), 2.9f, m_map1);
      if (m_mode == M_FOOT)
        m_blob.draw(m_walker.position() + Vector3D(0, 0.5, 0),
                    0.8f, m_map1);

      // In helmet cam you ARE the rider: don't draw yourself
      const bool helmet = (m_cam_mode == CAM_HELMET);
      if (!(helmet && m_mode == M_BIKE))
        m_vehicle.render();
      if (m_car_exists && !(helmet && m_mode == M_CAR))
        m_car.render();
      if (m_mode == M_FOOT && !helmet)
        m_walker.render(m_total_time);
      m_fish_school.render(m_total_time, cam);
      m_wildlife.render(m_total_time, cam);

      // Translucent water goes last so the seabed, fish, and a
      // submerged bike show through it, then dust so spray sits on
      // top of the surface
      m_ocean.render(m_total_time, fog);
      m_dust.render();
    }

    void display()
    {
      if (m_game_over)
      {
        draw_game_over();
        glutSwapBuffers();
        return;
      }

      // Use dynamic fog color for background clear
      glClearColor(fog.x, fog.y, fog.z, 0);

      render_scene();
      apply_underwater();
      apply_motion_blur();
      draw_hud();

      glutSwapBuffers();
    }

    // The end, as specified by the design department
    void draw_game_over()
    {
      glClearColor(0, 0, 0, 1);
      glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);

      glColor3f(0.75f, 0.08f, 0.08f);
      gl::draw_glut_text(GLUT_BITMAP_TIMES_ROMAN_24,
                         m_width / 2 - 32, m_height / 2 - 30,
                         "Sorry.");
      gl::draw_glut_text(GLUT_BITMAP_TIMES_ROMAN_24,
                         m_width / 2 - 118, m_height / 2 + 10,
                         "You are in great pain.");

      glColor3f(0.35f, 0.35f, 0.4f);
      gl::draw_glut_text(GLUT_BITMAP_HELVETICA_12,
                         m_width / 2 - 66, m_height / 2 + 64,
                         "press R to ride again");
    }

    void revive()
    {
      m_lives = 10;
      m_health = 100.0f;
      m_fuel = 100.0f;
      m_shake = 0.0f;
      m_mode = M_BIKE;
      m_vehicle.reset(spawn_position());
      m_game_over = false;
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
    mov::Vehicle m_car; // the commandeered one, once it exists
    gfx::Sky m_sky;
    gfx::Ocean m_ocean;
    Vegetation m_vegetation;
    Stars m_star_field;
    Dust m_dust;
    BlobShadow m_blob;
    Fish m_fish_school;
    Wildlife m_wildlife;
    City m_city;

    gl::Shader m_uw_vert;
    gl::Shader m_uw_frag;
    gl::ShaderProgram m_uw_prog;

    float m_last_shadow_update;
    float m_total_time;

    GLuint m_blur_tex;
    bool m_blur_valid;

    float m_shake;
    float m_health;
    float m_fov_k;
    int m_lives;
    bool m_game_over;
    float m_fuel;
    double m_odometer; // meters ridden, ever
    float m_go_input;

    enum Mode { M_BIKE, M_FOOT, M_CAR };
    enum CamMode { CAM_CHASE, CAM_FRONT, CAM_HELMET };

    Walker m_walker;
    int m_mode;
    int m_cam_mode;
    Vector3D m_fp_eye;
    bool m_car_exists;
    int m_combo;

    std::mt19937 m_fx_rng;
  };
}

int main(int argc, char **argv)
{
  using namespace moppe;

  for (int i = 1; i < argc; ++i)
  {
    if (std::string(argv[i]) == "--pico")
    {
      // The real Pico Island: 49.4 km square, summit 2333m,
      // sea level slightly raised so the coast reads as ocean
      pico_mode = true;
      map_size = Vector3D(49400 * one_meter,
                          2400 * one_meter,
                          49400 * one_meter);
      water_level = 15 * one_meter;
      fog_scale = 0.00013f; // clear island air: Pico visible afar
    }
    else if (std::string(argv[i]) == "--city")
    {
      // Urban stunt island in a shallow sea
      city_mode = true;
      water_level = 15 * one_meter;
    }
  }

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
