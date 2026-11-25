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
      m_sun_shadow.mesh = m_device->create_render_pipeline (mesh_caster);
      RenderPipelineDesc clear = caster;
      clear.program = &shaders::shadow_clear::program;
      clear.vertex = m_shaders.shadow_clear[0];
      clear.depth_compare = CompareOp::always;
      clear.depth_bias = clear.slope_scaled_depth_bias = 0.0f;
      clear.cull = Cull::none;
      clear.label = "clear near shadow";
      m_sun_shadow.clear = m_device->create_render_pipeline (clear);
      // The sun's two cascades side by side: near on the left, far on
      // the right.
      m_sun_shadow.map =
        m_device->create_texture ({ 2 * shadow_size,
                                    shadow_size,
                                    Format::d32_float,
                                    usage_depth | usage_sampled,
                                    1,
                                    "sun shadow" });
    }

    void NhalRenderer::begin_shadow_casters () {
      m_sun_shadow.casting = true;
      m_sun_shadow.casters.clear ();
    }

    void NhalRenderer::end_shadow_casters () {
      m_sun_shadow.casting = false;
    }

    void NhalRenderer::render_local_shadow (
      const render::LocalShadowParams& params) {
      if (!m_terrain.have || m_scene_open || m_scene_closed) {
        m_sun_shadow.casters.clear ();
        return;
      }
      using Shadow = render::LocalShadowParams;
      const bool trees = m_forest.count;
      const bool rocks = m_boulders.count;
      const auto& far = params.cascades[Shadow::far];
      auto& cached = m_sun_shadow.cached_far;
      // The far map contains only terrain and static tree proxies. Small
      // movements keep sampling the map with the matrix that drew it.
      constexpr meters_t focus_limit = 32.0f * u::m;
      const bool refresh =
        !cached || m_frame_reset || m_frame - m_sun_shadow.far_frame >= 4 ||
        cached->radius != far.radius ||
        dot (far.focus - cached->focus, far.focus - cached->focus) >
          focus_limit * focus_limit ||
        length2 (direction_value (m_params.sun_dir) -
                 direction_value (m_sun_shadow.cached_sun)) > 1e-6f;
      const std::size_t cascades = refresh ? 2 : 1;
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
      pass.depth = {
        m_sun_shadow.map, refresh ? Load::clear : Load::load, Store::store, 1.0f
      };
      m_device->begin_render_pass (pass);
      for (std::size_t c = 0; c < cascades; ++c) {
        m_device->set_viewport (
          float (c * shadow_size), 0, float (shadow_size), float (shadow_size));
        if (!refresh) {
          m_device->set_pipeline (m_sun_shadow.clear);
          m_device->draw (3);
        }
        draw_shadow_cascade (
          params.cascades[c], c, trees, rocks && c == Shadow::near);
      }
      m_device->end_render_pass ();
      m_sun_shadow.casters.clear ();

      if (refresh) {
        cached = far;
        m_sun_shadow.cached_sun = m_params.sun_dir;
        m_sun_shadow.far_frame = m_frame;
      }

      // Normalised light depth per metre, for biases given in metres:
      // a cascade's box runs from a quarter to eight times its reach.
      const auto per_metre = [] (const render::ShadowCascade& cascade) {
        return 1.0f / (7.75f * cascade.radius.numerical_value_in (u::m));
      };
      m_frame_values.sun_view = matrix (near.light_view_proj);
      m_frame_values.sun_view_far = matrix (cached->light_view_proj);
      m_frame_values.shadow = { m_terrain.params.shadow_strength,
                                1.0f / shadow_size,
                                per_metre (near),
                                per_metre (*cached) };
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
      caster.focus = lanes (focus, seconds_value (m_params.time));
      const Transient caster_block = m_device->allocate (sizeof caster);
      std::memcpy (caster_block.data, &caster, sizeof caster);

      m_device->set_pipeline (m_sun_shadow.terrain);
      m_device->set_buffer (0, caster_block);
      m_device->set_uniforms (1, terrain_block ());
      m_device->set_texture (0, m_terrain.heights);
      const auto& t = m_terrain.params;
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
          m_device->draw_indexed (m_terrain.chunk_indices[lod],
                                  IndexType::uint32,
                                  m_terrain.chunk_index_count[lod]);
        }

      if (trees) {
        const bool far = index == render::LocalShadowParams::far;
        m_device->set_pipeline (far ? m_forest.far_shadow : m_forest.shadow);
        m_device->set_buffer (0, caster_block);
        m_device->set_buffer (1, m_forest.shadow_block[index]);
        m_device->set_buffer (3, m_forest.records);
        m_device->set_buffer (4, m_forest.shadow_candidates[index]);
        for (std::uint32_t species = 0; species < forest_species; ++species) {
          shaders::forest_shadow::Draw draw {};
          draw.class_ = { float (species), 0, 0, 0 };
          m_device->set_uniforms (2, draw);
          m_device->draw_indexed_indirect (
            far ? m_forest.far_indices : m_forest.indices,
            IndexType::uint16,
            m_forest.shadow_arguments[index],
            species * sizeof (DrawIndexedIndirectArgs));
        }
      }
      if (index == render::LocalShadowParams::near &&
          !m_sun_shadow.casters.empty ())
        draw_mesh_casters (caster_block, light);
      if (rocks) {
        m_device->set_pipeline (m_boulders.shadow);
        m_device->set_buffer (0, caster_block);
        m_device->set_buffer (1, m_boulders.shadow_block);
        m_device->set_buffer (3, m_boulders.records);
        m_device->set_buffer (4, m_boulders.shadow_candidates);
        m_device->set_buffer (5, m_boulders.corners);
        shaders::boulders_shadow::Draw draw {};
        m_device->set_uniforms (2, draw);
        m_device->draw_indexed_indirect (
          m_boulders.indices, IndexType::uint16, m_boulders.shadow_arguments);
      }
    }

    void NhalRenderer::draw_mesh_casters (const Transient& caster_block,
                                          const Mat4& light) {
      m_device->set_pipeline (m_sun_shadow.mesh);
      for (const SunShadowState::Caster& c : m_sun_shadow.casters) {
        if (!c.mesh->bounds.visible (light * c.model))
          continue;
        shaders::mesh_shadow::Draw draw {};
        draw.model = matrix (c.model);
        draw.previous_model = draw.model;
        const Transient block = m_device->allocate (sizeof draw);
        std::memcpy (block.data, &draw, sizeof draw);
        for (const DrawList::Run& run : c.mesh->runs) {
          if (!run.count || run.state.blend || run.state.additive)
            continue;
          m_device->set_buffer (0, caster_block);
          m_device->set_buffer (1, c.mesh->vertices);
          m_device->set_buffer (2, block);
          m_device->draw_indexed (
            c.mesh->indices, c.mesh->index_type, run.count, 1, run.first);
        }
      }
    }
  }
}
