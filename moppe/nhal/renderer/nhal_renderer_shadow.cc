// The sun's shadow: two cascades in one atlas, their casters (terrain,
// trees, boulders, and the meshes lent to them), and local shadows.

#include <moppe/nhal/renderer/nhal_renderer_impl.hh>

namespace moppe::nhal {
  namespace detail {
    void NhalRenderer::make_shadow_pipelines () {
      const RenderPipelineDesc caster = shadow_caster ();
      // Baked meshes are not all closed, so both their faces cast.
      RenderPipelineDesc mesh_caster = caster;
      mesh_caster.program = &shaders::mesh_shadow::program;
      mesh_caster.vertex = m_shaders.mesh_shadow[0];
      mesh_caster.cull = Cull::none;
      mesh_caster.label = "mesh shadow";
      m_mesh_shadow = m_device->create_render_pipeline (mesh_caster);
      // The sun's two cascades side by side: near on the left, far on
      // the right.
      m_shadow_map = m_device->create_texture ({ 2 * shadow_size,
                                                 shadow_size,
                                                 Format::d32_float,
                                                 usage_depth | usage_sampled,
                                                 1,
                                                 "sun shadow" });
    }

    void NhalRenderer::begin_shadow_casters () {
      m_casting = true;
      m_casters.clear ();
    }

    void NhalRenderer::end_shadow_casters () {
      m_casting = false;
    }

    void NhalRenderer::render_local_shadow (
      const render::LocalShadowParams& params) {
      if (!m_have_terrain || m_scene_open || m_scene_closed) {
        m_casters.clear ();
        return;
      }
      using Shadow = render::LocalShadowParams;
      const bool trees = m_tree_count;
      const bool rocks = m_rock_count;
      constexpr std::size_t cascades = 2;
      for (std::size_t c = 0; c < cascades; ++c) {
        const render::ShadowCascade& cascade = params.cascades[c];
        if (trees)
          cull_trees (cascade.light_view_proj,
                      position_value (cascade.focus),
                      1.0f,
                      true,
                      c);
      }
      const render::ShadowCascade& near = params.cascades[Shadow::near];
      if (rocks)
        cull_rocks (
          near.light_view_proj, position_value (near.focus), 1.0f, true);

      RenderPassDesc pass;
      pass.label = "sun shadow";
      pass.depth = { m_shadow_map, Load::clear, Store::store, 1.0f };
      m_device->begin_render_pass (pass);
      for (std::size_t c = 0; c < cascades; ++c) {
        m_device->set_viewport (
          float (c * shadow_size), 0, float (shadow_size), float (shadow_size));
        draw_shadow_cascade (
          params.cascades[c], c, trees, rocks && c == Shadow::near);
      }
      m_device->end_render_pass ();
      m_casters.clear ();

      // Normalised light depth per metre, for biases given in metres:
      // a cascade's box runs from a quarter to eight times its reach.
      const auto per_metre = [] (const render::ShadowCascade& cascade) {
        return 1.0f / (7.75f * cascade.radius.numerical_value_in (u::m));
      };
      m_frame_values.sun_view = matrix (near.light_view_proj);
      m_frame_values.sun_view_far =
        matrix (params.cascades[Shadow::far].light_view_proj);
      m_frame_values.shadow = { m_terrain_params.shadow_strength,
                                1.0f / shadow_size,
                                per_metre (near),
                                per_metre (params.cascades[Shadow::far]) };
      upload_frame ();
    }

    void
    NhalRenderer::draw_shadow_cascade (const render::ShadowCascade& cascade,
                                       std::size_t index,
                                       bool trees,
                                       bool rocks) {
      const Vec3 focus = position_value (cascade.focus);
      const Mat4& light = cascade.light_view_proj;
      shaders::terrain_shadow::Caster caster {};
      caster.light_view = matrix (light);
      caster.focus = lanes (focus, m_params.time);
      const Transient caster_block = m_device->allocate (sizeof caster);
      std::memcpy (caster_block.data, &caster, sizeof caster);

      m_device->set_pipeline (m_terrain_shadow);
      m_device->set_buffer (0, caster_block);
      m_device->set_uniforms (1, terrain_block ());
      m_device->set_texture (0, m_heights);
      const auto& t = m_terrain_params;
      const int chunks_x = t.width / chunk_cells;
      const int chunks_z = t.height / chunk_cells;
      const float chunk_width = chunk_cells * t.scale[0];
      const float chunk_depth = chunk_cells * t.scale[2];
      const float reach = cascade.radius.numerical_value_in (u::m);
      const float draw_reach =
        reach + 0.5f * std::hypot (chunk_width, chunk_depth);
      const int centre_x = int (std::floor (focus[0] / chunk_width));
      const int centre_z = int (std::floor (focus[2] / chunk_depth));
      const int reach_x = int (std::ceil (reach / chunk_width)) + 1;
      const int reach_z = int (std::ceil (reach / chunk_depth)) + 1;
      auto floor_div = [] (int v, int d) {
        return v / d - (v % d < 0 ? 1 : 0);
      };
      const int lod = int (index == render::LocalShadowParams::near
                             ? render::TerrainLod::Native
                             : render::TerrainLod::Stride2);
      for (int z = centre_z - reach_z; z <= centre_z + reach_z; ++z)
        for (int x = centre_x - reach_x; x <= centre_x + reach_x; ++x) {
          const float dx = (x + 0.5f) * chunk_width - focus[0];
          const float dz = (z + 0.5f) * chunk_depth - focus[2];
          if (dx * dx + dz * dz > draw_reach * draw_reach)
            continue;
          const int wrap_x = floor_div (x, chunks_x);
          const int wrap_z = floor_div (z, chunks_z);
          shaders::terrain_shadow::Chunk chunk {};
          chunk.placement = { float ((x - wrap_x * chunks_x) * chunk_cells),
                              float ((z - wrap_z * chunks_z) * chunk_cells),
                              lod_step[lod],
                              float (lod_verts[lod]) };
          chunk.offset = { wrap_x * chunks_x * chunk_width,
                           0,
                           wrap_z * chunks_z * chunk_depth,
                           0 };
          m_device->set_uniforms (2, chunk);
          m_device->draw_indexed (
            m_chunk_indices[lod], IndexType::uint32, m_chunk_index_count[lod]);
        }

      if (trees) {
        const bool far = index == render::LocalShadowParams::far;
        m_device->set_pipeline (far ? m_forest_far_shadow : m_forest_shadow);
        m_device->set_buffer (0, caster_block);
        m_device->set_buffer (1, m_shadow_forest_block[index]);
        m_device->set_buffer (3, m_trees);
        m_device->set_buffer (4, m_shadow_candidates[index]);
        for (std::uint32_t species = 0; species < forest_species; ++species) {
          shaders::forest_shadow::Draw draw {};
          draw.class_ = { float (species * m_tree_count), 0, 0, 0 };
          m_device->set_uniforms (2, draw);
          m_device->draw_indexed_indirect (
            far ? m_far_tree_indices : m_tree_indices,
            IndexType::uint16,
            m_shadow_arguments[index],
            species * sizeof (DrawIndexedIndirectArgs));
        }
      }
      if (index == render::LocalShadowParams::near && !m_casters.empty ())
        draw_mesh_casters (caster_block);
      if (rocks) {
        m_device->set_pipeline (m_boulders_shadow);
        m_device->set_buffer (0, caster_block);
        m_device->set_buffer (1, m_rock_shadow_block);
        m_device->set_buffer (3, m_rocks);
        m_device->set_buffer (4, m_rock_shadow_candidates);
        m_device->set_buffer (5, m_rock_corners);
        shaders::boulders_shadow::Draw draw {};
        m_device->set_uniforms (2, draw);
        m_device->draw_indexed_indirect (
          m_rock_indices, IndexType::uint16, m_rock_shadow_arguments);
      }
    }

    void NhalRenderer::draw_mesh_casters (const Transient& caster_block) {
      m_device->set_pipeline (m_mesh_shadow);
      for (const Caster& c : m_casters) {
        shaders::mesh_shadow::Draw draw {};
        draw.model = matrix (c.model);
        draw.previous_model = draw.model;
        const Transient block = m_device->allocate (sizeof draw);
        std::memcpy (block.data, &draw, sizeof draw);
        for (const DrawList::Run& run : c.mesh->runs) {
          if (!run.count || run.state.blend || run.state.additive)
            continue;
          const std::uint32_t first = m_first_vertex_counts ? run.first : 0;
          const std::uint64_t offset =
            std::uint64_t (run.first - first) * sizeof (render::Vertex);
          m_device->set_buffer (0, caster_block);
          m_device->set_buffer (1, c.mesh->vertices, offset);
          m_device->set_buffer (2, block);
          m_device->draw (run.count, 1, first);
        }
      }
    }
  }
}
