// The scene's generic drawing: the sky, the game's draw lists and meshes
// through the uber pipelines, and the lettering in the world. Each
// feature's own drawing is in its nhal_renderer_*.cc.

#include <moppe/nhal/renderer/nhal_renderer_impl.hh>

namespace moppe::nhal {
  namespace detail {
    void NhalRenderer::make_scene_pipelines () {
      const RenderPipelineDesc scene = scene_target ();
      RenderPipelineDesc sky = scene;
      sky.program = &shaders::sky::program;
      sky.vertex = m_shaders.sky[0];
      sky.fragment = m_shaders.sky[1];
      sky.cull = Cull::none;
      sky.depth_write = false;
      m_sky = m_device->create_render_pipeline (sky);

      // Lettering over the surface it lies on: premultiplied by its
      // coverage, tested against the surface's depth but not writing it.
      RenderPipelineDesc lettering = scene;
      lettering.program = &shaders::world_text::program;
      lettering.vertex = m_shaders.world_text[0];
      lettering.fragment = m_shaders.world_text[1];
      lettering.blend[0] = Blend::premultiplied;
      lettering.depth_write = false;
      lettering.cull = Cull::none;
      lettering.label = "world text";
      m_world_text = m_device->create_render_pipeline (lettering);
      // A label standing in front writes its depth: the temporal resolve
      // follows the motion of the nearest surface about each texel, and a
      // label's thin strokes would otherwise take the ground's or a bus's
      // behind them, and lose their letters while the camera turns.
      RenderPipelineDesc labels = lettering;
      labels.depth_write = true;
      labels.label = "world labels";
      m_world_labels = m_device->create_render_pipeline (labels);
      // What the scene hides of a label: where its surfaces are nearer.
      RenderPipelineDesc behind = lettering;
      behind.depth_compare = CompareOp::less;
      behind.label = "world labels behind";
      m_world_labels_behind = m_device->create_render_pipeline (behind);
    }

    void NhalRenderer::draw_sky (const render::SkyParams&) {
      open_scene ();
      m_device->set_pipeline (m_sky);
      m_device->set_buffer (0, m_frame_block);
      m_device->draw (3);
    }

    void NhalRenderer::draw_list (const DrawList& list,
                                  std::uint64_t motion_id) {
      if (list.empty () || m_sun_shadow.casting)
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
      if (placed.runs.empty () || m_sun_shadow.casting || m_scene_closed)
        return;
      open_scene ();
      m_device->set_pipeline (m_world_text);
      m_device->set_buffer (0, m_frame_block);
      m_device->set_texture (8, m_sun_shadow.map);
      for (const NhalWorldText::Run& run : placed.runs) {
        const auto& glyphs = static_cast<const NhalGlyphSet&> (*run.glyphs);
        m_device->set_buffer (1, run.quads);
        m_device->set_buffer (2, glyphs.curves);
        m_device->set_buffer (3, glyphs.bands);
        m_device->draw (6, run.count);
      }
    }

    void NhalRenderer::draw_world_labels (const render::TextList& text,
                                          float hidden) {
      if (text.empty () || m_sun_shadow.casting || m_scene_closed)
        return;
      open_scene ();
      // Behind the scene's surfaces faintly, then in front of them.
      std::vector<render::GlyphQuad> faint = text.quads ();
      for (render::GlyphQuad& quad : faint)
        quad.color[3] *= hidden;
      for (const bool behind : { true, false }) {
        const std::vector<render::GlyphQuad>& quads =
          behind ? faint : text.quads ();
        m_device->set_pipeline (behind ? m_world_labels_behind
                                       : m_world_labels);
        m_device->set_buffer (0, m_frame_block);
        m_device->set_texture (8, m_sun_shadow.map);
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
      if (m_sun_shadow.casting) {
        m_sun_shadow.casters.push_back ({ &m, model });
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

    Pipeline NhalRenderer::uber (const DrawState& state) {
      const int key = (state.blend ? 1 : 0) | (state.additive ? 2 : 0) |
                      (state.depth_test ? 4 : 0) | (state.depth_write ? 8 : 0) |
                      (state.cull ? 16 : 0);
      Pipeline& pipeline = m_uber[key];
      if (pipeline)
        return pipeline;
      RenderPipelineDesc desc;
      desc.program = &shaders::uber::program;
      desc.vertex = m_shaders.uber[0];
      desc.fragment = m_shaders.uber[1];
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
        m_device->set_texture (8, m_sun_shadow.map);
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
