#include <moppe/game/wildlife.hh>

#include <algorithm>
#include <cmath>
#include <random>

namespace moppe {
namespace game {
  void
  Wildlife::generate (const map::HeightMap& map,
		      const WorldParams& params,
		      int herds, int flocks) {
    if (herds < 0)
      herds = params.pico_mode ? 24 : params.city_mode ? 3 : 8;
    if (flocks < 0)
      flocks = params.pico_mode ? 30 : 10;

    const float water = params.water_level;

    std::mt19937 rng (4242);
    std::uniform_real_distribution<float> u (0.0f, 1.0f);
    const Vector3D size = map.size ();

    m_horses.clear ();
    int made = 0, tries = 0;
    while (made < herds && tries++ < herds * 400) {
      const float hx = size.x * (0.05f + 0.9f * u (rng));
      const float hz = size.z * (0.05f + 0.9f * u (rng));
      const float hy = map.interpolated_height (hx, hz);

      // Herds like flat grassy meadows
      if (hy < water + 8.0f || hy > 0.30f * params.map_size.y)
	continue;
      if (map.interpolated_normal (hx, hz).y < 0.88f)
	continue;

      const int n = 3 + (int) (u (rng) * 4);
      for (int i = 0; i < n; ++i) {
	Horse h;
	h.x = hx + 18.0f * (u (rng) - 0.5f);
	h.z = hz + 18.0f * (u (rng) - 0.5f);
	h.y = map.interpolated_height (h.x, h.z);
	h.heading = 360.0f * u (rng);
	h.phase = 6.2832f * u (rng);
	h.size = 0.85f + 0.3f * u (rng);
	h.tone = u (rng);
	m_horses.push_back (h);
      }
      ++made;
    }

    m_birds.clear ();
    for (int f = 0; f < flocks; ++f) {
      const float fx = size.x * (0.05f + 0.9f * u (rng));
      const float fz = size.z * (0.05f + 0.9f * u (rng));
      const float ground = map.interpolated_height (fx, fz);
      const bool over_sea = ground < water;
      const float alt =
	std::max (ground, water) + 35.0f + 70.0f * u (rng);

      const int n = 6 + (int) (u (rng) * 5);
      for (int i = 0; i < n; ++i) {
	Bird b;
	b.cx = fx;
	b.cz = fz;
	b.y = alt + 8.0f * (u (rng) - 0.5f);
	b.radius = 18.0f + 22.0f * u (rng);
	b.speed = 0.25f + 0.2f * u (rng);
	b.offset = 6.2832f * u (rng);
	b.flap = 6.2832f * u (rng);
	b.size = 0.8f + 0.5f * u (rng);
	b.gull = over_sea;
	m_birds.push_back (b);
      }
    }
  }

  void
  Wildlife::render (render::DrawList& dl, const FrameEnv& env) const {
    if (m_horses.empty () && m_birds.empty ())
      return;

    const float time = env.time;
    const Vector3D& cam = env.camera_pos;

    for (size_t i = 0; i < m_horses.size (); ++i) {
      const Horse& h = m_horses[i];
      const float dx = cam.x - h.x, dz = cam.z - h.z;
      if (dx * dx + dz * dz > 700.0f * 700.0f)
	continue;
      draw_horse (dl, h, time);
    }

    // Birds read best as flat silhouettes, visible from below too
    dl.lit (false);
    render::DrawState no_cull;
    no_cull.cull = false;
    dl.state (no_cull);
    for (size_t i = 0; i < m_birds.size (); ++i) {
      const Bird& b = m_birds[i];
      const float dx = cam.x - b.cx, dz = cam.z - b.cz;
      if (dx * dx + dz * dz > 1300.0f * 1300.0f)
	continue;
      draw_bird (dl, b, time);
    }
    dl.lit (true);
    dl.state (render::DrawState ());
  }

  void
  Wildlife::box (render::DrawList& dl, float w, float h, float d) {
    dl.push ();
    dl.scale (w, h, d);
    dl.cube (1.0f);
    dl.pop ();
  }

  void
  Wildlife::blob (render::DrawList& dl,
		  float rx, float ry, float rz) {
    dl.push ();
    dl.scale (rx, ry, rz);
    dl.sphere (1.0f, 10, 8);
    dl.pop ();
  }

  void
  Wildlife::draw_horse (render::DrawList& dl, const Horse& h,
			float time) {
    dl.push ();
    dl.translate (h.x, h.y, h.z);
    dl.rotate_deg (h.heading, 0, 1, 0);
    dl.scale (h.size, h.size, h.size);

    // coat: bay through chestnut, with the odd gray one
    if (h.tone < 0.75f)
      dl.color (0.30f + 0.35f * h.tone,
		0.20f + 0.20f * h.tone,
		0.13f + 0.12f * h.tone);
    else
      dl.color (0.82f, 0.80f, 0.76f);

    // body
    dl.push ();
    dl.translate (0, 1.05f, 0);
    blob (dl, 0.30f, 0.34f, 0.75f);
    dl.pop ();

    // legs
    for (int lx = -1; lx <= 1; lx += 2)
      for (int lz = -1; lz <= 1; lz += 2) {
	dl.push ();
	dl.translate (lx * 0.16f, 0.42f, lz * 0.45f);
	box (dl, 0.10f, 0.84f, 0.10f);
	dl.pop ();
      }

    // neck and head, dipping down to graze and back up
    dl.push ();
    dl.translate (0, 1.25f, 0.6f);
    const float dip = 0.5f + 0.5f * std::sin (time * 0.7f + h.phase);
    dl.rotate_deg (-45.0f + 92.0f * dip * dip, 1, 0, 0);
    dl.push ();
    dl.translate (0, 0, 0.3f);
    blob (dl, 0.11f, 0.14f, 0.42f);
    dl.pop ();
    dl.push ();
    dl.translate (0, 0.02f, 0.72f);
    blob (dl, 0.09f, 0.12f, 0.26f);
    dl.pop ();
    dl.pop ();

    // swishing tail
    dl.push ();
    dl.translate (0, 1.2f, -0.78f);
    dl.rotate_deg (14.0f * std::sin (time * 2.3f + h.phase),
		   0, 0, 1);
    dl.translate (0, -0.25f, 0);
    dl.color (0.15f, 0.12f, 0.10f);
    blob (dl, 0.06f, 0.30f, 0.07f);
    dl.pop ();

    dl.pop ();
  }

  void
  Wildlife::draw_bird (render::DrawList& dl, const Bird& b,
		       float time) {
    const float a = b.offset + time * b.speed;

    dl.push ();
    dl.translate (b.cx + std::cos (a) * b.radius,
		  b.y + 1.5f * std::sin (time * 0.9f + b.flap),
		  b.cz + std::sin (a) * b.radius);
    dl.rotate_deg (-a * 57.2958f, 0, 1, 0);
    dl.rotate_deg (-18.0f, 0, 0, 1); // bank into the circle
    dl.scale (b.size, b.size, b.size);

    if (b.gull)
      dl.color (0.92f, 0.93f, 0.95f);
    else
      dl.color (0.13f, 0.12f, 0.14f);

    dl.push ();
    dl.scale (0.10f, 0.09f, 0.30f);
    dl.sphere (1.0f, 6, 5);
    dl.pop ();

    // flapping wing triangles
    const float wy = 0.40f * std::sin (time * 9.0f + b.flap);
    dl.begin (render::Prim::Triangles);
    dl.vertex (0, 0, 0.12f);
    dl.vertex (0, 0, -0.10f);
    dl.vertex (-0.55f, wy, -0.02f);
    dl.vertex (0, 0, 0.12f);
    dl.vertex (0, 0, -0.10f);
    dl.vertex (0.55f, wy, -0.02f);
    dl.end ();

    dl.pop ();
  }
}
}
