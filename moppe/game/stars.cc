#include <moppe/game/stars.hh>

#include <cmath>
#include <random>

namespace moppe {
namespace game {
  Stars::Stars ()
    : m_collected (0)
  { }

  void
  Stars::generate (const map::HeightMap& map,
		   const WorldParams& params, int count)
  {
    std::mt19937 rng (555);
    std::uniform_real_distribution<float> u (0.0f, 1.0f);
    const Vector3D size = map.size ();

    m_stars.clear ();
    while ((int) m_stars.size () < count) {
      Star s;
      s.pos.x = size.x * (0.03f + 0.94f * u (rng));
      s.pos.z = size.z * (0.03f + 0.94f * u (rng));

      float ground = map.interpolated_height (s.pos.x, s.pos.z);
      if (ground < params.water_level + 2)
	continue; // land only

      // Every fourth star hangs high up: rocket-jump territory
      bool high = (m_stars.size () % 4 == 0);
      s.pos.y = ground + (high ? 14.0f + 8.0f * u (rng) : 2.5f);
      s.phase = 360.0f * u (rng);
      s.respawn = 0;
      m_stars.push_back (s);
    }
  }

  int
  Stars::update (const Vector3D& vehicle_pos, float, float dt)
  {
    int picked = 0;
    for (size_t i = 0; i < m_stars.size (); ++i) {
      Star& s = m_stars[i];
      if (s.respawn > 0) {
	s.respawn -= dt;
	continue;
      }
      if ((s.pos - vehicle_pos).length2 () < 4.5f * 4.5f) {
	s.respawn = 60.0f; // comes back later
	++m_collected;
	++picked;
	m_last_pos = s.pos;
      }
    }
    return picked;
  }

  void
  Stars::render (render::DrawList& dl, const FrameEnv& env)
  {
    const Vector3D& cam = env.camera_pos;
    const float time = env.time;

    dl.set_texture (0); // the GL code disabled all texture units here
    dl.lit (false);
    for (size_t i = 0; i < m_stars.size (); ++i) {
      const Star& s = m_stars[i];
      if (s.respawn > 0)
	continue;

      const float dx = cam.x - s.pos.x, dz = cam.z - s.pos.z;
      if (dx * dx + dz * dz > 900.0f * 900.0f)
	continue;

      dl.push ();
      dl.translate (s.pos.x,
		    s.pos.y + 0.35f * std::sin (time * 2.0f + s.phase),
		    s.pos.z);
      dl.rotate_deg (time * 150.0f + s.phase, 0, 1, 0);
      dl.color (1.0f, 0.85f, 0.15f);
      dl.torus (0.16f, 0.75f, 10, 18);
      dl.color (1.0f, 0.95f, 0.6f);
      dl.sphere (0.3f, 8, 8);
      dl.pop ();
    }
    dl.lit (true);
  }
}
}
