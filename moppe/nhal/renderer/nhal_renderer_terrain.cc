// The terrain: its heights and normals, its material sheets, the roads'
// segments, and its chunks drawn at the levels the game's culling chose,
// with the road ribbons laid again over them near the camera.

#include <moppe/nhal/renderer/nhal_renderer_impl.hh>
#include <moppe/terrain/torus.hh>

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
      m_terrain = m_device->create_render_pipeline (terrain);

      RenderPipelineDesc ribbon = terrain;
      ribbon.program = &shaders::road_ribbon::program;
      ribbon.vertex = m_shaders.road_ribbon[0];
      ribbon.fragment = m_shaders.road_ribbon[1];
      ribbon.label = "road ribbon";
      m_road_ribbon = m_device->create_render_pipeline (ribbon);

      RenderPipelineDesc terrain_caster = caster;
      terrain_caster.program = &shaders::terrain_shadow::program;
      terrain_caster.vertex = m_shaders.terrain_shadow[0];
      terrain_caster.topology = Topology::triangle_strip;
      terrain_caster.label = "terrain shadow";
      m_terrain_shadow = m_device->create_render_pipeline (terrain_caster);
    }

    void NhalRenderer::set_terrain (
      const render::TerrainParams& params,
      std::span<const terrain::SurfaceElevation> heights,
      std::span<const terrain::TerrainNormal> normals) {
      m_terrain_params = params;
      const std::uint32_t w = params.width, h = params.height;
      for (Texture* t : { &m_heights, &m_normals })
        if (*t)
          m_device->destroy (*t);
      m_heights = m_device->create_texture (
        { w, h, Format::r32_float, usage_sampled, 1, "terrain heights" });
      static_assert (sizeof (terrain::SurfaceElevation) == sizeof (float));
      m_device->write_texture (m_heights, std::as_bytes (heights));
      // Normals: x and z as RG16 snorm; the shader rebuilds y.
      m_normals = m_device->create_texture (
        { w, h, Format::rg16_snorm, usage_sampled, 1, "terrain normals" });
      std::vector<std::int16_t> packed (normals.size () * 2);
      for (std::size_t i = 0; i < normals.size (); ++i) {
        const Vec3 n = normals[i].numerical_value_in (mp_units::one);
        packed[i * 2] =
          std::int16_t (std::clamp (n[0], -1.0f, 1.0f) * 32767.0f);
        packed[i * 2 + 1] =
          std::int16_t (std::clamp (n[2], -1.0f, 1.0f) * 32767.0f);
      }
      m_device->write_texture (m_normals, std::as_bytes (std::span (packed)));

      // Strip index templates per level, whole chunks and patches, with
      // the all-ones restart between rows.
      if (!m_chunk_indices[0]) {
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
          std::tie (m_chunk_indices[lod], m_chunk_index_count[lod]) =
            strips (lod_verts[lod], "terrain chunk strips");
          std::tie (m_patch_indices[lod], m_patch_index_count[lod]) =
            strips (patch_verts (lod), "terrain patch strips");
        }
      }
      m_have_terrain = true;
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
                        const char* label) {
        if (texture)
          m_device->destroy (texture);
        texture = {};
        if (pixels.empty () ||
            pixels.format () != render::PixelFormat::rgba8unorm)
          return;
        texture = m_device->create_texture ({ std::uint32_t (pixels.width ()),
                                              std::uint32_t (pixels.height ()),
                                              Format::rgba8_unorm,
                                              usage_sampled,
                                              1,
                                              label });
        std::vector<std::byte> bytes (pixels.byte_size ());
        pixels.write_into (bytes.data ());
        m_device->write_texture (texture, bytes);
      };
      sheet (m_landscape, landscape, "landscape materials");
      sheet (m_ground, ground, "ground materials");
      sheet (m_plants, plants, "plants");
      sheet (m_more_plants, more_plants, "more plants");
      sheet (m_scars, scars, "scars");
      sheet (m_bedrock, bedrock, "bedrock");
    }

    void NhalRenderer::set_roads (const RoadSetup& setup) {
      for (Buffer* b : { &m_road_rows, &m_road_index })
        if (*b)
          m_device->destroy (*b), *b = {};
      m_road_setup = setup;
      m_road_setup.segment_rows = {};
      m_road_setup.index = {};
      m_road_segments.assign (setup.segment_rows.begin (),
                              setup.segment_rows.end ());
      if (setup.segment_rows.empty () || setup.index.empty () ||
          setup.buckets_x <= 0 || setup.buckets_z <= 0)
        return;
      m_road_rows = m_device->create_buffer (
        { .size = setup.segment_rows.size_bytes (), .label = "road segments" },
        std::as_bytes (setup.segment_rows));
      m_road_index = m_device->create_buffer (
        { .size = setup.index.size_bytes (), .label = "road buckets" },
        std::as_bytes (setup.index));
    }

    void NhalRenderer::bind_roads (int first) {
      if (!m_no_roads) {
        const std::array<float, 8> none {};
        m_no_roads = m_device->create_buffer (
          { .size = sizeof (none), .label = "no roads" },
          std::as_bytes (std::span (none)));
      }
      m_device->set_buffer (first, m_road_rows ? m_road_rows : m_no_roads);
      m_device->set_buffer (first + 1,
                            m_road_index ? m_road_index : m_no_roads);
    }

    void NhalRenderer::draw_terrain (const ChunkDraw* chunks, int count) {
      if (!m_have_terrain || count <= 0)
        return;
      open_scene ();
      m_device->set_pipeline (m_terrain);
      bind_ground ();
      m_chunks.assign (chunks, chunks + count);
      draw_chunks ();
      draw_road_ribbons ();
    }

    void NhalRenderer::bind_ground () {
      m_device->set_buffer (0, m_frame_block);
      m_device->set_uniforms (1, terrain_block ());
      m_device->set_texture (0, m_heights);
      m_device->set_texture (1, m_normals);
      m_device->set_texture (3, m_ground_detail);
      m_device->set_texture (6, m_landscape ? m_landscape : m_white);
      m_device->set_texture (7, m_ground ? m_ground : m_white);
      m_device->set_texture (5, m_plants ? m_plants : m_clear);
      m_device->set_texture (4, m_more_plants ? m_more_plants : m_clear);
      m_device->set_texture (11, m_scars ? m_scars : m_clear);
      m_device->set_texture (12, m_bedrock ? m_bedrock : m_clear);
      m_device->set_texture (8, m_shadow_map);
      m_device->set_texture (14, m_water_levels ? m_water_levels : m_black);
      m_device->set_texture (15, m_shore_surf ? m_shore_surf : m_still_water);
      bind_stand ();
      bind_roads ();
    }

    void NhalRenderer::draw_road_ribbons () {
      if (!m_road_rows || m_road_segments.empty ())
        return;
      constexpr int rows = 33;
      constexpr int columns = 53;
      if (!m_ribbon_indices) {
        std::vector<std::uint32_t> indices;
        for (int row = 0; row + 1 < rows; ++row) {
          for (int x = 0; x < columns; ++x) {
            indices.push_back (std::uint32_t (row * columns + x));
            indices.push_back (std::uint32_t ((row + 1) * columns + x));
          }
          indices.push_back (0xffffffffu);
        }
        m_ribbon_indices = m_device->create_buffer (
          { .size = indices.size () * 4, .label = "road ribbon strips" },
          std::as_bytes (std::span (indices)));
        m_ribbon_index_count = std::uint32_t (indices.size ());
      }
      const auto& t = m_terrain_params;
      const float period_x = t.width * t.scale[0];
      const float period_z = t.height * t.scale[2];
      // Well inside where the finest level begins to morph away.
      const float reach = std::min (110.0f, 50.0f * t.scale[0]);
      const Vec3 camera = m_params.camera_pos;
      constexpr std::size_t stride = RoadSetup::rows_per_segment;
      const float segments = float (m_road_segments.size () / stride);
      bool bound = false;
      for (std::size_t s = 0; stride * s + 1 < m_road_segments.size (); ++s) {
        const auto& [ax, az, bx, bz] = m_road_segments[stride * s];
        const float kind = m_road_segments[stride * s + 1][3];
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
          m_device->set_pipeline (m_road_ribbon);
          bind_ground ();
          bound = true;
        }
        shaders::road_ribbon::Ribbon ribbon {};
        ribbon.segment = { float (s), ox, oz, segments };
        ribbon.grid = { float (rows),
                        float (columns),
                        m_road_setup.half_width + m_road_setup.shoulder,
                        0 };
        m_device->set_uniforms (2, ribbon);
        m_device->draw_indexed (
          m_ribbon_indices, IndexType::uint32, m_ribbon_index_count);
      }
    }

    bool NhalRenderer::wet (const ChunkDraw& c) const {
      const int patch = render::terrain_patch_cells;
      const int across = m_terrain_params.width / patch;
      const int down = m_terrain_params.height / patch;
      if (across <= 0 || down <= 0 ||
          m_wet_patches.size () != std::size_t (across) * down)
        return true;
      for (int z = c.z0 / patch; z < (c.z0 + c.cells) / patch; ++z)
        for (int x = c.x0 / patch; x < (c.x0 + c.cells) / patch; ++x)
          if (m_wet_patches[std::size_t (z % down) * across + x % across])
            return true;
      return false;
    }

    void NhalRenderer::draw_chunks (bool only_wet) {
      for (const ChunkDraw& c : m_chunks) {
        if (only_wet && !wet (c))
          continue;
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
        m_device->draw_indexed (
          patch ? m_patch_indices[lod] : m_chunk_indices[lod],
          IndexType::uint32,
          patch ? m_patch_index_count[lod] : m_chunk_index_count[lod]);
      }
    }

    shaders::terrain::Terrain NhalRenderer::terrain_block () const {
      shaders::terrain::Terrain terrain {};
      const auto& p = m_terrain_params;
      terrain.scale = { p.scale[0], p.scale[1], p.scale[2], p.tex_scale };
      terrain.fields = { m_landscape && m_ground ? 1.0f : 0.0f,
                         m_closure ? 1.0f : 0.0f,
                         m_stand_inverse_period[0],
                         m_stand_inverse_period[1] };
      terrain.size = {
        float (p.width), float (p.height), m_shore_size[0], m_shore_size[1]
      };
      terrain.roads = { 0.0f,
                        float (m_road_setup.buckets_x),
                        float (m_road_setup.buckets_z),
                        m_road_rows ? 1.0f : 0.0f };
      terrain.road_shape = { m_road_setup.half_width,
                             m_road_setup.shoulder,
                             m_road_setup.crown,
                             m_road_setup.blend };
      terrain.road_batter = { m_road_setup.cut_slope,
                              m_road_setup.fill_slope,
                              std::clamp (m_params.rain, 0.0f, 1.0f),
                              0 };
      return terrain;
    }
  }
}
