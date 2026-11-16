// Drawing the scene: the terrain and the roads, the forest, the grass and
// the sward, the boulders, the sky and the sea, falling leaves and
// waterfalls, the game's draw lists and meshes, the lettering in the world,
// and the compute passes that cull the grass, rocks, and trees before them.

#include <moppe/nhal/renderer/nhal_renderer_impl.hh>

namespace moppe::nhal {
  namespace detail {
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
        const float ox = period_x * std::round ((camera[0] - mx) / period_x);
        const float oz = period_z * std::round ((camera[2] - mz) / period_z);
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

    void NhalRenderer::draw_forest () {
      if (!m_tree_count)
        return;
      open_scene ();
      m_device->set_pipeline (m_forest);
      m_device->set_buffer (0, m_frame_block);
      m_device->set_buffer (1, m_forest_block);
      m_device->set_buffer (3, m_trees);
      m_device->set_buffer (4, m_tree_candidates);
      m_device->set_texture (8, m_shadow_map);
      for (std::uint32_t c = 0; c < forest_classes; ++c) {
        shaders::forest::Draw draw {};
        draw.class_ = { float (c * m_tree_count), 0, 0, 0 };
        m_device->set_uniforms (2, draw);
        m_device->draw_indexed_indirect (m_tree_indices,
                                         IndexType::uint16,
                                         m_tree_arguments,
                                         c * sizeof (DrawIndexedIndirectArgs));
      }
    }

    void
    NhalRenderer::draw_undergrowth (const render::UndergrowthParams& params) {
      // This frame draws the tiles begin_frame chose with the last
      // parameters; the next frame's cull takes these.
      m_grass_params = params;
      m_grass_wanted = true;
      if (!m_grass_culled)
        return;
      open_scene ();
      // The canopy first: translucent, it blends over the ground it sits
      // on, and the blades it hands over to stand in it.
      draw_sward ();
      draw_blades (params);
    }

    void NhalRenderer::draw_sward () {
      m_device->set_pipeline (m_sward_pipeline);
      m_device->set_buffer (0, m_frame_block);
      m_device->set_uniforms (1, m_sward_block);
      m_device->set_buffer (2, m_sward_patches);
      m_device->set_uniforms (4, terrain_block ());
      bind_roads (5);
      m_device->set_texture (0, m_heights);
      m_device->set_texture (1, m_normals);
      m_device->set_texture (2, texture_or_white (m_grass));
      m_device->set_texture (3, m_ground_detail);
      m_device->set_texture (6, m_landscape);
      m_device->set_texture (7, m_ground);
      m_device->set_texture (5, m_plants ? m_plants : m_clear);
      m_device->set_texture (4, m_more_plants ? m_more_plants : m_clear);
      m_device->set_texture (11, m_scars ? m_scars : m_clear);
      m_device->set_texture (12, m_bedrock ? m_bedrock : m_clear);
      m_device->set_texture (8, m_shadow_map);
      bind_stand ();
      m_device->draw_indexed_indirect (
        m_sward_indices, IndexType::uint16, m_sward_arguments);
    }

    void NhalRenderer::draw_blades (const render::UndergrowthParams& params) {
      shaders::grass::Grass grass = m_grass_block;
      grass.interaction =
        lanes (params.interaction_position, params.interaction_radius);
      m_device->set_pipeline (m_grass_pipeline);
      m_device->set_buffer (0, m_frame_block);
      m_device->set_uniforms (1, grass);
      m_device->set_buffer (2, m_grass_tiles);
      m_device->set_texture (0, m_heights);
      m_device->set_texture (1, m_normals);
      m_device->set_texture (3, m_ground_detail);
      m_device->set_texture (6, m_landscape);
      m_device->set_texture (7, m_ground);
      m_device->set_texture (5, m_plants ? m_plants : m_clear);
      m_device->set_texture (4, m_more_plants ? m_more_plants : m_clear);
      m_device->set_texture (11, m_scars ? m_scars : m_clear);
      m_device->set_texture (12, m_bedrock ? m_bedrock : m_clear);
      m_device->set_texture (8, m_shadow_map);
      bind_stand ();
      const float tiles = grass.window[2] * grass.window[2];
      for (std::uint32_t c = 0; c < grass_classes; ++c) {
        shaders::grass::Draw draw {};
        draw.class_ = { float (c) * tiles, 0, 0, 0 };
        m_device->set_uniforms (3, draw);
        m_device->draw_indexed_indirect (m_grass_indices,
                                         IndexType::uint16,
                                         m_grass_arguments,
                                         c * sizeof (DrawIndexedIndirectArgs));
      }
    }

    void NhalRenderer::draw_boulders () {
      if (!m_rock_count)
        return;
      open_scene ();
      m_device->set_pipeline (m_boulders);
      m_device->set_buffer (0, m_frame_block);
      m_device->set_buffer (1, m_rock_block);
      m_device->set_buffer (3, m_rocks);
      m_device->set_buffer (4, m_rock_candidates);
      m_device->set_buffer (5, m_rock_corners);
      m_device->set_buffer (6, m_rock_faces);
      m_device->set_texture (8, m_shadow_map);
      for (std::uint32_t fine = 0; fine < 2; ++fine) {
        shaders::boulders::Draw draw {};
        draw.class_ = { float (fine * m_rock_count), float (fine), 0, 0 };
        m_device->set_uniforms (2, draw);
        m_device->draw_indexed_indirect (m_rock_indices,
                                         IndexType::uint16,
                                         m_rock_arguments,
                                         fine *
                                           sizeof (DrawIndexedIndirectArgs));
      }
    }

    void NhalRenderer::draw_sky (const render::SkyParams&) {
      open_scene ();
      m_device->set_pipeline (m_sky);
      m_device->set_buffer (0, m_frame_block);
      m_device->draw (3);
    }

    void NhalRenderer::draw_ocean (const render::OceanParams& params) {
      if (!m_have_terrain || !m_water_levels || m_scene_closed)
        return;
      // A view of dry land needs neither the copy nor the pass's break.
      if (std::ranges::none_of (
            m_chunks, [this] (const ChunkDraw& c) { return wet (c); }))
        return;
      copy_beneath_water ();
      m_device->set_pipeline (m_water);
      m_device->set_buffer (0, m_frame_block);
      m_device->set_uniforms (1, terrain_block ());
      m_device->set_uniforms (3, sea_block (params.time));
      m_device->set_texture (0, m_heights);
      m_device->set_texture (8, m_shadow_map);
      m_device->set_texture (11, m_water_levels);
      m_device->set_texture (12, m_water_flow ? m_water_flow : m_still_water);
      m_device->set_texture (13, m_beneath);
      m_device->set_texture (14, m_beneath_depth);
      m_device->set_texture (15, m_shore_surf ? m_shore_surf : m_still_water);
      draw_chunks (true);
    }

    void NhalRenderer::draw_falling_leaves () {
      if (!m_have_terrain || !m_litter || m_scene_closed)
        return;
      open_scene ();
      constexpr float cell_metres = 1.2f;
      constexpr int side = 64;
      const Vec3 camera = m_params.camera_pos;
      const auto& t = m_terrain_params;
      shaders::leaves::Leaves leaves {};
      leaves.window = { std::floor (camera[0] / cell_metres) - side / 2,
                        std::floor (camera[2] / cell_metres) - side / 2,
                        float (side),
                        cell_metres };
      leaves.lattice = {
        1.0f / t.scale[0], 1.0f / t.scale[2], t.scale[1], float (t.width)
      };
      leaves.stand_field = { 1.0f,
                             m_stand_inverse_period[0],
                             m_stand_inverse_period[1],
                             0.5f * side * cell_metres };
      m_device->set_pipeline (m_leaves);
      m_device->set_buffer (0, m_frame_block);
      m_device->set_uniforms (1, leaves);
      m_device->set_texture (0, m_heights);
      m_device->set_texture (8, m_shadow_map);
      bind_stand ();
      m_device->draw (6, side * side);
    }

    void NhalRenderer::draw_waterfalls (const render::Mesh& mesh,
                                        const Mat4& model) {
      const auto& m = static_cast<const NhalMesh&> (mesh);
      if (!m.vertices || m_scene_closed)
        return;
      open_scene ();
      draw_runs ({}, m.vertices, m.runs, model, model, m_waterfall);
    }

    void NhalRenderer::draw_list (const DrawList& list,
                                  std::uint64_t motion_id) {
      if (list.empty () || m_casting)
        return;
      open_scene ();
      const std::vector<render::Vertex>& now = list.vertices ();
      const Transient vertices = m_device->upload (std::span (now));
      std::optional<Transient> before;
      if (motion_id) {
        if (auto found = m_previous_vertices.find (motion_id);
            found != m_previous_vertices.end () &&
            found->second.size () == now.size ())
          before =
            m_device->upload (std::span<const render::Vertex> (found->second));
        m_current_vertices[motion_id].assign (now.begin (), now.end ());
      }
      draw_runs (vertices,
                 Buffer {},
                 list.runs (),
                 Mat4 {},
                 Mat4 {},
                 {},
                 before ? &*before : nullptr);
    }

    void NhalRenderer::draw_world_text (const render::WorldText& text) {
      const auto& placed = static_cast<const NhalWorldText&> (text);
      if (placed.runs.empty () || m_casting || m_scene_closed)
        return;
      open_scene ();
      m_device->set_pipeline (m_world_text);
      m_device->set_buffer (0, m_frame_block);
      m_device->set_texture (8, m_shadow_map);
      for (const NhalWorldText::Run& run : placed.runs) {
        const auto& glyphs = static_cast<const NhalGlyphSet&> (*run.glyphs);
        m_device->set_buffer (1, run.quads);
        m_device->set_buffer (2, glyphs.curves);
        m_device->set_buffer (3, glyphs.bands);
        m_device->draw (6, run.count);
      }
    }

    void NhalRenderer::draw_world_labels (const render::TextList& text) {
      if (text.empty () || m_casting || m_scene_closed)
        return;
      open_scene ();
      // Behind the scene's surfaces faintly, then in front of them.
      std::vector<render::GlyphQuad> faint = text.quads ();
      for (render::GlyphQuad& quad : faint)
        quad.color[3] *= 0.3f;
      for (const bool behind : { true, false }) {
        const std::vector<render::GlyphQuad>& quads =
          behind ? faint : text.quads ();
        m_device->set_pipeline (behind ? m_world_labels_behind : m_world_text);
        m_device->set_buffer (0, m_frame_block);
        m_device->set_texture (8, m_shadow_map);
        for (const render::TextList::Run& run : text.runs ()) {
          const auto* glyphs =
            static_cast<const NhalGlyphSet*> (run.glyphs.get ());
          if (!glyphs || !run.count)
            continue;
          m_device->set_buffer (1,
                                m_device->upload (std::span (quads).subspan (
                                  run.first, run.count)));
          m_device->set_buffer (2, glyphs->curves);
          m_device->set_buffer (3, glyphs->bands);
          m_device->draw (6, run.count);
        }
      }
    }

    float NhalRenderer::scene_focal_pixels () const {
      return 0.5f * float (m_scene_height) * std::abs (m_params.proj.at (1, 1));
    }

    void NhalRenderer::draw_mesh (const render::Mesh& mesh,
                                  const Mat4& model,
                                  std::uint64_t motion_id) {
      const auto& m = static_cast<const NhalMesh&> (mesh);
      if (!m.vertices)
        return;
      if (m_casting) {
        m_casters.push_back ({ &m, model });
        return;
      }
      open_scene ();
      Mat4 previous = model;
      if (motion_id) {
        if (auto found = m_previous_models.find (motion_id);
            found != m_previous_models.end ())
          previous = found->second;
        m_current_models[motion_id] = model;
      }
      draw_runs ({}, m.vertices, m.runs, model, previous);
    }

    void NhalRenderer::cull_grass (const Mat4& view_proj,
                                   const FrameParams& params) {
      m_grass_culled = false;
      if (!m_grass_wanted || !m_have_terrain || !m_landscape || !m_ground)
        return;
      constexpr float tile = 0.60f;
      constexpr std::uint32_t shoots = 32, indices_per_shoot = 18;
      const float reach = std::max (m_grass_params.reach, 1.0f) * 1.7f;
      const std::uint32_t side = std::min (
        std::uint32_t (std::ceil (2.0f * reach / tile)), grass_window_side);
      if (!m_grass_tiles) {
        // A word a tile, and room in every class for the whole window.
        m_grass_tiles = m_device->create_buffer (
          { .size = std::uint64_t (grass_classes) * grass_window_side *
                    grass_window_side * 4,
            .usage = buffer_storage_write,
            .label = "grass tiles" });
        m_grass_arguments = m_device->create_buffer (
          { .size = grass_classes * sizeof (DrawIndexedIndirectArgs),
            .usage = buffer_storage_write | buffer_indirect,
            .label = "grass draw" });
        std::vector<std::uint16_t> indices;
        for (std::uint32_t shoot = 0; shoot < shoots; ++shoot)
          for (std::uint32_t quad = 0; quad < 3; ++quad) {
            const std::uint16_t c = std::uint16_t (shoot * 8 + quad * 2);
            indices.insert (indices.end (),
                            { c,
                              std::uint16_t (c + 1),
                              std::uint16_t (c + 3),
                              c,
                              std::uint16_t (c + 3),
                              std::uint16_t (c + 2) });
          }
        m_grass_indices = m_device->create_buffer (
          { .size = indices.size () * 2, .label = "grass blades" },
          std::as_bytes (std::span (indices)));

        m_sward_patches = m_device->create_buffer (
          { .size = std::uint64_t (sward_window_side) * sward_window_side * 8,
            .usage = buffer_storage_write,
            .label = "sward patches" });
        m_sward_arguments = m_device->create_buffer (
          { .size = sizeof (DrawIndexedIndirectArgs),
            .usage = buffer_storage_write | buffer_indirect,
            .label = "sward draw" });
        std::vector<std::uint16_t> grid;
        for (std::uint16_t z = 0; z < 4; ++z)
          for (std::uint16_t x = 0; x < 4; ++x) {
            const std::uint16_t a = std::uint16_t (z * 5 + x);
            const std::uint16_t b = std::uint16_t (a + 1);
            const std::uint16_t c = std::uint16_t (a + 5);
            const std::uint16_t d = std::uint16_t (c + 1);
            grid.insert (grid.end (), { a, c, b, b, c, d });
          }
        m_sward_indices = m_device->create_buffer (
          { .size = grid.size () * 2, .label = "sward patch" },
          std::as_bytes (std::span (grid)));
      }
      const auto& t = m_terrain_params;
      shaders::grass::Grass grass {};
      grass.window = { std::floor ((params.camera_pos[0] - reach) / tile),
                       std::floor ((params.camera_pos[2] - reach) / tile),
                       float (side),
                       tile };
      grass.lattice = {
        1.0f / t.scale[0], 1.0f / t.scale[2], t.scale[1], float (t.width)
      };
      const float focal =
        0.5f * float (m_scene_height) * std::abs (params.proj.at (1, 1));
      grass.habitat = { reach, m_grass_params.density, t.tex_scale, focal };
      grass.stand_field = stand_field ();
      grass.bare = { m_grass_params.bare_centre[0],
                     m_grass_params.bare_centre[2],
                     m_grass_params.bare_along[0],
                     m_grass_params.bare_along[2] };
      grass.bare_size = {
        m_grass_params.bare_half_x, m_grass_params.bare_half_z, 0.0f, 0.0f
      };
      grass.yard = { m_grass_params.yard_centre[0],
                     m_grass_params.yard_centre[2],
                     m_grass_params.yard_along[0],
                     m_grass_params.yard_along[2] };
      grass.yard_size = { m_grass_params.yard_half_x,
                          m_grass_params.yard_half_z,
                          m_grass_params.yard_margin,
                          0.0f };
      m_grass_block = grass;

      // The canopy's reach follows the projected height of the whole
      // sward, so looking down from the glider asks the same image-space
      // question as riding.
      constexpr float patch = 16.0f;
      const float canopy_reach =
        std::clamp (0.42f * focal / 0.28f, 180.0f, 1200.0f);
      const std::uint32_t patches =
        std::min (std::uint32_t (std::ceil (2.0f * canopy_reach / patch)),
                  sward_window_side);
      auto row = [&] (int r) {
        return std::sqrt (
          view_proj.element (r) * view_proj.element (r) +
          view_proj.element (4 + r) * view_proj.element (4 + r) +
          view_proj.element (8 + r) * view_proj.element (8 + r));
      };
      shaders::grass::Grass sward = grass;
      sward.window = {
        std::floor ((params.camera_pos[0] - canopy_reach) / patch),
        std::floor ((params.camera_pos[2] - canopy_reach) / patch),
        float (patches),
        patch
      };
      sward.habitat[0] = canopy_reach;
      sward.interaction = { row (0), row (1), 0, 0 };
      m_sward_block = sward;

      std::array<DrawIndexedIndirectArgs, grass_classes> reset {};
      for (std::uint32_t c = 0; c < grass_classes; ++c)
        reset[c] = { (1u << c) * indices_per_shoot, 0, 0, 0, 0 };
      m_device->copy_to_buffer (
        m_grass_arguments,
        0,
        m_device->upload (std::span<const DrawIndexedIndirectArgs> (reset)));
      m_device->begin_compute_pass ("grass tiles");
      m_device->set_pipeline (m_grass_tiles_pipeline);
      m_device->set_buffer (0, m_frame_block);
      m_device->set_uniforms (1, grass);
      m_device->set_buffer (2, m_grass_tiles);
      m_device->set_buffer (3, m_grass_arguments);
      m_device->set_uniforms (4, terrain_block ());
      bind_roads (5);
      m_device->set_texture (0, m_heights);
      m_device->set_texture (1, m_normals);
      m_device->set_texture (6, m_landscape);
      m_device->set_texture (7, m_ground);
      m_device->set_texture (5, m_plants ? m_plants : m_clear);
      m_device->set_texture (4, m_more_plants ? m_more_plants : m_clear);
      m_device->set_texture (11, m_scars ? m_scars : m_clear);
      m_device->set_texture (12, m_bedrock ? m_bedrock : m_clear);
      bind_stand ();
      m_device->dispatch ((side * side + 63) / 64);
      m_device->end_compute_pass ();

      const DrawIndexedIndirectArgs sward_reset { 96, 0, 0, 0, 0 };
      m_device->copy_to_buffer (
        m_sward_arguments,
        0,
        m_device->upload (
          std::span<const DrawIndexedIndirectArgs> (&sward_reset, 1)));
      m_device->begin_compute_pass ("sward patches");
      m_device->set_pipeline (m_sward_patches_pipeline);
      m_device->set_buffer (0, m_frame_block);
      m_device->set_uniforms (1, sward);
      m_device->set_buffer (2, m_sward_patches);
      m_device->set_buffer (3, m_sward_arguments);
      m_device->set_texture (0, m_heights);
      m_device->dispatch ((patches * patches + 63) / 64);
      m_device->end_compute_pass ();
      m_grass_culled = true;
    }

    shaders::water::Sea NhalRenderer::sea_block (double time) {
      using namespace terrain;
      shaders::water::Sea sea {};
      std::array<float, 4>* lanes[][2] = {
        { &sea.sea_train_0, &sea.sea_pace_0 },
        { &sea.sea_train_1, &sea.sea_pace_1 },
        { &sea.sea_train_2, &sea.sea_pace_2 },
        { &sea.sea_train_3, &sea.sea_pace_3 },
        { &sea.sea_train_4, &sea.sea_pace_4 },
        { &sea.sea_train_5, &sea.sea_pace_5 },
      };
      static_assert (std::size (lanes) == sea_wave_trains.size ());
      for (std::size_t i = 0; i < sea_wave_trains.size (); ++i) {
        const SeaWaveTrain& t = sea_wave_trains[i];
        const float heading = radians_value (sea_wind_heading + t.heading);
        const float k = per_metre_value (sea_wave_number (t));
        const float amplitude = meters_value (t.amplitude);
        *lanes[i][0] = { k * std::cos (heading),
                         k * std::sin (heading),
                         amplitude,
                         radians_value (sea_wave_phase (t, time)) };
        *lanes[i][1] = { per_second_value (sea_wave_frequency (t)),
                         amplitude * sea_wave_lean,
                         meters_value (t.wavelength),
                         k };
      }
      return sea;
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

    void NhalRenderer::cull_rocks (const Mat4& view,
                                   const Vec3& eye,
                                   float height,
                                   bool shadow) {
      if (!m_rock_count)
        return;
      auto row = [&] (int r) {
        return std::sqrt (view.element (r) * view.element (r) +
                          view.element (4 + r) * view.element (4 + r) +
                          view.element (8 + r) * view.element (8 + r));
      };
      shaders::boulder_cull::Forest block {};
      block.view = matrix (view);
      block.eye = lanes (eye);
      block.world = { m_rock_period[0],
                      m_rock_period[1],
                      float (m_rock_count),
                      float (m_rock_count) };
      block.cull = { row (0), row (1), row (3), height };
      block.mode = { shadow ? 1.0f : 0.0f, 0, 0, 0 };
      Transient& uniforms = shadow ? m_rock_shadow_block : m_rock_block;
      uniforms = m_device->allocate (sizeof block);
      std::memcpy (uniforms.data, &block, sizeof block);
      const DrawIndexedIndirectArgs reset[2] = { { 60, 0, 0, 0, 0 },
                                                 { 240, 0, 60, 0, 0 } };
      const Buffer arguments =
        shadow ? m_rock_shadow_arguments : m_rock_arguments;
      m_device->copy_to_buffer (
        arguments,
        0,
        m_device->upload (std::span<const DrawIndexedIndirectArgs> (reset)));
      m_device->begin_compute_pass (shadow ? "shadow boulder culling"
                                           : "boulder culling");
      m_device->set_pipeline (m_boulder_cull);
      m_device->set_buffer (1, uniforms);
      m_device->set_buffer (2, m_rocks);
      m_device->set_buffer (
        3, shadow ? m_rock_shadow_candidates : m_rock_candidates);
      m_device->set_buffer (4, arguments);
      m_device->dispatch ((m_rock_count + 63) / 64);
      m_device->end_compute_pass ();
    }

    void NhalRenderer::cull_trees (const Mat4& view,
                                   const Vec3& eye,
                                   float height,
                                   bool shadow,
                                   std::size_t cascade) {
      if (!m_tree_count)
        return;
      auto row = [&] (int r) {
        return std::sqrt (view.element (r) * view.element (r) +
                          view.element (4 + r) * view.element (4 + r) +
                          view.element (8 + r) * view.element (8 + r));
      };
      shaders::forest_cull::Forest forest {};
      forest.view = matrix (view);
      forest.eye = lanes (eye);
      forest.world = { m_tree_period[0],
                       m_tree_period[1],
                       float (m_tree_count),
                       float (m_tree_count) };
      forest.cull = { row (0), row (1), row (3), height };
      forest.mode = { shadow ? 1.0f + float (cascade) : 0.0f, 0, 0, 0 };
      Transient& block =
        shadow ? m_shadow_forest_block[cascade] : m_forest_block;
      block = m_device->allocate (sizeof forest);
      std::memcpy (block.data, &forest, sizeof forest);

      std::array<DrawIndexedIndirectArgs, forest_classes> reset {};
      for (std::uint32_t c = 0; c < forest_classes; ++c) {
        // The shadow's classes are each species' tier 0.
        const std::uint32_t topology = shadow ? c * forest_tiers : c;
        if (shadow && cascade == render::LocalShadowParams::far)
          reset[c] = { far_tree_indices, 0, 0, 0, 0 };
        else if (topology < forest_classes)
          reset[c] = {
            m_tree_class_count[topology], 0, m_tree_class_first[topology], 0, 0
          };
      }
      const Buffer arguments =
        shadow ? m_shadow_arguments[cascade] : m_tree_arguments;
      m_device->copy_to_buffer (
        arguments,
        0,
        m_device->upload (std::span<const DrawIndexedIndirectArgs> (
          reset.data (), shadow ? forest_species : forest_classes)));
      m_device->begin_compute_pass (shadow ? "shadow tree culling"
                                           : "forest culling");
      m_device->set_pipeline (m_forest_cull);
      m_device->set_buffer (1, block);
      m_device->set_buffer (2, m_trees);
      m_device->set_buffer (
        3, shadow ? m_shadow_candidates[cascade] : m_tree_candidates);
      m_device->set_buffer (4, arguments);
      m_device->dispatch ((m_tree_count + 63) / 64);
      m_device->end_compute_pass ();
    }

    Pipeline NhalRenderer::uber (const DrawState& state) {
      const int key = (state.blend ? 1 : 0) | (state.additive ? 2 : 0) |
                      (state.depth_test ? 4 : 0) | (state.depth_write ? 8 : 0) |
                      (state.cull ? 16 : 0);
      Pipeline& pipeline = m_uber[key];
      if (pipeline)
        return pipeline;
      RenderPipelineDesc desc;
      desc.program = &shaders::uber::program;
      desc.vertex = m_uber_code.stage (0);
      desc.fragment = m_uber_code.stage (1);
      desc.color_formats[0] = Format::rgba16_float;
      desc.color_formats[1] = Format::rg16_float;
      desc.color_count = 2;
      desc.blend[0] = state.additive ? Blend::additive
                      : state.blend  ? Blend::alpha
                                     : Blend::none;
      desc.depth_format = Format::d32_float;
      desc.depth_compare =
        state.depth_test ? CompareOp::greater_equal : CompareOp::always;
      desc.depth_write = state.depth_write;
      desc.cull = state.cull ? Cull::back : Cull::none;
      pipeline = m_device->create_render_pipeline (desc);
      std::cerr << "moppe: NHAL draw-list pipeline " << key << " (blend "
                << state.blend << ", additive " << state.additive
                << ", depth test " << state.depth_test << ", write "
                << state.depth_write << ", cull " << state.cull << ")"
                << std::endl;
      return pipeline;
    }

    void NhalRenderer::draw_runs (const Transient& streamed,
                                  Buffer baked,
                                  const std::vector<DrawList::Run>& runs,
                                  const Mat4& model,
                                  const Mat4& previous,
                                  Pipeline program,
                                  const Transient* streamed_before) {
      shaders::uber::Draw draw {};
      draw.model = matrix (model);
      draw.previous_model = matrix (previous);
      // The normal matrix: the inverse transpose of the model's 3x3.
      float m[3][3];
      for (int c = 0; c < 3; ++c)
        for (int r = 0; r < 3; ++r)
          m[c][r] = model.at (c, r);
      const float det = m[0][0] * (m[1][1] * m[2][2] - m[2][1] * m[1][2]) -
                        m[1][0] * (m[0][1] * m[2][2] - m[2][1] * m[0][2]) +
                        m[2][0] * (m[0][1] * m[1][2] - m[1][1] * m[0][2]);
      const float inv = std::abs (det) > 1e-12f ? 1.0f / det : 1.0f;
      auto cof = [&] (int c, int r) {
        const int c1 = (c + 1) % 3, c2 = (c + 2) % 3;
        const int r1 = (r + 1) % 3, r2 = (r + 2) % 3;
        return (m[c1][r1] * m[c2][r2] - m[c2][r1] * m[c1][r2]) * inv;
      };
      draw.normal_x = { cof (0, 0), cof (0, 1), cof (0, 2), 0 };
      draw.normal_y = { cof (1, 0), cof (1, 1), cof (1, 2), 0 };
      draw.normal_z = { cof (2, 0), cof (2, 1), cof (2, 2), 0 };
      draw.normal_x[3] = streamed_before ? 1.0f : 0.0f;
      const Transient block = m_device->allocate (sizeof draw);
      std::memcpy (block.data, &draw, sizeof draw);

      DrawState current;
      bool first = true;
      for (const DrawList::Run& run : runs) {
        if (!run.count)
          continue;
        if (first || run.state != current) {
          m_device->set_pipeline (program ? program : uber (run.state));
          current = run.state;
          first = false;
        }
        // Each run's vertices from its first: by the draw's first vertex
        // where the shader's index counts from it, and otherwise
        // (Direct3D) by binding the buffer from that vertex.
        const std::uint32_t first = m_first_vertex_counts ? run.first : 0;
        const std::uint64_t offset =
          std::uint64_t (run.first - first) * sizeof (render::Vertex);
        m_device->set_buffer (0, m_frame_block);
        if (baked) {
          m_device->set_buffer (1, baked, offset);
          m_device->set_buffer (3, baked, offset);
        } else {
          m_device->set_buffer (1, advanced (streamed, offset));
          m_device->set_buffer (
            3,
            advanced (streamed_before ? *streamed_before : streamed, offset));
        }
        m_device->set_buffer (2, block);
        m_device->set_texture (0, texture_or_white (run.texture));
        m_device->set_texture (8, m_shadow_map);
        m_device->draw (run.count, 1, first);
      }
    }

    Transient NhalRenderer::advanced (Transient slice, std::uint64_t bytes) {
      slice.data = static_cast<std::byte*> (slice.data) + bytes;
      slice.gpu_address += bytes;
      slice.size -= bytes;
      return slice;
    }

    Texture
    NhalRenderer::texture_or_white (const render::Texture* texture) const {
      return texture ? static_cast<const NhalTexture*> (texture)->handle
                     : m_white;
    }

    Texture
    NhalRenderer::texture_or_white (const render::TexturePtr& texture) const {
      return texture_or_white (texture.get ());
    }
  }
}
