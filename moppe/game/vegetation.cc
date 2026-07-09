#include <moppe/game/vegetation.hh>

#include <algorithm>
#include <random>

namespace moppe {
namespace game {
  namespace {
    // Cube trunk + cone canopy; exact transforms and colors of the
    // GL build's draw_tree.
    void
    record_tree (render::DrawList& dl, float x, float y, float z,
		 float s, float tint)
    {
      dl.push ();
      dl.translate (x, y, z);
      dl.scale (s, s, s);

      dl.color (0.42f, 0.28f, 0.14f);
      dl.push ();
      dl.translate (0, 1.2f, 0);
      dl.scale (0.5f, 2.4f, 0.5f);
      dl.cube (1.0f);
      dl.pop ();

      dl.color (0.08f + 0.1f * tint, 0.35f + 0.15f * tint, 0.1f);
      dl.push ();
      dl.translate (0, 2.0f, 0);
      dl.rotate_deg (-90, 1, 0, 0);
      dl.cone (2.2f, 5.5f, 8, 2);
      dl.pop ();

      dl.pop ();
    }

    void
    record_bush (render::DrawList& dl, float x, float y, float z,
		 float s, float tint)
    {
      dl.push ();
      dl.translate (x, y + 0.3f * s, z);
      dl.color (0.1f + 0.12f * tint, 0.3f + 0.18f * tint, 0.08f);
      dl.scale (1.4f * s, 0.9f * s, 1.4f * s);
      dl.sphere (1.0f, 8, 6);
      dl.pop ();
    }
  }

  void
  Vegetation::generate (const map::HeightMap& map,
			const WorldParams& world, int count)
  {
    const int bushes =
      world.pico_mode ? 4000 : world.city_mode ? 300 : 1500;
    generate (map, world, count, bushes);
  }

  void
  Vegetation::generate (const map::HeightMap& map,
			const WorldParams& world,
			int trees, int bushes)
  {
    std::mt19937 rng (1234);
    std::uniform_real_distribution<float> u (0.0f, 1.0f);

    const Vector3D size = map.size ();
    m_map_size = world.map_size;
    m_plants.clear ();

    int placed = 0;
    for (int i = 0; i < trees * 20 && placed < trees; ++i) {
      float x = size.x * (0.02f + 0.96f * u (rng));
      float z = size.z * (0.02f + 0.96f * u (rng));
      float y = map.interpolated_height (x, z);

      // Trees like gentle grassy ground below the rock line,
      // and nothing grows in the ocean
      if (y > 0.32f * world.map_size.y) continue;
      if (y < world.water_level + 5) continue;
      if (map.interpolated_normal (x, z).y < 0.8f) continue;

      Plant p;
      p.tree = true;
      p.x = x; p.y = y; p.z = z;
      p.s = 0.8f + 1.0f * u (rng);
      p.tint = u (rng);
      m_plants.push_back (p);
      ++placed;
    }

    placed = 0;
    for (int i = 0; i < bushes * 20 && placed < bushes; ++i) {
      float x = size.x * (0.02f + 0.96f * u (rng));
      float z = size.z * (0.02f + 0.96f * u (rng));
      float y = map.interpolated_height (x, z);

      // Bushes climb a little higher and tolerate more slope
      if (y > 0.45f * world.map_size.y) continue;
      if (y < world.water_level + 5) continue;
      if (map.interpolated_normal (x, z).y < 0.72f) continue;

      Plant p;
      p.tree = false;
      p.x = x; p.y = y; p.z = z;
      p.s = 0.6f + 0.8f * u (rng);
      p.tint = u (rng);
      m_plants.push_back (p);
      ++placed;
    }
  }

  int
  Vegetation::sector_of (float x, float z) const
  {
    int sx = (int) (x / (m_map_size.x / VSEC));
    int sz = (int) (z / (m_map_size.z / VSEC));
    sx = std::max (0, std::min (VSEC - 1, sx));
    sz = std::max (0, std::min (VSEC - 1, sz));
    return sz * VSEC + sx;
  }

  void
  Vegetation::load (render::Renderer& r)
  {
    for (int s = 0; s < VSEC * VSEC; ++s) {
      render::DrawList dl;
      for (size_t i = 0; i < m_plants.size (); ++i) {
	const Plant& p = m_plants[i];
	if (sector_of (p.x, p.z) != s)
	  continue;
	if (p.tree)
	  record_tree (dl, p.x, p.y, p.z, p.s, p.tint);
	else
	  record_bush (dl, p.x, p.y, p.z, p.s, p.tint);
      }
      m_meshes[s] = dl.empty ()
	? render::MeshPtr () : r.create_mesh (dl);
    }
  }

  void
  Vegetation::render (render::Renderer& r, const FrameEnv& env)
  {
    if (m_map_size.x <= 0)
      return;

    // Only sectors within fog-visibility range get drawn.  The GL
    // build fogged vegetation at fog_scale * 1.35; the unified haze
    // replaces the fog, but the factor lives on in the reach.
    const float density = env.fog_scale * 1.35f;
    const float sec = m_map_size.x / VSEC;
    const float reach = 1.9f / density + sec * 0.71f;

    for (int sz = 0; sz < VSEC; ++sz)
      for (int sx = 0; sx < VSEC; ++sx) {
	const float dx = env.camera_pos.x - (sx + 0.5f) * sec;
	const float dz = env.camera_pos.z - (sz + 0.5f) * sec;
	if (dx * dx + dz * dz >= reach * reach)
	  continue;
	const render::MeshPtr& mesh = m_meshes[sz * VSEC + sx];
	if (mesh)
	  r.draw_mesh (*mesh, Mat4 ());
      }
  }
}
}
