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
    m_period = size;
    m_periodic = map.periodic ();
    m_collected = 0;

    m_stars.clear ();
    while ((int) m_stars.size () < count) {
      Star s;
      s.pos.x = size.x * (m_periodic ? u (rng)
				    : 0.03f + 0.94f * u (rng));
      s.pos.z = size.z * (m_periodic ? u (rng)
				    : 0.03f + 0.94f * u (rng));

      float ground = map.interpolated_height (s.pos.x, s.pos.z);
      if (ground < params.water_level + 2)
	continue; // land only

      // Every fourth star hangs high up: jump-jet territory
      bool high = (m_stars.size () % 4 == 0);
      s.pos.y = ground + (high ? 14.0f + 8.0f * u (rng) : 2.5f);
      s.home = s.pos;
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
	if (s.respawn <= 0)
	  s.pos = s.home;
	continue;
      }
      Vector3D delta = s.pos - vehicle_pos;
      if (m_periodic) {
	delta.x = terrain::minimum_image_delta (delta.x, m_period.x);
	delta.z = terrain::minimum_image_delta (delta.z, m_period.z);
      }
      const float distance = delta.length ();
      if (distance < 22.0f && distance > 0.001f) {
	// Once noticed, a star spirals into the rider. Attraction ramps up
	// smoothly at the edge and wins over the orbit near collection.
	const float pull = 1.0f - distance / 22.0f;
	const float alpha = 1.0f - std::exp
	  (-(1.2f + 7.0f * pull * pull) * dt);
	Vector3D tangent (-delta.z, 0, delta.x);
	if (tangent.length2 () > 0.0001f)
	  tangent.normalize ();
	const float orbit = 4.5f * pull * (1.0f - pull);
	s.pos -= delta * alpha;
	s.pos += tangent * (orbit * dt);
	s.phase += dt * (180.0f + 540.0f * pull);
	delta = s.pos - vehicle_pos;
	if (m_periodic) {
	  delta.x = terrain::minimum_image_delta (delta.x, m_period.x);
	  delta.z = terrain::minimum_image_delta (delta.z, m_period.z);
	}
      }
      if (delta.length2 () < 3.0f * 3.0f) {
	s.respawn = 60.0f; // comes back later
	++m_collected;
	++picked;
	m_last_pos = vehicle_pos + delta;
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

      Vector3D position = s.pos;
      if (m_periodic) {
	position.x = terrain::nearest_image (position.x, cam.x, m_period.x);
	position.z = terrain::nearest_image (position.z, cam.z, m_period.z);
      }
      const float dx = cam.x - position.x, dz = cam.z - position.z;
      if (dx * dx + dz * dz > 900.0f * 900.0f)
	continue;

      dl.push ();
      dl.translate (position.x,
		    position.y + 0.35f * std::sin (time * 2.0f + s.phase),
		    position.z);
      dl.rotate_deg (time * 150.0f + s.phase, 0, 1, 0);
      dl.rotate_deg (time * 95.0f + s.phase * 0.7f, 1, 0, 0);
      dl.color (1.0f, 0.85f, 0.15f);
      dl.torus (0.16f, 0.75f, 10, 18);
      dl.color (1.0f, 0.95f, 0.6f);
      dl.sphere (0.3f, 8, 8);

      // A breathing additive halo turns each pickup into a beacon
      // (the bloom pass picks it up from far off).
      {
	render::DrawState glow;
	glow.blend = true;
	glow.additive = true;
	glow.depth_write = false;
	dl.state (glow);
	const float pulse =
	  1.0f + 0.15f * std::sin (time * 2.0f + s.phase);
	dl.color (1.0f, 0.80f, 0.30f, 0.15f);
	dl.sphere (1.15f * pulse, 10, 8);
	dl.state (render::DrawState ());
      }
      dl.pop ();
    }
    dl.lit (true);
  }
}
}
