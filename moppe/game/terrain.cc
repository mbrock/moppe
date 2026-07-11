#include <moppe/game/terrain.hh>
#include <moppe/gfx/tga.hh>

#include <algorithm>
#include <cmath>
#include <cstdlib>
#include <stdexcept>

#include <moppe/platform/platform.hh>

namespace moppe {
namespace game {
  namespace {
    const int CHUNK = 128;
    const int LOD_COUNT = (int) render::TerrainLod::Count;

    // Distances are expressed in source heightmap cells and scaled to
    // world units at setup.  Each end is also the point where the finer
    // level has completely morphed onto its parent surface.
    const float LOD_MORPH_START[LOD_COUNT - 1] = {
      64.0f, 288.0f, 576.0f, 1152.0f
    };
    const float LOD_END[LOD_COUNT - 1] = {
      96.0f, 384.0f, 768.0f, 1536.0f
    };

    render::TexturePtr
    load_tga (render::Renderer& r, const std::string& rel) {
      tga::TGAImg img;
      const std::string path = platform::asset_path (rel);
      if (img.Load (const_cast<char*> (path.c_str ())) != IMG_OK)
	throw std::runtime_error ("failed to load texture: " + path);

      render::TextureDesc desc;
      desc.width = img.GetWidth ();
      desc.height = img.GetHeight ();
      desc.format = img.GetBPP () == 32
	? render::TextureFormat::RGBA8 : render::TextureFormat::RGB8;
      desc.filter = render::TextureFilter::Mipmap;
      desc.wrap = render::TextureWrap::Repeat;
      desc.max_anisotropy = 8.0f;
      return r.create_texture (desc, img.GetImg ());
    }
  }

  void
  Terrain::setup (render::Renderer& r,
		  const map::RandomHeightMap& map,
		  const WorldParams& world,
		  render::TerrainProjection projection,
		  bool repeat_periodically,
		  bool interactive_preview) {
    m_scale = map.scale ();
    m_period = map.size ();
    m_periodic = map.periodic ();
    m_repeat_periodically = repeat_periodically;
    m_projection = projection;
    m_lod_scale = std::max (m_scale.x, m_scale.z);

    render::TerrainParams params;
    params.width = map.width ();
    params.height = map.height ();
    params.scale = m_scale;
    params.height_scale = world.map_size.y;
    params.sea_level_norm = world.water_level / world.map_size.y;
    params.tex_scale = 0.5f * one_meter / m_scale.x;
    // Debug: MOPPE_NOSHADOW=1 disables the cast-shadow lookup.
    params.shadow_strength = projection == render::TerrainProjection::Torus
      || ::getenv ("MOPPE_NOSHADOW") ? 0.0f : 0.85f;
    params.shadow_resolution = interactive_preview ? 1024 : 4096;
    params.shadow_sample_step = interactive_preview ? 2 : 1;
    params.fog_scale = world.fog_scale;
    params.periodic = map.periodic ();
    params.projection = projection;
    const float shortest_period = std::min (m_period.x, m_period.z);
    params.torus_major_radius = 0.34f * shortest_period;
    params.torus_minor_radius = 0.10f * shortest_period;
    params.torus_height_scale = 0.45f;
    params.derive_normals = interactive_preview;
    r.set_terrain (params, map.raw_heights (), map.raw_normals ());

    if (!m_textures_loaded) {
      m_grass = load_tga (r, "textures/grass3.tga");
      m_dirt = load_tga (r, "textures/dirt.tga");
      m_rock = load_tga (r, "textures/stones.tga");
      m_snow = load_tga (r, "textures/snow.tga");
      r.set_terrain_textures (m_grass, m_dirt, m_rock, m_snow);
      m_textures_loaded = true;
    }

    // Chunk bounding spheres from the actual height range.
    const int chunks_per_side = (map.width () - 1) / CHUNK;
    m_chunks.clear ();
    m_chunks.reserve ((size_t) chunks_per_side * chunks_per_side);
    for (int cz = 0; cz < chunks_per_side; ++cz)
      for (int cx = 0; cx < chunks_per_side; ++cx) {
	float ymin = -1.0f, ymax = 2.0f;
	if (!interactive_preview) {
	  ymin = 1e9f;
	  ymax = -1e9f;
	  for (int z = cz * CHUNK; z <= (cz + 1) * CHUNK; ++z)
	    for (int x = cx * CHUNK; x <= (cx + 1) * CHUNK; ++x) {
	      const float h = map.get (x, z);
	      ymin = std::min (ymin, h);
	      ymax = std::max (ymax, h);
	    }
	}
	ymin *= m_scale.y;
	ymax *= m_scale.y;

	Chunk c;
	c.x0 = cx * CHUNK;
	c.z0 = cz * CHUNK;
	const float x0 = c.x0 * m_scale.x;
	const float x1 = (c.x0 + CHUNK) * m_scale.x;
	const float z0 = c.z0 * m_scale.z;
	const float z1 = (c.z0 + CHUNK) * m_scale.z;
	c.center = Vector3D ((x0 + x1) / 2, (ymin + ymax) / 2,
			     (z0 + z1) / 2);
	const float hx = (x1 - x0) / 2, hy = (ymax - ymin) / 2,
	  hz = (z1 - z0) / 2;
	c.radius = std::sqrt (hx * hx + hy * hy + hz * hz);
	m_chunks.push_back (c);
      }
  }

  void
  Terrain::render_shadow (render::Renderer& r,
			  const map::HeightMap& map,
			  const Vector3D& sun_dir) {
    if (m_projection == render::TerrainProjection::Torus)
      return;
    const Vector3D bounds = map.size ();
    const Vector3D center (bounds.x / 2, bounds.y / 2, bounds.z / 2);
    const float radius = bounds.length () / 2;

    // Ortho box big enough for the whole scene: the light sits
    // radius*3.5 out toward the sun, so the scene spans roughly
    // [2.5r, 4.5r] in light depth (same numbers as the GL build).
    const Vector3D light_pos = center + sun_dir * (radius * 3.5f);
    const Mat4 view = Mat4::look_at (light_pos, center,
				     Vector3D (0, 1, 0));
    const Mat4 proj = Mat4::ortho (-radius, radius, -radius, radius,
				   radius * 0.5f, radius * 6.0f);
    r.render_terrain_shadow (proj * view);
  }

  void
  Terrain::render (render::Renderer& r, const Vector3D& cam,
		   const Vector3D& view_dir, float max_dist) {
    m_draws.clear ();

    if (m_projection == render::TerrainProjection::Torus) {
      m_draws.reserve (m_chunks.size ());
      for (const Chunk& chunk : m_chunks) {
	render::ChunkDraw draw;
	draw.x0 = static_cast<uint16_t> (chunk.x0);
	draw.z0 = static_cast<uint16_t> (chunk.z0);
	draw.lod = render::TerrainLod::Stride2;
	draw.morph_start = 0.0f;
	draw.morph_end = 0.0f;
	m_draws.push_back (draw);
      }
      if (!m_draws.empty ())
	r.draw_terrain (m_draws.data (), static_cast<int> (m_draws.size ()));
      return;
    }

    const float half_width = 0.5f * CHUNK * m_scale.x;
    const float half_depth = 0.5f * CHUNK * m_scale.z;
    for (size_t i = 0; i < m_chunks.size (); ++i) {
      const Chunk& c = m_chunks[i];
      const float reach = max_dist + c.radius;
      const bool repeat = m_periodic && m_repeat_periodically;
      const int min_tile_x = repeat ? static_cast<int> (std::ceil
	((cam.x - reach - c.center.x) / m_period.x)) : 0;
      const int max_tile_x = repeat ? static_cast<int> (std::floor
	((cam.x + reach - c.center.x) / m_period.x)) : 0;
      const int min_tile_z = repeat ? static_cast<int> (std::ceil
	((cam.z - reach - c.center.z) / m_period.z)) : 0;
      const int max_tile_z = repeat ? static_cast<int> (std::floor
	((cam.z + reach - c.center.z) / m_period.z)) : 0;

      for (int tile_z = min_tile_z; tile_z <= max_tile_z; ++tile_z)
	for (int tile_x = min_tile_x; tile_x <= max_tile_x; ++tile_x) {
	  const Vector3D offset (tile_x * m_period.x, 0,
				 tile_z * m_period.z);
	  const Vector3D d = c.center + offset - cam;
	  const float dist2 = d.length2 ();

	  // Too far: the haze has swallowed it.
	  if (dist2 > reach * reach)
	    continue;

	  // Entirely behind the camera plane (conservative).
	  if (dist2 > c.radius * c.radius &&
	      d.dot (view_dir) < -c.radius)
	    continue;

	  // Choose from the distance to the nearest point of the chunk,
	  // rather than its center.
	  const float dx = std::max
	    (0.0f, std::fabs (d.x) - half_width);
	  const float dz = std::max
	    (0.0f, std::fabs (d.z) - half_depth);
	  const float nearest = std::sqrt (dx * dx + dz * dz);

	  int lod = LOD_COUNT - 1;
	  for (int level = 0; level < LOD_COUNT - 1; ++level)
	    if (nearest < LOD_END[level] * m_lod_scale) {
	      lod = level;
	      break;
	    }

	  render::ChunkDraw draw;
	  draw.x0 = (uint16_t) c.x0;
	  draw.z0 = (uint16_t) c.z0;
	  draw.lod = (render::TerrainLod) lod;
	  draw.morph_start = lod < LOD_COUNT - 1
	    ? LOD_MORPH_START[lod] * m_lod_scale : 0.0f;
	  draw.morph_end = lod < LOD_COUNT - 1
	    ? LOD_END[lod] * m_lod_scale : 0.0f;
	  draw.offset_x = offset.x;
	  draw.offset_z = offset.z;
	  m_draws.push_back (draw);
	}
    }

    if (!m_draws.empty ())
      r.draw_terrain (&m_draws[0], (int) m_draws.size ());
  }
}
}
