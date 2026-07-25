#include <moppe/game/terrain.hh>
#include <moppe/gfx/tga.hh>
#include <moppe/profile.hh>

#include <algorithm>
#include <cmath>
#include <cstdlib>
#include <stdexcept>

#include <moppe/platform/platform.hh>

namespace moppe {
  namespace game {
    namespace {
      const int CHUNK = 128;
      const int LOD_COUNT = (int)render::TerrainLod::Count;

      // Distances are expressed in source terrain cells and scaled to
      // world units at setup.  Each end is also the point where the finer
      // level has completely morphed onto its parent surface.
      const float LOD_MORPH_START[LOD_COUNT - 1] = {
        64.0f, 288.0f, 576.0f, 1152.0f
      };
      const float LOD_END[LOD_COUNT - 1] = { 96.0f, 384.0f, 768.0f, 1536.0f };

      render::TexturePtr load_tga (render::Renderer& r,
                                   const std::string& rel) {
        MOPPE_PROFILE_ZONE ("terrain.load_material_texture");
        tga_image::TGAImg img;
        const std::string path = platform::asset_path (rel);
        if (img.Load (const_cast<char*> (path.c_str ())) != IMG_OK)
          throw std::runtime_error ("failed to load texture: " + path);

        render::TextureDesc desc;
        desc.width = img.GetWidth ();
        desc.height = img.GetHeight ();
        desc.format = img.GetBPP () == 32 ? render::TextureFormat::RGBA8
                                          : render::TextureFormat::RGB8;
        desc.filter = render::TextureFilter::Mipmap;
        desc.wrap = render::TextureWrap::Repeat;
        desc.max_anisotropy = 8.0f;
        return r.create_texture (desc, img.GetImg ());
      }
    }

    void Terrain::setup (render::Renderer& r,
                         const map::Surface& map,
                         const WorldParams& world,
                         const GraphicsSettings& graphics) {
      MOPPE_PROFILE_ZONE ("Terrain::setup");
      m_scale = map.sample_spacing ();
      m_period = map.world_extent ();
      m_lod_scale = std::max (m_scale[0], m_scale[2]);

      render::TerrainParams params;
      params.width = map.width ();
      params.height = map.height ();
      params.scale = m_scale;
      params.sea_level = meters_value (world.water_level);
      params.tex_scale = 0.5f / m_scale[0];
      params.shadow_strength = graphics.terrain_shadows ? 0.85f : 0.0f;
      params.fog_scale = attenuation_value (world.fog_scale);
      params.topology_overlay = graphics.terrain_topology;
      params.fragment_normals = graphics.terrain_fragment_normals;
      params.snow_support_filter = graphics.snow_support_filter;
      params.channel_flux_detail = graphics.channel_flux_detail;
      {
        MOPPE_PROFILE_ZONE ("terrain.upload_height_and_normals");
        r.set_terrain (
          params,
          spatial::get<terrain::surface_elevation> (map.geometry ()),
          spatial::get<terrain::terrain_normal> (map.geometry ()));
      }

      if (!m_textures_loaded) {
        MOPPE_PROFILE_ZONE ("terrain.load_material_textures");
        m_grass = load_tga (r, "textures/grass3.tga");
        m_dirt = load_tga (r, "textures/dirt.tga");
        m_rock = load_tga (r, "textures/stones.tga");
        m_snow = load_tga (r, "textures/snow.tga");
        r.set_terrain_textures (m_grass, m_dirt, m_rock, m_snow);
        m_textures_loaded = true;
      }

      // Chunk bounding spheres from the actual height range.
      MOPPE_PROFILE_NAMED_ZONE (build_chunks, "terrain.build_chunk_bounds");
      const int chunks_per_side = map.width () / CHUNK;
      m_chunks.clear ();
      m_chunks.reserve ((size_t)chunks_per_side * chunks_per_side);
      for (int cz = 0; cz < chunks_per_side; ++cz)
        for (int cx = 0; cx < chunks_per_side; ++cx) {
          float ymin = 1e9f, ymax = -1e9f;
          for (int z = cz * CHUNK; z <= (cz + 1) * CHUNK; ++z)
            for (int x = cx * CHUNK; x <= (cx + 1) * CHUNK; ++x) {
              const float h = terrain::surface_elevation_value (
                map.elevation_at (terrain::wrap_index (x, map.width ()),
                                  terrain::wrap_index (z, map.height ())));
              ymin = std::min (ymin, h);
              ymax = std::max (ymax, h);
            }

          Chunk c;
          c.x0 = cx * CHUNK;
          c.z0 = cz * CHUNK;
          const float x0 = c.x0 * m_scale[0];
          const float x1 = (c.x0 + CHUNK) * m_scale[0];
          const float z0 = c.z0 * m_scale[2];
          const float z1 = (c.z0 + CHUNK) * m_scale[2];
          c.center = Vec3 ((x0 + x1) / 2, (ymin + ymax) / 2, (z0 + z1) / 2);
          const float hx = (x1 - x0) / 2, hy = (ymax - ymin) / 2,
                      hz = (z1 - z0) / 2;
          c.radius = std::sqrt (hx * hx + hy * hy + hz * hz);
          m_chunks.push_back (c);
        }
    }

    void Terrain::render_shadow (render::Renderer& r,
                                 const map::Surface& map,
                                 const Vec3& sun_dir) {
      MOPPE_PROFILE_ZONE ("Terrain::render_shadow");
      const Vec3 bounds = map.world_extent ();
      const Vec3 center (bounds[0] / 2, bounds[1] / 2, bounds[2] / 2);
      const float radius = length (bounds) / 2;

      // Ortho box big enough for the whole scene: the light sits
      // radius*3.5 out toward the sun, so the scene spans roughly
      // [2.5r, 4.5r] in light depth (same numbers as the GL build).
      const Vec3 light_pos = center + sun_dir * (radius * 3.5f);
      const Mat4 view = Mat4::look_at (light_pos, center, Vec3 (0, 1, 0));
      const Mat4 proj = Mat4::ortho (
        -radius, radius, -radius, radius, radius * 0.5f, radius * 6.0f);
      r.render_terrain_shadow (proj * view);
    }

    void Terrain::render (render::Renderer& r,
                          const Vec3& cam,
                          const Vec3& view_dir,
                          float max_dist) {
      m_draws.clear ();

      const float half_width = 0.5f * CHUNK * m_scale[0];
      const float half_depth = 0.5f * CHUNK * m_scale[2];
      for (size_t i = 0; i < m_chunks.size (); ++i) {
        const Chunk& c = m_chunks[i];
        const float reach = max_dist + c.radius;
        const int min_tile_x = static_cast<int> (
          std::ceil ((cam[0] - reach - c.center[0]) / m_period[0]));
        const int max_tile_x = static_cast<int> (
          std::floor ((cam[0] + reach - c.center[0]) / m_period[0]));
        const int min_tile_z = static_cast<int> (
          std::ceil ((cam[2] - reach - c.center[2]) / m_period[2]));
        const int max_tile_z = static_cast<int> (
          std::floor ((cam[2] + reach - c.center[2]) / m_period[2]));

        for (int tile_z = min_tile_z; tile_z <= max_tile_z; ++tile_z)
          for (int tile_x = min_tile_x; tile_x <= max_tile_x; ++tile_x) {
            const Vec3 offset (tile_x * m_period[0], 0, tile_z * m_period[2]);
            const Vec3 d = c.center + offset - cam;
            const float dist2 = length2 (d);

            // Too far: the haze has swallowed it.
            if (dist2 > reach * reach)
              continue;

            // Entirely behind the camera plane (conservative).
            if (dist2 > c.radius * c.radius && dot (d, view_dir) < -c.radius)
              continue;

            // Choose from the distance to the nearest point of the chunk,
            // rather than its center.
            const float dx = std::max (0.0f, std::fabs (d[0]) - half_width);
            const float dz = std::max (0.0f, std::fabs (d[2]) - half_depth);
            const float nearest = std::sqrt (dx * dx + dz * dz);

            int lod = LOD_COUNT - 1;
            for (int level = 0; level < LOD_COUNT - 1; ++level)
              if (nearest < LOD_END[level] * m_lod_scale) {
                lod = level;
                break;
              }

            render::ChunkDraw draw;
            draw.x0 = (uint16_t)c.x0;
            draw.z0 = (uint16_t)c.z0;
            draw.lod = (render::TerrainLod)lod;
            draw.morph_start =
              lod < LOD_COUNT - 1 ? LOD_MORPH_START[lod] * m_lod_scale : 0.0f;
            draw.morph_end =
              lod < LOD_COUNT - 1 ? LOD_END[lod] * m_lod_scale : 0.0f;
            draw.offset_x = offset[0];
            draw.offset_z = offset[2];
            m_draws.push_back (draw);
          }
      }

      if (!m_draws.empty ())
        r.draw_terrain (&m_draws[0], (int)m_draws.size ());
    }
  }
}
