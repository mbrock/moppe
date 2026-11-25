// The terrain: its heights and normals, its material sheets, the roads'
// segments, and its chunks drawn at the levels the game's culling chose,
// with the road ribbons laid again over them near the camera.

#include <moppe/nhal/renderer/field_texture.hh>
#include <moppe/nhal/renderer/nhal_renderer_impl.hh>
#include <moppe/terrain/torus.hh>

#include <fstream>
#include <filesystem>

namespace moppe::nhal {
  namespace detail {
    void NhalRenderer::make_terrain_pipelines () {
      const RenderPipelineDesc scene = scene_target ();
      const RenderPipelineDesc caster = shadow_caster ();
      RenderPipelineDesc terrain = scene;
      terrain.program = &shaders::terrain::program;
      terrain.vertex = m_shaders.terrain[0];
      terrain.fragment = m_shaders.terrain[1];
      terrain.topology = Topology::triangle_strip;
      m_terrain.draw = m_device->create_render_pipeline (terrain);

      RenderPipelineDesc ribbon = terrain;
      ribbon.program = &shaders::road_ribbon::program;
      ribbon.vertex = m_shaders.road_ribbon[0];
      ribbon.fragment = m_shaders.road_ribbon[1];
      ribbon.label = "road ribbon";
      m_terrain.ribbon = m_device->create_render_pipeline (ribbon);

      const char* root = environment ("MOPPE_MATERIAL_PROBE");
      const char* mode = environment ("MOPPE_MATERIAL_MODE");
      if (root && mode && std::string_view (mode) != "live") {
        const bool roads = std::string_view (mode) == "roads";
        const bool grain = roads && environment ("MOPPE_PROBE_GRAIN");
        m_probe_channels = roads ? (grain ? 4 : 3) : 2;
        m_probe_formats.fill (Format::rgba16_float);
        m_probe_formats[3] = Format::r8_unorm;
        if (const char* packed = environment ("MOPPE_PROBE_PACK")) {
          m_probe_formats[0] = m_probe_formats[1] = Format::rgba8_unorm;
          if (std::string_view (packed) == "24")
            m_probe_formats[2] = Format::rgba8_unorm;
        }
        auto read = [&] (std::string name) {
          std::ifstream in (std::filesystem::path (root) / name);
          if (!in) throw std::runtime_error ("Missing shader " + name);
          return std::string (std::istreambuf_iterator<char> (in), {});
        };
        const std::string suffix = roads ? "-roads.metal" : "-ground.metal";
        auto variant = [&] (RenderPipelineDesc desc, const char* prefix) {
          const auto src = read (prefix + suffix);
          desc.fragment.msl = src;
          return m_device->create_render_pipeline (desc);
        };
        m_probe_terrain = variant (terrain, "terrain");
        m_probe_ribbon = variant (ribbon, "road_ribbon");
        RenderPipelineDesc bake;
        bake.program = terrain.program;
        const auto vertex = read ("triangle.metal");
        const auto fragment = read ("bake" + suffix);
        bake.vertex.msl = vertex;
        bake.fragment.msl = fragment;
        bake.color_count = m_probe_channels;
        std::copy_n (m_probe_formats.begin (), m_probe_channels, bake.color_formats.begin ());
        bake.label = "terrain material tile bake";
        m_bake_material = m_device->create_render_pipeline (bake);
      }

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

    void NhalRenderer::bake_material_probe () {
      if (!m_bake_material || !m_terrain.have || !m_terrain.ground)
        return;
      const auto camera = position_value (m_params.camera_pos);
      const int spans[] = { 16, 64, 256, 1024, 0 };
      for (int level = 0; level < 5; ++level) {
        const bool fresh = !m_tiles[level][0];
        const int side = level == 4 ? 1024 : 512;
        const int span = spans[level];
        int x = 0, y = 0, dx = 0, dy = 0;
        if (span) {
          // Align to 8 texels so a small move needn't open every level.
          auto corner = [&] (float v) {
            return int (std::floor (v * side / (8 * span))) * 8 - side/2;
          };
          x = corner (camera[0]);
          y = corner (camera[2]);
          dx = x - m_origins[level][0];
          dy = y - m_origins[level][1];
          m_origins[level] = {x, y};
        }
        if (!fresh && !dx && !dy) continue;
        auto& win = m_windows[level];
        win = { float (x) * span / side, float (y) * span / side,
                float (span), float (side) };
        const bool full = fresh || std::abs (dx) >= side || std::abs (dy) >= side
                          || environment ("MOPPE_PROBE_REFILL");
        RenderPassDesc pass;
        constexpr const char* labels[] = { "material bake 16m", "material bake 64m",
            "material bake 256m", "material bake 1024m", "material bake world" };
        pass.label = labels[level];
        pass.color_count = m_probe_channels;
        for (unsigned i = 0; i < m_probe_channels; ++i) {
          auto& t = m_tiles[level][i];
          if (fresh) t = m_device->create_texture ({
            unsigned (side), unsigned (side), m_probe_formats[i],
            usage_render_target | usage_sampled, 1, "material tile" });
          pass.colors[i] = { t, full ? Load::discard : Load::load, Store::store };
        }
        m_device->begin_render_pass (pass);
        m_device->set_pipeline (m_bake_material);
        bind_ground ();
        m_device->set_uniforms (5, win);
        // Split each logical rectangle at both toroidal seams.
        auto rect = [&] (int left, int top, int w, int h) {
          int py = (top % side + side) % side;
          while (h > 0) {
            int rows = std::min (h, side - py);
            int px = (left % side + side) % side;
            for (int width = w; width > 0; px = 0) {
              int cols = std::min (width, side - px);
              m_device->set_viewport (px, py, cols, rows);
              m_device->draw (3);
              width -= cols;
            }
            py = 0;
            h -= rows;
          }
        };
        if (full) rect (0, 0, side, side);
        else {
          rect (dx > 0 ? x + side - dx : x, y, std::abs (dx), side);
          rect (x + std::max (-dx, 0), dy > 0 ? y + side - dy : y,
                side - std::abs (dx), std::abs (dy));
        }
        m_device->end_render_pass ();
      }
    }

    void NhalRenderer::bind_material_probe () {
      m_device->set_uniforms (5, m_windows);
      for (unsigned i = 0; i < m_probe_channels; ++i)
        for (unsigned level = 0; level < 5; ++level)
          m_device->set_texture (16 + 5*i + level, m_tiles[level][i]);
    }

    void NhalRenderer::draw_terrain (const ChunkDraw* chunks, int count) {
      if (!m_terrain.have || count <= 0)
        return;
      open_scene ();
      if (m_tiles[0][0]) bind_material_probe ();
      m_device->set_pipeline (m_tiles[0][0] ? m_probe_terrain : m_terrain.draw);
      bind_ground ();
      m_terrain.chunks.assign (chunks, chunks + count);
      draw_chunks ();
      draw_road_ribbons ();
    }

    void NhalRenderer::bind_ground () {
      m_device->set_buffer (0, m_frame_block);
      m_device->set_uniforms (1, terrain_block ());
      bind_ground_textures ();
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
          m_device->set_pipeline (m_tiles[0][0] ? m_probe_ribbon : m_terrain.ribbon);
          bind_ground ();
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
