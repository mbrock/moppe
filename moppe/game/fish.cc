#include <moppe/game/fish.hh>

#include <cmath>
#include <random>

namespace moppe {
namespace game {
  void
  Fish::generate (const map::HeightMap& map,
		  const WorldParams& params,
		  int schools) {
    if (schools < 0)
      schools = params.pico_mode ? 40 : 16;

    const float water = params.water_level;

    std::mt19937 rng (777);
    std::uniform_real_distribution<float> u (0.0f, 1.0f);
    const Vector3D size = map.size ();

    m_fish.clear ();
    int made = 0, tries = 0;
    while (made < schools && tries++ < schools * 300) {
      const float ax = size.x * (0.05f + 0.9f * u (rng));
      const float az = size.z * (0.05f + 0.9f * u (rng));
      const float ground = map.interpolated_height (ax, az);
      if (ground > water - 6.0f)
	continue; // schools want reasonably deep water

      const int n = 8 + (int) (u (rng) * 5);
      for (int i = 0; i < n; ++i) {
	One f;
	f.cx = ax + 12.0f * (u (rng) - 0.5f);
	f.cz = az + 12.0f * (u (rng) - 0.5f);
	f.y = ground + 1.5f
	  + (water - 3.0f - ground - 1.5f) * u (rng);
	f.radius = 3.0f + 6.0f * u (rng);
	f.speed = 0.5f + 0.7f * u (rng);
	f.phase = 6.2832f * u (rng);
	f.size = 0.6f + 0.8f * u (rng);
	f.hue = u (rng);
	m_fish.push_back (f);
      }
      ++made;
    }
  }

  void
  Fish::render (render::DrawList& dl, const FrameEnv& env) const {
    if (m_fish.empty ())
      return;

    const float time = env.time;
    const Vector3D& cam = env.camera_pos;

    for (size_t i = 0; i < m_fish.size (); ++i) {
      const One& f = m_fish[i];

      const float cdx = cam.x - f.cx, cdz = cam.z - f.cz;
      if (cdx * cdx + cdz * cdz > 600.0f * 600.0f)
	continue; // invisible underwater from that far

      const float a = f.phase + time * f.speed;

      dl.push ();
      dl.translate (f.cx + std::cos (a) * f.radius,
		    f.y + 0.3f * std::sin (time * 1.3f
					   + f.phase * 3.0f),
		    f.cz + std::sin (a) * f.radius);
      // face along the swim circle's tangent
      dl.rotate_deg (-a * 57.2958f, 0, 1, 0);
      dl.scale (f.size, f.size, f.size);

      // body: orange to silvery blue depending on the fish
      dl.color (1.0f - 0.3f * f.hue,
		0.5f + 0.3f * f.hue,
		0.15f + 0.75f * f.hue);
      dl.push ();
      dl.scale (0.28f, 0.22f, 0.6f);
      dl.sphere (1.0f, 8, 6);
      dl.pop ();

      // wiggling tail fin
      dl.push ();
      dl.translate (0, 0, -0.55f);
      dl.rotate_deg (30.0f * std::sin (time * 6.0f + f.phase),
		     0, 1, 0);
      dl.rotate_deg (180, 0, 1, 0);
      dl.cone (0.16f, 0.45f, 6, 2);
      dl.pop ();

      dl.pop ();
    }
  }
}
}
