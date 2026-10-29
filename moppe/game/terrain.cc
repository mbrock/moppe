#include <moppe/game/terrain.hh>
#include <moppe/gfx/tga.hh>
#include <moppe/profile.hh>
#include <moppe/terrain/readings.hh>
#include <moppe/terrain/sea_waves.hh>

#include <algorithm>
#include <cmath>
#include <cstdlib>
#include <stdexcept>

#include <moppe/platform/platform.hh>

namespace moppe {
  namespace game {
    namespace {
      const int CHUNK = render::terrain_chunk_cells;
      const int PATCH = render::terrain_patch_cells;
      const int LOD_COUNT = (int)render::TerrainLod::Count;

      // Distances are expressed in source terrain cells and scaled to
      // world units at setup.  Each end is also the point where the finer
      // level has completely morphed onto its parent surface.
      const float LOD_MORPH_START[LOD_COUNT - 1] = {
        64.0f, 288.0f, 576.0f, 1152.0f
      };
      const float LOD_END[LOD_COUNT - 1] = { 96.0f, 384.0f, 768.0f, 1536.0f };

      constexpr float MAX_WAVE_HEIGHT = [] {
        float height = 0.0f;
        for (const auto& train : terrain::sea_wave_trains)
          height += train.amplitude;
        return height;
      }();

      // The four side planes of a view frustum, from the rows of its
      // view-projection matrix.  Near and far are left to the distance cull.
      struct SidePlanes {
        float plane[4][4];

        explicit SidePlanes (const Mat4& m) {
          for (int k = 0; k < 4; ++k) {
            const int row = k / 2;
            const float sign = k % 2 ? -1.0f : 1.0f;
            for (int col = 0; col < 4; ++col)
              plane[k][col] = m.at (col, 3) + sign * m.at (col, row);
          }
        }

        // Whether any part of the box can be on screen.  `margin` widens
        // each plane by that many metres for jitter and horizontal waves.
        bool admits (const Vec3& lo, const Vec3& hi, float margin) const {
          for (const auto& p : plane) {
            const float x = p[0] >= 0 ? hi[0] : lo[0];
            const float y = p[1] >= 0 ? hi[1] : lo[1];
            const float z = p[2] >= 0 ? hi[2] : lo[2];
            const float reach =
              margin * std::sqrt (p[0] * p[0] + p[1] * p[1] + p[2] * p[2]);
            if (p[0] * x + p[1] * y + p[2] * z + p[3] + reach < 0)
              return false;
          }
          return true;
        }
      };

      render::TexturePtr load_tga (render::Renderer& r,
                                   const std::string& rel) {
        MOPPE_PROFILE_ZONE ("terrain.load_sward_texture");
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
                         const map::SurfaceGeometry& surface,
                         const WorldParams& world,
                         const GraphicsSettings& graphics,
                         const terrain::WaterSheets* water) {
      MOPPE_PROFILE_ZONE ("Terrain::setup");
      m_scale = Vec3 (surface.domain ().spacing_x ().numerical_value_in (u::m),
                      1.0f,
                      surface.domain ().spacing_z ().numerical_value_in (u::m));
      m_extent = extent_value (world.map_size);
      m_period = Vec3 (m_extent[0], 0.0f, m_extent[2]);
      m_lod_scale = std::max (m_scale[0], m_scale[2]);

      render::TerrainParams params;
      params.width = static_cast<int> (surface.domain ().width ());
      params.height = static_cast<int> (surface.domain ().height ());
      params.scale = m_scale;
      params.sea_level = (world.water_level).numerical_value_in (moppe::u::m);
      // The material bands grade over this world's own land, so ask the
      // surface how high it actually reaches instead of assuming a range.
      params.land_relief = std::max (
        terrain::measure_height_range (surface).maximum - params.sea_level,
        1.0f);
      params.tex_scale = 0.5f / m_scale[0];
      params.shadow_strength = graphics.terrain_shadows ? 0.92f : 0.0f;
      params.fog_scale = attenuation_value (world.fog_scale);
      params.topology_overlay = graphics.terrain_topology;
      params.fragment_normals = graphics.terrain_fragment_normals;
      params.snow_support_filter = graphics.snow_support_filter;
      params.grass_cover_boost = graphics.grass_cover_boost;
      {
        MOPPE_PROFILE_ZONE ("terrain.upload_height_and_normals");
        r.set_terrain (params,
                       spatial::get<terrain::surface_elevation> (surface),
                       spatial::get<terrain::terrain_normal> (surface));
      }

      if (!m_grass) {
        m_grass = load_tga (r, "textures/grass3.tga");
        r.set_sward_texture (m_grass);
      }

      // Water reuses these draws, so bound both the ground and its wet
      // sheet, per patch. Dry cells never introduce a sea plane.
      MOPPE_PROFILE_NAMED_ZONE (build_chunks, "terrain.build_chunk_bounds");
      const int chunks_per_side =
        static_cast<int> (surface.domain ().width ()) / CHUNK;
      m_chunks.clear ();
      m_chunks.reserve ((size_t)chunks_per_side * chunks_per_side);
      for (int cz = 0; cz < chunks_per_side; ++cz)
        for (int cx = 0; cx < chunks_per_side; ++cx) {
          Chunk c;
          std::fill_n (c.patch_ymin, PATCHES * PATCHES, 1e9f);
          std::fill_n (c.patch_ymax, PATCHES * PATCHES, -1e9f);
          std::array<bool, PATCHES * PATCHES> wet {};
          std::array<float, PATCHES * PATCHES> waves {};
          for (int z = cz * CHUNK; z <= (cz + 1) * CHUNK; ++z)
            for (int x = cx * CHUNK; x <= (cx + 1) * CHUNK; ++x) {
              const terrain::TerrainIndex at {
                static_cast<std::size_t> (terrain::wrap_index (
                  x, static_cast<int> (surface.domain ().width ()))),
                static_cast<std::size_t> (terrain::wrap_index (
                  z, static_cast<int> (surface.domain ().height ())))
              };
              const float h = terrain::surface_elevation_value (
                spatial::get<terrain::surface_elevation> (surface[at]));
              float top = h, wave = 0.0f;
              if (water) {
                const auto site = (*water)[at];
                const float level = terrain::surface_elevation_value (
                  spatial::get<terrain::surface_elevation> (site));
                top = std::max (h, level);
                const float amplitude =
                  spatial::get<terrain::wave_amplitude> (site)
                    .numerical_value_in (mp_units::one);
                wave = MAX_WAVE_HEIGHT * amplitude * amplitude;
              }
              // Samples on a patch border belong to both patches.
              const int px = x - cx * CHUNK, pz = z - cz * CHUNK;
              for (int j = std::max (0, (pz - 1) / PATCH);
                   j <= std::min (PATCHES - 1, pz / PATCH);
                   ++j)
                for (int i = std::max (0, (px - 1) / PATCH);
                     i <= std::min (PATCHES - 1, px / PATCH);
                     ++i) {
                  const int k = j * PATCHES + i;
                  float& lo = c.patch_ymin[k];
                  float& hi = c.patch_ymax[k];
                  lo = std::min (lo, h);
                  hi = std::max (hi, top);
                  wet[k] = wet[k] || top > h;
                  waves[k] = std::max (waves[k], wave);
                }
            }

          // Amplitude and depth interpolate separately. Omitting the
          // shallow-water damping bounds their product between samples
          // too, where it can exceed either sample's displaced height.
          float ymin = 1e9f, ymax = -1e9f;
          for (int k = 0; k < PATCHES * PATCHES; ++k) {
            const float wave = wet[k] ? waves[k] : 0.0f;
            c.patch_ymin[k] -= wave;
            c.patch_ymax[k] += wave;
            ymin = std::min (ymin, c.patch_ymin[k]);
            ymax = std::max (ymax, c.patch_ymax[k]);
            c.wave_reach =
              std::max (c.wave_reach, wave * terrain::sea_wave_lean);
          }

          c.x0 = cx * CHUNK;
          c.z0 = cz * CHUNK;
          const float x0 = c.x0 * m_scale[0];
          const float x1 = (c.x0 + CHUNK) * m_scale[0];
          const float z0 = c.z0 * m_scale[2];
          const float z1 = (c.z0 + CHUNK) * m_scale[2];
          c.center = Vec3 ((x0 + x1) / 2, (ymin + ymax) / 2, (z0 + z1) / 2);
          const float hx = (x1 - x0) / 2, hy = (ymax - ymin) / 2,
                      hz = (z1 - z0) / 2;
          c.radius = std::sqrt (hx * hx + hy * hy + hz * hz) + c.wave_reach;
          c.ymin = ymin;
          c.ymax = ymax;
          m_chunks.push_back (c);
        }
    }

    void Terrain::render_shadow (render::Renderer& r,
                                 const Vec3& sun_dir,
                                 bool include_forest) {
      MOPPE_PROFILE_ZONE ("Terrain::render_shadow");
      const Vec3 center (m_extent[0] / 2, m_extent[1] / 2, m_extent[2] / 2);
      const float radius = length (m_extent) / 2;

      // Ortho box big enough for the whole scene: the light sits
      // radius*3.5 out toward the sun, so the scene spans roughly
      // [2.5r, 4.5r] in light depth (same numbers as the GL build).
      const Vec3 light_pos = center + sun_dir * (radius * 3.5f);
      const Mat4 view = Mat4::look_at (light_pos, center, Vec3 (0, 1, 0));
      const Mat4 proj = Mat4::ortho (
        -radius, radius, -radius, radius, radius * 0.5f, radius * 6.0f);
      r.render_terrain_shadow (proj * view, include_forest);
    }

    namespace {
      // One cascade of the sun's map: an orthographic box of `radius`
      // about a centre `look_ahead` along the horizontal view.
      //
      // The map's texels are a lattice in the plane square to the light.
      // Snapping the centre to that lattice keeps it still under the
      // camera's travel, but the light turns all day, and a turning
      // lattice slides under the world by the angle turned times the
      // distance to whatever it pivots about. Snapped to a lattice through
      // the world's origin, a map kilometres from it slid many texels a
      // second, and every shadow edge crawled and jumped. So the centre
      // is an anchor that persists from frame to frame and walks after
      // the camera in whole texels of this frame's lattice: the lattice
      // turns about a point at the camera, where the slide is nothing.
      //
      // The lattice's roll about the light is free to choose, and is
      // squared to `pole`, the axis the light turns about. The light
      // keeps one angle to that axis all day, so the lattice simply turns
      // with it; squared to the zenith it would spin fastest at noon.
      render::ShadowCascade shadow_cascade (position_t camera,
                                            const Vec3& horizontal,
                                            const Vec3& sun_dir,
                                            const Vec3& pole,
                                            meters_t radius,
                                            meters_t look_ahead,
                                            Terrain::ShadowAnchor& anchor) {
        constexpr int shadow_texels = 2048;
        const Vec3 wanted = position_value (camera + horizontal * look_ahead);
        const float reach = radius.numerical_value_in (u::m);
        const Vec3 light_forward = normalized (-sun_dir);
        Vec3 light_right = cross (light_forward, pole);
        if (length2 (light_right) < 1e-4f)
          light_right =
            cross (light_forward,
                   std::fabs (light_forward[1]) > 0.98f ? Vec3 (0, 0, 1)
                                                        : Vec3 (0, 1, 0));
        light_right = normalized (light_right);
        const Vec3 light_up = cross (light_right, light_forward);

        const double texel = 2.0 * reach / shadow_texels;
        const std::array<double, 3> start =
          anchor ? *anchor
                 : std::array<double, 3> { wanted[0], wanted[1], wanted[2] };
        const auto along = [&] (const Vec3& axis) {
          double sum = 0.0;
          for (std::size_t i = 0; i < 3; ++i)
            sum += (static_cast<double> (wanted[i]) - start[i]) *
                   static_cast<double> (axis[i]);
          return sum;
        };
        const double right_steps = std::round (along (light_right) / texel);
        const double up_steps = std::round (along (light_up) / texel);
        const double forward_way = along (light_forward);
        std::array<double, 3> walked;
        for (std::size_t i = 0; i < 3; ++i)
          walked[i] = start[i] + right_steps * texel * light_right[i] +
                      up_steps * texel * light_up[i] +
                      forward_way * light_forward[i];
        anchor = walked;
        const Vec3 centre (static_cast<float> (walked[0]),
                           static_cast<float> (walked[1]),
                           static_cast<float> (walked[2]));

        const Vec3 light_position = centre + sun_dir * (reach * 4.0f);
        const Mat4 view = Mat4::look_at (light_position, centre, light_up);
        const Mat4 projection = Mat4::ortho (
          -reach, reach, -reach, reach, reach * 0.25f, reach * 8.0f);
        return { .light_view_proj = projection * view,
                 .focus = position (centre),
                 .radius = radius };
      }
    }

    void Terrain::render_local_shadow (render::Renderer& r,
                                       position_t camera,
                                       const Vec3& view_dir,
                                       const Vec3& sun_dir,
                                       const Vec3& sky_pole,
                                       bool include_forest,
                                       bool include_boulders,
                                       bool include_far) {
      MOPPE_PROFILE_ZONE ("Terrain::render_local_shadow");
      Vec3 horizontal (view_dir[0], 0.0f, view_dir[2]);
      if (length2 (horizontal) < 1e-6f)
        horizontal = Vec3 (0, 0, 1);
      else
        horizontal = normalized (horizontal);

      // The near cascade keeps the rider's surroundings sharp; the far one,
      // ten times coarser, reaches out to where the haze closes, mostly
      // ahead, which is what a glider sees.
      r.render_local_shadow ({
        .cascades = { shadow_cascade (camera,
                                      horizontal,
                                      sun_dir,
                                      sky_pole,
                                      160.0f * u::m,
                                      48.0f * u::m,
                                      m_shadow_anchors[0]),
                      shadow_cascade (camera,
                                      horizontal,
                                      sun_dir,
                                      sky_pole,
                                      1600.0f * u::m,
                                      700.0f * u::m,
                                      m_shadow_anchors[1]) },
        .include_far = include_far,
        .include_forest = include_forest,
        .include_boulders = include_boulders,
      });
    }

    void Terrain::render (render::Renderer& r,
                          const Vec3& cam,
                          const Mat4& view_proj,
                          float max_dist) {
      m_draws.clear ();
      const SidePlanes frustum (view_proj);
      constexpr float frustum_margin = 1.0f;

      // The level for a box whose nearest point is `nearest` away, and the
      // draw that renders it.
      const auto lod_for = [&] (float nearest) {
        for (int level = 0; level < LOD_COUNT - 1; ++level)
          if (nearest < LOD_END[level] * m_lod_scale)
            return level;
        return LOD_COUNT - 1;
      };
      const auto emit =
        [&] (int x0, int z0, int cells, int lod, const Vec3& offset) {
          render::ChunkDraw draw;
          draw.x0 = (uint16_t)x0;
          draw.z0 = (uint16_t)z0;
          draw.cells = (uint16_t)cells;
          draw.lod = (render::TerrainLod)lod;
          draw.morph_start =
            lod < LOD_COUNT - 1 ? LOD_MORPH_START[lod] * m_lod_scale : 0.0f;
          draw.morph_end =
            lod < LOD_COUNT - 1 ? LOD_END[lod] * m_lod_scale : 0.0f;
          draw.offset_x = offset[0];
          draw.offset_z = offset[2];
          m_draws.push_back (draw);
        };
      // Horizontal distance from the camera to the nearest point of a
      // world-space rectangle.
      const auto nearest_to = [&] (float x0, float z0, float x1, float z1) {
        const float dx = std::max ({ 0.0f, x0 - cam[0], cam[0] - x1 });
        const float dz = std::max ({ 0.0f, z0 - cam[2], cam[2] - z1 });
        return std::sqrt (dx * dx + dz * dz);
      };

      const float chunk_x = CHUNK * m_scale[0];
      const float chunk_z = CHUNK * m_scale[2];
      const float patch_x = PATCH * m_scale[0];
      const float patch_z = PATCH * m_scale[2];
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

            // Too far: the haze has swallowed it.
            if (length2 (d) > reach * reach)
              continue;

            const float x0 = c.x0 * m_scale[0] + offset[0];
            const float z0 = c.z0 * m_scale[2] + offset[2];
            if (!frustum.admits (Vec3 (x0, c.ymin, z0),
                                 Vec3 (x0 + chunk_x, c.ymax, z0 + chunk_z),
                                 frustum_margin + c.wave_reach))
              continue;

            // Choose from the distance to the nearest point of the chunk,
            // rather than its center.
            const int lod =
              lod_for (nearest_to (x0, z0, x0 + chunk_x, z0 + chunk_z));
            if (lod != (int)render::TerrainLod::Subdivided) {
              emit (c.x0, c.z0, CHUNK, lod, offset);
              continue;
            }

            // The subdivided level is several times denser than the
            // native one, so a chunk that merely touches its ring would
            // spend most of its triangles on ground hundreds of metres
            // away.  Patches choose their own level instead.  Where a
            // native patch meets a subdivided one, the shared edge lies at
            // least LOD_END[0] away, so the subdivided side has already
            // morphed fully onto the native surface there.
            for (int pz = 0; pz < PATCHES; ++pz)
              for (int px = 0; px < PATCHES; ++px) {
                const float px0 = x0 + px * patch_x;
                const float pz0 = z0 + pz * patch_z;
                const int k = pz * PATCHES + px;
                if (!frustum.admits (
                      Vec3 (px0, c.patch_ymin[k], pz0),
                      Vec3 (px0 + patch_x, c.patch_ymax[k], pz0 + patch_z),
                      frustum_margin + c.wave_reach))
                  continue;
                emit (
                  c.x0 + px * PATCH,
                  c.z0 + pz * PATCH,
                  PATCH,
                  lod_for (nearest_to (px0, pz0, px0 + patch_x, pz0 + patch_z)),
                  offset);
              }
          }
      }

      if (!m_draws.empty ())
        r.draw_terrain (&m_draws[0], (int)m_draws.size ());
    }
  }
}
