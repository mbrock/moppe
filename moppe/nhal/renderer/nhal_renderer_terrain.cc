// The terrain: its heights and normals, its material sheets, the roads'
// segments, and its chunks drawn at the levels the game's culling chose,
// with the road ribbons laid again over them near the camera.

#include <moppe/nhal/renderer/field_texture.hh>
#include <moppe/nhal/renderer/nhal_renderer_impl.hh>
#include <moppe/terrain/torus.hh>

namespace moppe::nhal {
  namespace detail {
    namespace {
      constexpr std::uint32_t cache_atlas_width = 1024;
      constexpr std::uint32_t cache_atlas_height = 2048;
      constexpr std::uint32_t terrain_gi_size = 1024;
      constexpr std::int64_t cache_ring_side = 510;
      constexpr std::int64_t cache_scroll_step = 8;
      constexpr float cache_world_side = 1022.0f;
      constexpr std::array<float, 4> cache_level_spans {
        16.0f, 64.0f, 256.0f, 1024.0f
      };
      constexpr std::array<std::array<int, 2>, 4> cache_level_slots { {
        { 0, 0 },
        { 512, 0 },
        { 0, 512 },
        { 512, 512 },
      } };

      std::int64_t floor_div (std::int64_t a, std::int64_t b) {
        const std::int64_t q = a / b;
        const std::int64_t r = a % b;
        return r < 0 ? q - 1 : q;
      }
    }

    bool NhalRenderer::use_terrain_cache () const {
      static const bool disabled = [] {
        const char* v = moppe::environment ("MOPPE_TERRAIN_CACHE");
        return v && std::string_view (v) == "0";
      }();
      return !disabled;
    }

    void NhalRenderer::make_terrain_pipelines () {
      const RenderPipelineDesc scene = scene_target ();
      const RenderPipelineDesc caster = shadow_caster ();
      RenderPipelineDesc terrain = scene;
      terrain.program = &shaders::terrain::program;
      terrain.vertex = m_shaders.terrain[0];
      terrain.fragment = m_shaders.terrain[1];
      terrain.topology = Topology::triangle_strip;
      m_terrain.draw = m_device->create_render_pipeline (terrain);

      RenderPipelineDesc terrain_live = terrain;
      terrain_live.program = &shaders::terrain_live::program;
      terrain_live.vertex = m_shaders.terrain_live[0];
      terrain_live.fragment = m_shaders.terrain_live[1];
      terrain_live.label = "terrain live";
      m_terrain.draw_live = m_device->create_render_pipeline (terrain_live);

      RenderPipelineDesc ribbon = terrain;
      ribbon.program = &shaders::road_ribbon::program;
      ribbon.vertex = m_shaders.road_ribbon[0];
      ribbon.fragment = m_shaders.road_ribbon[1];
      ribbon.label = "road ribbon";
      m_terrain.ribbon = m_device->create_render_pipeline (ribbon);

      RenderPipelineDesc ribbon_live = ribbon;
      ribbon_live.program = &shaders::road_ribbon_live::program;
      ribbon_live.vertex = m_shaders.road_ribbon_live[0];
      ribbon_live.fragment = m_shaders.road_ribbon_live[1];
      ribbon_live.label = "road ribbon live";
      m_terrain.ribbon_live = m_device->create_render_pipeline (ribbon_live);

      RenderPipelineDesc bake;
      bake.program = &shaders::terrain_bake::program;
      bake.vertex = m_shaders.terrain_bake[0];
      bake.fragment = m_shaders.terrain_bake[1];
      bake.color_count = 4;
      bake.color_formats[0] = Format::rgba8_unorm;
      bake.color_formats[1] = Format::rgba8_unorm;
      bake.color_formats[2] = Format::rgba8_unorm;
      bake.color_formats[3] = Format::rgba8_unorm;
      bake.cull = Cull::none;
      bake.depth_write = false;
      bake.label = "terrain material bake";
      m_terrain.cache_bake = m_device->create_render_pipeline (bake);

      RenderPipelineDesc gi;
      gi.program = &shaders::terrain_gi_bake::program;
      gi.vertex = m_shaders.terrain_gi_bake[0];
      gi.fragment = m_shaders.terrain_gi_bake[1];
      gi.color_count = 3;
      gi.color_formats[0] = Format::rgba8_unorm;
      gi.color_formats[1] = Format::rgba8_unorm;
      gi.color_formats[2] = Format::rgba8_unorm;
      gi.cull = Cull::none;
      gi.depth_write = false;
      gi.label = "terrain gi bake";
      m_terrain.gi_bake = m_device->create_render_pipeline (gi);

      RenderPipelineDesc terrain_caster = caster;
      terrain_caster.program = &shaders::terrain_shadow::program;
      terrain_caster.vertex = m_shaders.terrain_shadow[0];
      terrain_caster.topology = Topology::triangle_strip;
      terrain_caster.label = "terrain shadow";
      m_sun_shadow.terrain = m_device->create_render_pipeline (terrain_caster);
    }

    void NhalRenderer::set_terrain (
      const render::TerrainParams& params,
      std::span<const terrain::SurfaceElevation> heights,
      std::span<const terrain::TerrainNormal> normals) {
      m_sun_shadow.cached_far.reset ();
      m_terrain.params = params;
      m_terrain.snow_window = {};
      const std::uint32_t w = params.width, h = params.height;
      for (Texture* t : { &m_terrain.heights, &m_terrain.normals })
        if (*t)
          m_device->destroy (*t);
      m_terrain.heights = m_device->create_texture (
        { w, h, Format::r32_float, usage_sampled, 1, "terrain heights" });
      static_assert (sizeof (terrain::SurfaceElevation) == sizeof (float));
      m_device->write_texture (m_terrain.heights, std::as_bytes (heights));
      // Normals: x and z as RG16 snorm; the shader rebuilds y.
      m_terrain.normals = m_device->create_texture (
        { w, h, Format::rg16_snorm, usage_sampled, 1, "terrain normals" });
      std::vector<std::int16_t> packed (normals.size () * 2);
      for (std::size_t i = 0; i < normals.size (); ++i) {
        const Vec3 n = normals[i].numerical_value_in (mp_units::one);
        packed[i * 2] =
          std::int16_t (std::clamp (n[0], -1.0f, 1.0f) * 32767.0f);
        packed[i * 2 + 1] =
          std::int16_t (std::clamp (n[2], -1.0f, 1.0f) * 32767.0f);
      }
      m_device->write_texture (m_terrain.normals,
                               std::as_bytes (std::span (packed)));

      // Strip index templates per level, whole chunks and patches, with
      // the all-ones restart between rows.
      if (!m_terrain.chunk_indices[0]) {
        auto strips = [&] (int verts, const char* label) {
          std::vector<std::uint32_t> indices;
          for (int row = 0; row + 1 < verts; ++row) {
            for (int x = 0; x < verts; ++x) {
              indices.push_back (std::uint32_t (row * verts + x));
              indices.push_back (std::uint32_t ((row + 1) * verts + x));
            }
            indices.push_back (0xffffffffu);
          }
          const Buffer buffer = m_device->create_buffer (
            { .size = indices.size () * 4, .label = label },
            std::as_bytes (std::span (indices)));
          return std::pair (buffer, std::uint32_t (indices.size ()));
        };
        for (int lod = 0; lod < lod_count; ++lod) {
          std::tie (m_terrain.chunk_indices[lod],
                    m_terrain.chunk_index_count[lod]) =
            strips (lod_verts[lod], "terrain chunk strips");
          std::tie (m_terrain.patch_indices[lod],
                    m_terrain.patch_index_count[lod]) =
            strips (patch_verts (lod), "terrain patch strips");
        }
      }
      m_terrain.have = true;
      m_terrain.gi_dirty = true;
      ensure_terrain_gi ();
      m_terrain.cache_dirty = true;
      if (use_terrain_cache ())
        ensure_terrain_cache ();
    }

    void NhalRenderer::set_terrain_materials (
      const render::TexturePixels& landscape,
      const render::TexturePixels& ground,
      const render::TexturePixels& plants,
      const render::TexturePixels& more_plants,
      const render::TexturePixels& scars,
      const render::TexturePixels& bedrock) {
      auto sheet = [&] (Texture& texture,
                        const render::TexturePixels& pixels,
                        const char* label,
                        unsigned limit = 2048) {
        if (texture)
          m_device->destroy (texture);
        texture = {};
        if (pixels.empty () ||
            pixels.format () != render::PixelFormat::rgba8unorm)
          return;
        // The ground sheet carries narrow treads and signed shore edges.
        // Preserve their native footprint for grass, snow and the ground.
        texture =
          field_texture (*m_device, pixels, Format::rgba8_unorm, label, limit);
      };
      sheet (m_terrain.landscape, landscape, "landscape materials");
      sheet (m_terrain.ground,
             ground,
             "ground materials",
             std::max (ground.width (), ground.height ()));
      sheet (m_terrain.plants, plants, "plants");
      sheet (m_terrain.more_plants, more_plants, "more plants");
      sheet (m_terrain.scars, scars, "scars");
      sheet (m_terrain.bedrock, bedrock, "bedrock");
      m_terrain.cache_dirty = true;
    }

    void NhalRenderer::set_snow_relief (plan_position_t origin,
                                        meters_t span,
                                        meters_t depth,
                                        int side,
                                        std::span<const std::uint8_t> pixels) {
      if (pixels.empty ()) {
        m_terrain.snow_window = {};
        return;
      }
      const std::uint32_t slots =
        std::max (1u, m_device->info ().frames_in_flight) + 1u;
      if (m_terrain.snow.size () != slots ||
          m_terrain.snow_side != std::uint32_t (side)) {
        for (Texture t : m_terrain.snow)
          if (t)
            m_device->destroy (t);
        m_terrain.snow.clear ();
        m_terrain.snow.reserve (slots);
        for (std::uint32_t i = 0; i < slots; ++i)
          m_terrain.snow.push_back (
            m_device->create_texture ({ std::uint32_t (side),
                                        std::uint32_t (side),
                                        Format::r8_unorm,
                                        usage_sampled,
                                        1,
                                        "packed snow" }));
        m_terrain.snow_side = std::uint32_t (side);
        m_terrain.snow_slot = 0;
      } else {
        m_terrain.snow_slot = (m_terrain.snow_slot + 1u) % slots;
      }
      m_device->write_texture (m_terrain.snow[m_terrain.snow_slot],
                               std::as_bytes (pixels));
      m_terrain.snow_window = { meters_value (x_of (origin)),
                                meters_value (z_of (origin)),
                                1.0f / meters_value (span),
                                meters_value (depth) };
    }

    void NhalRenderer::set_roads (const RoadSetup& setup) {
      for (Buffer* b : { &m_terrain.road_rows, &m_terrain.road_index })
        if (*b)
          m_device->destroy (*b), *b = {};
      m_terrain.road_setup = setup;
      m_terrain.road_setup.segment_rows = {};
      m_terrain.road_setup.index = {};
      m_terrain.road_segments.assign (setup.segment_rows.begin (),
                                      setup.segment_rows.end ());
      if (setup.segment_rows.empty () || setup.index.empty () ||
          setup.buckets_x <= 0 || setup.buckets_z <= 0)
        return;
      m_terrain.road_rows = m_device->create_buffer (
        { .size = setup.segment_rows.size_bytes (), .label = "road segments" },
        std::as_bytes (setup.segment_rows));
      m_terrain.road_index = m_device->create_buffer (
        { .size = setup.index.size_bytes (), .label = "road buckets" },
        std::as_bytes (setup.index));
      m_terrain.cache_dirty = true;
    }

    void NhalRenderer::bind_roads (int first) {
      if (!m_terrain.no_roads) {
        const std::array<float, 8> none {};
        m_terrain.no_roads = m_device->create_buffer (
          { .size = sizeof (none), .label = "no roads" },
          std::as_bytes (std::span (none)));
      }
      m_device->set_buffer (
        first, m_terrain.road_rows ? m_terrain.road_rows : m_terrain.no_roads);
      m_device->set_buffer (first + 1,
                            m_terrain.road_index ? m_terrain.road_index
                                                 : m_terrain.no_roads);
    }

    void NhalRenderer::ensure_terrain_cache () {
      if (m_terrain.cache_atlas[0])
        return;
      const std::array<Format, 4> formats {
        Format::rgba8_unorm,
        Format::rgba8_unorm,
        Format::rgba8_unorm,
        Format::rgba8_unorm,
      };
      const std::array<const char*, 4> labels {
        "terrain cache albedo",
        "terrain cache slope",
        "terrain cache normal",
        "terrain cache grain",
      };
      for (std::size_t i = 0; i < formats.size (); ++i)
        m_terrain.cache_atlas[i] =
          m_device->create_texture ({ cache_atlas_width,
                                      cache_atlas_height,
                                      formats[i],
                                      usage_render_target | usage_sampled,
                                      1,
                                      labels[i] });
    }

    void NhalRenderer::ensure_terrain_gi () {
      if (m_terrain.sky_gi)
        return;
      const auto make = [&] (const char* label) {
        return m_device->create_texture ({ terrain_gi_size,
                                           terrain_gi_size,
                                           Format::rgba8_unorm,
                                           usage_render_target | usage_sampled,
                                           1,
                                           label });
      };
      m_terrain.sky_gi = make ("terrain sky gi");
      m_terrain.horizon_a = make ("terrain horizon a");
      m_terrain.horizon_b = make ("terrain horizon b");
    }

    void NhalRenderer::update_terrain_gi () {
      if (!m_terrain.have || !m_terrain.gi_dirty)
        return;
      ensure_terrain_gi ();
      m_dynamic.pause ();
      const auto& p = m_terrain.params;
      const float r_start = std::max (p.scale[0], p.scale[2]);
      const float period_min =
        std::min (float (p.width) * p.scale[0], float (p.height) * p.scale[2]);
      const float r_end = std::max (r_start * 2.0f, 0.42f * period_min);
      constexpr int steps = 48;
      const float r_ratio =
        std::pow (r_end / r_start, 1.0f / float (steps - 1));
      shaders::terrain_gi_bake::GiBake bake {};
      bake.dims = { float (p.width), float (p.height), p.scale[0], p.scale[2] };
      bake.params = { p.scale[1], r_start, r_ratio, float (terrain_gi_size) };
      bake.canopy = { 48.0f, m_forest.closure ? 1.0f : 0.0f, 0.0f, 0.0f };
      RenderPassDesc pass;
      pass.label = "terrain gi bake";
      pass.color_count = 3;
      pass.colors[0] = { m_terrain.sky_gi, Load::discard, Store::store };
      pass.colors[1] = { m_terrain.horizon_a, Load::discard, Store::store };
      pass.colors[2] = { m_terrain.horizon_b, Load::discard, Store::store };
      m_device->begin_render_pass (pass);
      m_device->set_viewport (
        0.0f, 0.0f, float (terrain_gi_size), float (terrain_gi_size));
      m_device->set_pipeline (m_terrain.gi_bake);
      m_device->set_uniforms (0, bake);
      m_device->set_texture (0, m_terrain.heights);
      m_device->set_texture (1, m_forest.closure ? m_forest.closure : m_clear);
      m_device->draw (3);
      m_device->end_render_pass ();
      m_terrain.gi_dirty = false;
    }

    void NhalRenderer::bind_world_gi (int sky_slot,
                                      int ha_slot,
                                      int hb_slot,
                                      int closure_slot) {
      const Texture sky = m_terrain.sky_gi ? m_terrain.sky_gi : m_gi_open;
      const Texture ha = m_terrain.horizon_a ? m_terrain.horizon_a : m_clear;
      const Texture hb = m_terrain.horizon_b ? m_terrain.horizon_b : m_clear;
      if (sky_slot >= 0)
        m_device->set_texture (std::uint32_t (sky_slot), sky);
      if (ha_slot >= 0)
        m_device->set_texture (std::uint32_t (ha_slot), ha);
      if (hb_slot >= 0)
        m_device->set_texture (std::uint32_t (hb_slot), hb);
      if (closure_slot >= 0)
        m_device->set_texture (std::uint32_t (closure_slot),
                               m_forest.closure ? m_forest.closure : m_clear);
    }

    void NhalRenderer::update_terrain_cache () {
      if (!m_terrain.have)
        return;
      ensure_terrain_cache ();
      if (m_terrain.cache_dirty) {
        m_terrain.cache_global_valid = false;
        for (auto& lvl : m_terrain.cache_levels)
          lvl.valid = false;
        m_terrain.cache_dirty = false;
      }

      const Vec3 camera = position_value (m_params.camera_pos);
      const auto& p = m_terrain.params;
      const float period_x = float (p.width) * p.scale[0];
      const float period_z = float (p.height) * p.scale[2];
      std::vector<std::array<float, 4>> strips;
      auto push_strip = [&] (float x0,
                             float y0,
                             float x1,
                             float y1,
                             float slot_x,
                             float slot_y,
                             float active,
                             float footprint,
                             float origin_x,
                             float origin_z,
                             float span,
                             float road_reach) {
        strips.push_back ({ x0, y0, x1, y1 });
        strips.push_back ({ slot_x, slot_y, active, footprint });
        strips.push_back ({ origin_x, origin_z, span, road_reach });
      };

      const bool cold_fill = !m_terrain.cache_global_valid;
      if (!m_terrain.cache_global_valid) {
        const float global_footprint =
          std::max (period_x, period_z) / cache_world_side;
        push_strip (0.0f,
                    1024.0f,
                    1024.0f,
                    2048.0f,
                    0.0f,
                    1024.0f,
                    cache_world_side,
                    global_footprint,
                    0.0f,
                    0.0f,
                    0.0f,
                    0.0f);
        m_terrain.cache_global_valid = true;
      }

      struct Span1D {
        int a, b;
      };
      auto wrap_axis = [] (std::int64_t a0, std::int64_t a1) {
        std::array<Span1D, 4> out {};
        int count = 0;
        const std::int64_t len = a1 - a0;
        if (len >= cache_ring_side) {
          out[count++] = { 0, int (cache_ring_side) + 2 };
          return std::pair (out, count);
        }
        auto mod = [] (std::int64_t v) -> int {
          const std::int64_t r = v % cache_ring_side;
          return int (r < 0 ? r + cache_ring_side : r);
        };
        const int start = mod (a0);
        const int end = start + int (len);
        bool touches_zero = false, touches_last = false;
        if (end <= int (cache_ring_side)) {
          out[count++] = { start + 1, end + 1 };
          touches_zero = (start == 0);
          touches_last = (end == int (cache_ring_side));
        } else {
          out[count++] = { start + 1, int (cache_ring_side) + 1 };
          out[count++] = { 1, end - int (cache_ring_side) + 1 };
          touches_zero = true;
          touches_last = true;
        }
        // Mirror ring texel 509 to slot pixel 0 and ring texel 0 to pixel 511.
        if (touches_last)
          out[count++] = { 0, 1 };
        if (touches_zero)
          out[count++] = { int (cache_ring_side) + 1,
                           int (cache_ring_side) + 2 };
        return std::pair (out, count);
      };

      for (std::size_t i = 0; i < m_terrain.cache_levels.size (); ++i) {
        auto& lvl = m_terrain.cache_levels[i];
        const float span = cache_level_spans[i];
        const float texel = span / float (cache_ring_side);
        const auto desired_ix = std::int64_t (
          std::floor (double (camera[0]) / texel - 0.5 * cache_ring_side));
        const auto desired_iz = std::int64_t (
          std::floor (double (camera[2]) / texel - 0.5 * cache_ring_side));
        std::int64_t next_ix = lvl.ix;
        std::int64_t next_iz = lvl.iz;
        if (!lvl.valid || std::abs (desired_ix - lvl.ix) >= cache_scroll_step)
          next_ix =
            floor_div (desired_ix, cache_scroll_step) * cache_scroll_step;
        if (!lvl.valid || std::abs (desired_iz - lvl.iz) >= cache_scroll_step)
          next_iz =
            floor_div (desired_iz, cache_scroll_step) * cache_scroll_step;
        if (lvl.valid && next_ix == lvl.ix && next_iz == lvl.iz)
          continue;

        const float origin_x = float (double (next_ix) * texel);
        const float origin_z = float (double (next_iz) * texel);
        const float slot_x = float (cache_level_slots[i][0]);
        const float slot_y = float (cache_level_slots[i][1]);
        auto emit_rect = [&] (std::int64_t rx0,
                              std::int64_t rz0,
                              std::int64_t rx1,
                              std::int64_t rz1) {
          if (rx1 <= rx0 || rz1 <= rz0)
            return;
          const auto [xs, xn] = wrap_axis (rx0, rx1);
          const auto [ys, yn] = wrap_axis (rz0, rz1);
          for (int yi = 0; yi < yn; ++yi)
            for (int xi = 0; xi < xn; ++xi)
              push_strip (slot_x + float (xs[xi].a),
                          slot_y + float (ys[yi].a),
                          slot_x + float (xs[xi].b),
                          slot_y + float (ys[yi].b),
                          slot_x,
                          slot_y,
                          float (cache_ring_side),
                          texel,
                          origin_x,
                          origin_z,
                          span,
                          1.0f);
        };

        const std::int64_t dx = next_ix - lvl.ix;
        const std::int64_t dz = next_iz - lvl.iz;
        if (!lvl.valid || std::abs (dx) >= cache_ring_side ||
            std::abs (dz) >= cache_ring_side) {
          push_strip (slot_x,
                      slot_y,
                      slot_x + 512.0f,
                      slot_y + 512.0f,
                      slot_x,
                      slot_y,
                      float (cache_ring_side),
                      texel,
                      origin_x,
                      origin_z,
                      span,
                      1.0f);
        } else {
          if (dx > 0)
            emit_rect (lvl.ix + cache_ring_side,
                       next_iz,
                       next_ix + cache_ring_side,
                       next_iz + cache_ring_side);
          else if (dx < 0)
            emit_rect (next_ix, next_iz, lvl.ix, next_iz + cache_ring_side);

          const std::int64_t rem_x0 = dx < 0 ? lvl.ix : next_ix;
          const std::int64_t rem_x1 =
            dx > 0 ? lvl.ix + cache_ring_side : next_ix + cache_ring_side;
          if (dz > 0)
            emit_rect (rem_x0,
                       lvl.iz + cache_ring_side,
                       rem_x1,
                       next_iz + cache_ring_side);
          else if (dz < 0)
            emit_rect (rem_x0, next_iz, rem_x1, lvl.iz);
        }

        lvl.ix = next_ix;
        lvl.iz = next_iz;
        lvl.origin_x = origin_x;
        lvl.origin_z = origin_z;
        lvl.valid = true;
      }

      if (strips.empty ())
        return;

      if (cold_fill)
        m_dynamic.pause ();

      const Load load = cold_fill ? Load::discard : Load::load;
      RenderPassDesc pass;
      pass.label = "terrain material cache";
      pass.color_count = 4;
      for (std::size_t i = 0; i < 4; ++i)
        pass.colors[i] = { m_terrain.cache_atlas[i], load, Store::store };
      pass.waits[0] = { Stage::fragment, Stage::fragment };
      pass.wait_count = 1;
      m_device->begin_render_pass (pass);
      m_device->set_pipeline (m_terrain.cache_bake);
      m_device->set_buffer (0, m_frame_block);
      m_device->set_uniforms (1, terrain_block ());
      bind_ground_textures ();
      bind_roads ();
      m_device->set_buffer (
        5, m_device->upload (std::span<const std::array<float, 4>> (strips)));
      m_device->draw (6, std::uint32_t (strips.size () / 3));
      m_device->end_render_pass ();
    }

    shaders::terrain::Cache NhalRenderer::terrain_cache_block () const {
      shaders::terrain::Cache cache {};
      auto fill = [&] (std::size_t i) -> std::array<float, 4> {
        const auto& lvl = m_terrain.cache_levels[i];
        const float span = cache_level_spans[i];
        return { lvl.origin_x, lvl.origin_z, span, 1.0f / span };
      };
      cache.level0 = fill (0);
      cache.level1 = fill (1);
      cache.level2 = fill (2);
      cache.level3 = fill (3);
      return cache;
    }

    void NhalRenderer::draw_terrain (const ChunkDraw* chunks, int count) {
      if (!m_terrain.have || count <= 0)
        return;
      open_scene ();
      if (use_terrain_cache ()) {
        m_device->set_pipeline (m_terrain.draw);
        bind_ground_cached ();
      } else {
        m_device->set_pipeline (m_terrain.draw_live);
        bind_ground ();
      }
      m_terrain.chunks.assign (chunks, chunks + count);
      draw_chunks ();
      draw_road_ribbons ();
    }

    void NhalRenderer::bind_ground_cached () {
      m_device->set_buffer (0, m_frame_block);
      m_device->set_uniforms (1, terrain_block ());
      m_device->set_uniforms (5, terrain_cache_block ());
      m_device->set_texture (0, m_terrain.heights);
      m_device->set_texture (1, m_terrain.normals);
      m_device->set_texture (3, m_terrain.detail);
      m_device->set_texture (4, m_terrain.cache_atlas[0]);
      m_device->set_texture (5, m_terrain.cache_atlas[1]);
      m_device->set_texture (6, m_terrain.cache_atlas[2]);
      m_device->set_texture (7, m_terrain.ground ? m_terrain.ground : m_white);
      m_device->set_texture (8, m_sun_shadow.map);
      m_device->set_texture (9, m_terrain.cache_atlas[3]);
      bind_world_gi (10, 11, 12);
      m_device->set_texture (13,
                             m_terrain.snow.empty ()
                               ? m_clear
                               : m_terrain.snow[m_terrain.snow_slot]);
      m_device->set_texture (14, m_water.levels ? m_water.levels : m_black);
      m_device->set_texture (
        15, m_water.shore_surf ? m_water.shore_surf : m_water.still);
      bind_roads ();
    }

    void NhalRenderer::bind_ground () {
      m_device->set_buffer (0, m_frame_block);
      m_device->set_uniforms (1, terrain_block ());
      bind_ground_textures ();
      bind_world_gi (2, -1, -1);
      m_device->set_texture (13,
                             m_terrain.snow.empty ()
                               ? m_clear
                               : m_terrain.snow[m_terrain.snow_slot]);
      m_device->set_texture (14, m_water.levels ? m_water.levels : m_black);
      m_device->set_texture (
        15, m_water.shore_surf ? m_water.shore_surf : m_water.still);
      bind_roads ();
    }

    void NhalRenderer::bind_ground_textures () {
      m_device->set_texture (0, m_terrain.heights);
      m_device->set_texture (1, m_terrain.normals);
      m_device->set_texture (3, m_terrain.detail);
      m_device->set_texture (
        6, m_terrain.landscape ? m_terrain.landscape : m_white);
      m_device->set_texture (7, m_terrain.ground ? m_terrain.ground : m_white);
      m_device->set_texture (5, m_terrain.plants ? m_terrain.plants : m_clear);
      m_device->set_texture (
        4, m_terrain.more_plants ? m_terrain.more_plants : m_clear);
      m_device->set_texture (11, m_terrain.scars ? m_terrain.scars : m_clear);
      m_device->set_texture (12,
                             m_terrain.bedrock ? m_terrain.bedrock : m_clear);
      m_device->set_texture (8, m_sun_shadow.map);
      bind_stand ();
    }

    void NhalRenderer::draw_road_ribbons () {
      if (!m_terrain.road_rows || m_terrain.road_segments.empty ())
        return;
      constexpr int rows = 33;
      constexpr int columns = 53;
      if (!m_terrain.ribbon_indices) {
        std::vector<std::uint32_t> indices;
        for (int row = 0; row + 1 < rows; ++row) {
          for (int x = 0; x < columns; ++x) {
            indices.push_back (std::uint32_t (row * columns + x));
            indices.push_back (std::uint32_t ((row + 1) * columns + x));
          }
          indices.push_back (0xffffffffu);
        }
        m_terrain.ribbon_indices = m_device->create_buffer (
          { .size = indices.size () * 4, .label = "road ribbon strips" },
          std::as_bytes (std::span (indices)));
        m_terrain.ribbon_index_count = std::uint32_t (indices.size ());
      }
      const auto& t = m_terrain.params;
      const float period_x = t.width * t.scale[0];
      const float period_z = t.height * t.scale[2];
      // Well inside where the finest level begins to morph away.
      const float reach = std::min (110.0f, 50.0f * t.scale[0]);
      const Vec3 camera = position_value (m_params.camera_pos);
      constexpr std::size_t stride = RoadSetup::rows_per_segment;
      const float segments = float (m_terrain.road_segments.size () / stride);
      bool bound = false;
      for (std::size_t s = 0; stride * s + 1 < m_terrain.road_segments.size ();
           ++s) {
        const auto& [ax, az, bx, bz] = m_terrain.road_segments[stride * s];
        const float kind = m_terrain.road_segments[stride * s + 1][3];
        // Footpaths are worn, not built; a bridge's deck is a mesh.
        if (kind == 1.0f || kind == 2.0f)
          continue;
        const float mx = 0.5f * (ax + bx), mz = 0.5f * (az + bz);
        const float ox = terrain::whole_periods (camera[0] - mx, period_x);
        const float oz = terrain::whole_periods (camera[2] - mz, period_z);
        const float half = 0.5f * std::hypot (bx - ax, bz - az);
        if (std::hypot (mx + ox - camera[0], mz + oz - camera[2]) >
            reach + half)
          continue;
        if (!bound) {
          if (use_terrain_cache ()) {
            m_device->set_pipeline (m_terrain.ribbon);
            bind_ground_cached ();
          } else {
            m_device->set_pipeline (m_terrain.ribbon_live);
            bind_ground ();
          }
          bound = true;
        }
        shaders::road_ribbon::Ribbon ribbon {};
        ribbon.segment = { float (s), ox, oz, segments };
        ribbon.grid = { float (rows),
                        float (columns),
                        m_terrain.road_setup.half_width +
                          m_terrain.road_setup.shoulder,
                        0 };
        m_device->set_uniforms (2, ribbon);
        m_device->draw_indexed (m_terrain.ribbon_indices,
                                IndexType::uint32,
                                m_terrain.ribbon_index_count);
      }
    }

    void NhalRenderer::draw_chunks () {
      for (const ChunkDraw& c : m_terrain.chunks) {
        const int lod = std::clamp (int (c.lod), 0, lod_count - 1);
        const bool patch = c.cells == render::terrain_patch_cells;
        shaders::terrain::Chunk chunk {};
        chunk.placement = { float (c.x0),
                            float (c.z0),
                            lod_step[lod],
                            float (patch ? patch_verts (lod)
                                         : lod_verts[lod]) };
        chunk.morph = { c.morph_start,
                        c.morph_end,
                        lod_step[std::min (lod + 1, lod_count - 1)],
                        0 };
        chunk.offset = { c.offset_x, 0, c.offset_z, 0 };
        m_device->set_uniforms (2, chunk);
        m_device->draw_indexed (patch ? m_terrain.patch_indices[lod]
                                      : m_terrain.chunk_indices[lod],
                                IndexType::uint32,
                                patch ? m_terrain.patch_index_count[lod]
                                      : m_terrain.chunk_index_count[lod]);
      }
    }

    shaders::terrain::Terrain NhalRenderer::terrain_block () const {
      shaders::terrain::Terrain terrain {};
      terrain.snow_window = m_terrain.snow_window;
      const auto& p = m_terrain.params;
      terrain.scale = { p.scale[0], p.scale[1], p.scale[2], p.tex_scale };
      terrain.fields = { m_terrain.landscape && m_terrain.ground ? 1.0f : 0.0f,
                         m_forest.closure ? 1.0f : 0.0f,
                         m_forest.stand_inverse_period[0],
                         m_forest.stand_inverse_period[1] };
      terrain.size = { float (p.width),
                       float (p.height),
                       m_water.shore_size[0],
                       m_water.shore_size[1] };
      terrain.roads = { 0.0f,
                        float (m_terrain.road_setup.buckets_x),
                        float (m_terrain.road_setup.buckets_z),
                        m_terrain.road_rows ? 1.0f : 0.0f };
      terrain.road_shape = { m_terrain.road_setup.half_width,
                             m_terrain.road_setup.shoulder,
                             m_terrain.road_setup.crown,
                             m_terrain.road_setup.blend };
      terrain.road_batter = {
        m_terrain.road_setup.cut_slope,
        m_terrain.road_setup.fill_slope,
        std::clamp (m_params.rain.numerical_value_in (one), 0.0f, 1.0f),
        0
      };
      return terrain;
    }
  }
}
