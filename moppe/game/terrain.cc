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

      constexpr meters_t MAX_WAVE_HEIGHT = [] {
        meters_t height = 0.0f * u::m;
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
        // The planes are the matrix's rows, so the box meets them in
        // metres.
        bool admits (const position_t& low,
                     const position_t& high,
                     meters_t margin) const {
          const Vec3& lo = position_value (low);
          const Vec3& hi = position_value (high);
          for (const auto& p : plane) {
            const float x = p[0] >= 0 ? hi[0] : lo[0];
            const float y = p[1] >= 0 ? hi[1] : lo[1];
            const float z = p[2] >= 0 ? hi[2] : lo[2];
            const float reach =
              meters_value (margin) *
              std::sqrt (p[0] * p[0] + p[1] * p[1] + p[2] * p[2]);
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
      m_cell_x = surface.domain ().spacing_x ();
      m_cell_z = surface.domain ().spacing_z ();
      m_period = plan (extent_value (world.map_size)) * spatial_extent[u::m];
      m_lod_scale = std::max (m_cell_x, m_cell_z);

      // The material bands grade over this world's own massif relief, so narrow
      // towering alpine horn spires never push the snowline off the snowfield.
      const meters_t land_relief = terrain::measure_climatic_relief (
        surface, terrain::surface_elevation_point (world.water_level));

      render::TerrainParams params;
      params.width = static_cast<int> (surface.domain ().width ());
      params.height = static_cast<int> (surface.domain ().height ());
      params.scale =
        Vec3 (meters_value (m_cell_x), 1.0f, meters_value (m_cell_z));
      params.sea_level = meters_value (world.water_level);
      params.land_relief = std::max (meters_value (land_relief), 1.0f);
      params.tex_scale = 0.5f / meters_value (m_cell_x);
      params.shadow_strength = graphics.terrain_shadows ? 0.92f : 0.0f;
      params.fog_scale = attenuation_value (world.fog_scale);
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
          const terrain::SurfaceElevation lowest =
            terrain::surface_elevation_point (-1e9f * u::m);
          const terrain::SurfaceElevation highest =
            terrain::surface_elevation_point (1e9f * u::m);
          std::fill_n (c.patch_ymin, PATCHES * PATCHES, highest);
          std::fill_n (c.patch_ymax, PATCHES * PATCHES, lowest);
          std::array<bool, PATCHES * PATCHES> wet {};
          std::array<meters_t, PATCHES * PATCHES> waves {};
          for (int z = cz * CHUNK; z <= (cz + 1) * CHUNK; ++z)
            for (int x = cx * CHUNK; x <= (cx + 1) * CHUNK; ++x) {
              const terrain::TerrainIndex at {
                static_cast<std::size_t> (terrain::wrap_index (
                  x, static_cast<int> (surface.domain ().width ()))),
                static_cast<std::size_t> (terrain::wrap_index (
                  z, static_cast<int> (surface.domain ().height ())))
              };
              const terrain::SurfaceElevation h =
                spatial::get<terrain::surface_elevation> (surface[at]);
              terrain::SurfaceElevation top = h;
              meters_t wave = 0.0f * u::m;
              if (water) {
                const auto site = (*water)[at];
                const terrain::SurfaceElevation level =
                  spatial::get<terrain::surface_elevation> (site);
                top = std::max (h, level);
                const terrain::WaveAmplitude amplitude =
                  spatial::get<terrain::wave_amplitude> (site);
                wave = quantity_cast<isq::length> (MAX_WAVE_HEIGHT * amplitude *
                                                   amplitude);
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
                  terrain::SurfaceElevation& lo = c.patch_ymin[k];
                  terrain::SurfaceElevation& hi = c.patch_ymax[k];
                  lo = std::min (lo, h);
                  hi = std::max (hi, top);
                  wet[k] = wet[k] || top > h;
                  waves[k] = std::max (waves[k], wave);
                }
            }

          // Amplitude and depth interpolate separately. Omitting the
          // shallow-water damping bounds their product between samples
          // too, where it can exceed either sample's displaced height.
          terrain::SurfaceElevation ymin = highest, ymax = lowest;
          for (int k = 0; k < PATCHES * PATCHES; ++k) {
            const meters_t wave = wet[k] ? waves[k] : 0.0f * u::m;
            c.patch_ymin[k] -= wave;
            c.patch_ymax[k] += wave;
            ymin = std::min (ymin, c.patch_ymin[k]);
            ymax = std::max (ymax, c.patch_ymax[k]);
            c.wave_reach =
              std::max (c.wave_reach,
                        wave * terrain::sea_wave_lean.numerical_value_in (one));
          }

          c.x0 = cx * CHUNK;
          c.z0 = cz * CHUNK;
          const meters_t x0 = c.x0 * m_cell_x;
          const meters_t x1 = (c.x0 + CHUNK) * m_cell_x;
          const meters_t z0 = c.z0 * m_cell_z;
          const meters_t z1 = (c.z0 + CHUNK) * m_cell_z;
          c.center = terrain::at_elevation (
            plan_position ((x0 + x1) / 2, (z0 + z1) / 2),
            terrain::SurfaceElevation (
              (ymin.delta_from_zero () + ymax.delta_from_zero ()) / 2));
          const meters_t hx = (x1 - x0) / 2,
                         hy = quantity_cast<isq::length> (ymax - ymin) / 2,
                         hz = (z1 - z0) / 2;
          c.radius = sqrt (hx * hx + hy * hy + hz * hz) + c.wave_reach;
          c.ymin = ymin;
          c.ymax = ymax;
          m_chunks.push_back (c);
        }
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
                                            const direction_t& horizontal,
                                            const direction_t& sun,
                                            const direction_t& pole_way,
                                            meters_t radius,
                                            meters_t look_ahead,
                                            Terrain::ShadowAnchor& anchor) {
        constexpr int shadow_texels = 2048;
        const position_t wanted = camera + quantity_cast<isq::position_vector> (
                                             along (horizontal, look_ahead));
        // The light's frame is matrix arithmetic, on the directions' lanes.
        const Vec3& sun_dir = direction_value (sun);
        const Vec3& pole = direction_value (pole_way);
        const float reach = radius.numerical_value_in (u::m);
        const Vec3 light_forward = normalized (-sun_dir);
        Vec3 light_right = cross (light_forward, pole);
        if (length2 (light_right) < 1e-4f)
          light_right =
            cross (light_forward,
                   std::fabs (light_forward[1]) > 0.98f ? Vec3 (0, 0, 1)
                                                        : Vec3 (0, 1, 0));
        light_right = normalized (light_right);
        // A reciprocal-golden-ratio roll staggers texel coverage along
        // straight silhouettes so adjacent rows cross an edge at different
        // fractions of a texel.
        light_right = normalized (
          light_right + cross (light_right, light_forward) * 0.6180339887f);
        const Vec3 light_up = cross (light_right, light_forward);

        const meters_f64_t texel = 2.0 * meters_f64_t (radius) / shadow_texels;
        // Each lane of where the cascade wants to be, in double precision.
        const auto lane = [&] (std::size_t i) -> meters_f64_t {
          return static_cast<double> (position_value (wanted)[i]) * u::m;
        };
        const std::array<meters_f64_t, 3> start =
          anchor ? *anchor
                 : std::array<meters_f64_t, 3> { lane (0), lane (1), lane (2) };
        const auto along_axis = [&] (const Vec3& axis) {
          meters_f64_t sum = 0.0 * u::m;
          for (std::size_t i = 0; i < 3; ++i)
            sum += (lane (i) - start[i]) * static_cast<double> (axis[i]);
          return sum;
        };
        const auto steps = [&] (const Vec3& axis) {
          return std::round (
            (along_axis (axis) / texel).numerical_value_in (one));
        };
        const double right_steps = steps (light_right);
        const double up_steps = steps (light_up);
        const meters_f64_t forward_way = along_axis (light_forward);
        std::array<meters_f64_t, 3> walked;
        for (std::size_t i = 0; i < 3; ++i)
          walked[i] = start[i] + right_steps * texel * light_right[i] +
                      up_steps * texel * light_up[i] +
                      forward_way * light_forward[i];
        anchor = walked;
        const auto narrowed = [&] (std::size_t i) {
          return static_cast<float> (walked[i].numerical_value_in (u::m));
        };
        const Vec3 centre (narrowed (0), narrowed (1), narrowed (2));

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
                                       const direction_t& view_dir,
                                       const direction_t& sun_dir,
                                       const direction_t& sky_pole) {
      MOPPE_PROFILE_ZONE ("Terrain::render_local_shadow");
      const Vec3 level = lift (plan (direction_value (view_dir)));
      const direction_t horizontal = unit_direction (
        length2 (level) < 1e-6f ? Vec3 (0, 0, 1) : normalized (level));

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
      });
    }

    void Terrain::render (render::Renderer& r,
                          position_t cam,
                          const Mat4& view_proj,
                          meters_t max_dist) {
      m_draws.clear ();
      const SidePlanes frustum (view_proj);
      constexpr meters_t frustum_margin = 1.0f * u::m;
      const plan_position_t eye = plan (cam);
      const meters_t period_x = plan_value (m_period).x () * u::m;
      const meters_t period_z = plan_value (m_period).z () * u::m;

      // The level for a box whose nearest point is `nearest` away, and the
      // draw that renders it.
      const auto lod_for = [&] (meters_t nearest) {
        for (int level = 0; level < LOD_COUNT - 1; ++level)
          if (nearest < LOD_END[level] * m_lod_scale)
            return level;
        return LOD_COUNT - 1;
      };
      const auto emit = [&] (int x0,
                             int z0,
                             int cells,
                             int lod,
                             terrain::SurfaceElevation highest,
                             meters_t offset_x,
                             meters_t offset_z) {
        render::ChunkDraw draw;
        draw.x0 = (uint16_t)x0;
        draw.z0 = (uint16_t)z0;
        draw.cells = (uint16_t)cells;
        draw.highest = highest;
        draw.lod = (render::TerrainLod)lod;
        draw.morph_start = lod < LOD_COUNT - 1
                             ? meters_value (LOD_MORPH_START[lod] * m_lod_scale)
                             : 0.0f;
        draw.morph_end = lod < LOD_COUNT - 1
                           ? meters_value (LOD_END[lod] * m_lod_scale)
                           : 0.0f;
        draw.offset_x = meters_value (offset_x);
        draw.offset_z = meters_value (offset_z);
        m_draws.push_back (draw);
      };
      // Horizontal distance from the camera to the nearest point of a
      // world-space rectangle.
      const auto nearest_to =
        [&] (meters_t x0, meters_t z0, meters_t x1, meters_t z1) {
          const meters_t dx =
            std::max ({ 0.0f * u::m, x0 - x_of (eye), x_of (eye) - x1 });
          const meters_t dz =
            std::max ({ 0.0f * u::m, z0 - z_of (eye), z_of (eye) - z1 });
          return sqrt (dx * dx + dz * dz);
        };
      // How many whole periods from `from` to `to`, rounded by `whole`.
      const auto tiles = [] (auto whole, meters_t span, meters_t period) {
        return static_cast<int> (
          whole ((span / period).numerical_value_in (one)));
      };
      const auto up = [] (float value) { return std::ceil (value); };
      const auto down = [] (float value) { return std::floor (value); };

      const meters_t chunk_x = CHUNK * m_cell_x;
      const meters_t chunk_z = CHUNK * m_cell_z;
      const meters_t patch_x = PATCH * m_cell_x;
      const meters_t patch_z = PATCH * m_cell_z;
      for (size_t i = 0; i < m_chunks.size (); ++i) {
        const Chunk& c = m_chunks[i];
        const meters_t reach = max_dist + c.radius;
        const plan_position_t centre = plan (c.center);
        const int min_tile_x =
          tiles (up, x_of (eye) - reach - x_of (centre), period_x);
        const int max_tile_x =
          tiles (down, x_of (eye) + reach - x_of (centre), period_x);
        const int min_tile_z =
          tiles (up, z_of (eye) - reach - z_of (centre), period_z);
        const int max_tile_z =
          tiles (down, z_of (eye) + reach - z_of (centre), period_z);

        for (int tile_z = min_tile_z; tile_z <= max_tile_z; ++tile_z)
          for (int tile_x = min_tile_x; tile_x <= max_tile_x; ++tile_x) {
            const meters_t offset_x = tile_x * period_x;
            const meters_t offset_z = tile_z * period_z;
            const displacement_t offset = displacement (
              lift (plan_value (plan_displacement (offset_x, offset_z))));
            const displacement_t d =
              (c.center + quantity_cast<isq::position_vector> (offset)) - cam;

            // Too far: the haze has swallowed it.
            if (length2 (displacement_value (d)) * u::m * u::m > reach * reach)
              continue;

            const meters_t x0 = c.x0 * m_cell_x + offset_x;
            const meters_t z0 = c.z0 * m_cell_z + offset_z;
            // The box from a corner on the plan up to two elevations.
            const auto corner =
              [] (meters_t x, meters_t z, terrain::SurfaceElevation y) {
                return terrain::at_elevation (plan_position (x, z), y);
              };
            if (!frustum.admits (corner (x0, z0, c.ymin),
                                 corner (x0 + chunk_x, z0 + chunk_z, c.ymax),
                                 frustum_margin + c.wave_reach))
              continue;

            // Choose from the distance to the nearest point of the chunk,
            // rather than its center.
            const int lod =
              lod_for (nearest_to (x0, z0, x0 + chunk_x, z0 + chunk_z));
            if (lod != (int)render::TerrainLod::Subdivided) {
              emit (c.x0, c.z0, CHUNK, lod, c.ymax, offset_x, offset_z);
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
                const meters_t px0 = x0 + px * patch_x;
                const meters_t pz0 = z0 + pz * patch_z;
                const int k = pz * PATCHES + px;
                if (!frustum.admits (
                      corner (px0, pz0, c.patch_ymin[k]),
                      corner (px0 + patch_x, pz0 + patch_z, c.patch_ymax[k]),
                      frustum_margin + c.wave_reach))
                  continue;
                emit (
                  c.x0 + px * PATCH,
                  c.z0 + pz * PATCH,
                  PATCH,
                  lod_for (nearest_to (px0, pz0, px0 + patch_x, pz0 + patch_z)),
                  c.patch_ymax[k],
                  offset_x,
                  offset_z);
              }
          }
      }

      if (!m_draws.empty ())
        r.draw_terrain (&m_draws[0], (int)m_draws.size ());
    }
  }
}
