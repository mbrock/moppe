// The boulders: their records, the icosahedra they are shaped from, and
// the cull that counts each into its class's indirect draw.

#include <moppe/nhal/renderer/nhal_renderer_impl.hh>

namespace moppe::nhal {
  namespace detail {
    namespace {
      // The icosahedron every boulder is shaped from, and its twenty faces as
      // corner triples (shader_types.h's MOPPE_BOULDER_FACE_CORNERS).
      constexpr float icosahedron[12][3] = {
        { -0.525731f, 0.850651f, 0.0f },  { 0.525731f, 0.850651f, 0.0f },
        { -0.525731f, -0.850651f, 0.0f }, { 0.525731f, -0.850651f, 0.0f },
        { 0.0f, -0.525731f, 0.850651f },  { 0.0f, 0.525731f, 0.850651f },
        { 0.0f, -0.525731f, -0.850651f }, { 0.0f, 0.525731f, -0.850651f },
        { 0.850651f, 0.0f, -0.525731f },  { 0.850651f, 0.0f, 0.525731f },
        { -0.850651f, 0.0f, -0.525731f }, { -0.850651f, 0.0f, 0.525731f },
      };
      constexpr std::uint32_t icosahedron_faces[60] = {
        0, 11, 5,  0, 5,  1, 0, 1, 7, 0, 7,  10, 0, 10, 11, 1, 5, 9, 5, 11,
        4, 11, 10, 2, 10, 7, 6, 7, 1, 8, 3,  9,  4, 3,  4,  2, 3, 2, 6, 3,
        6, 8,  3,  8, 9,  4, 9, 5, 2, 4, 11, 6,  2, 10, 8,  6, 7, 9, 8, 1,
      };
    }

    void NhalRenderer::make_boulders_pipelines () {
      const RenderPipelineDesc scene = scene_target ();
      const RenderPipelineDesc caster = shadow_caster ();
      RenderPipelineDesc rock_caster = caster;
      rock_caster.program = &shaders::boulders_shadow::program;
      rock_caster.vertex = m_shaders.boulders_shadow[0];
      rock_caster.label = "boulder shadow";
      m_boulders.shadow = m_device->create_render_pipeline (rock_caster);
      RenderPipelineDesc rock = scene;
      rock.program = &shaders::boulders::program;
      rock.vertex = m_shaders.boulders[0];
      rock.fragment = m_shaders.boulders[1];
      rock.cull = Cull::none;
      rock.label = "boulders";
      m_boulders.draw = m_device->create_render_pipeline (rock);
      m_boulders.cull =
        m_device->create_compute_pipeline ({ &shaders::boulder_cull::program,
                                             m_shaders.boulder_cull,
                                             "boulder culling" });
    }

    void NhalRenderer::set_boulders (
      const render::BoulderSetup& setup,
      std::span<const render::BoulderInstance> instances) {
      for (Buffer* b : { &m_boulders.records,
                         &m_boulders.candidates,
                         &m_boulders.arguments,
                         &m_boulders.shadow_candidates,
                         &m_boulders.shadow_arguments })
        if (*b)
          m_device->destroy (*b), *b = {};
      std::vector<std::array<float, 4>> rows;
      rows.reserve (instances.size () * 3);
      for (const render::BoulderInstance& rock : instances) {
        const float radius = rock.radius.numerical_value_in (u::m);
        if (!(radius > 0.0f))
          continue;
        const Vec3 centre = position_value (rock.centre);
        const Vec3 up = rock.ground_normal.numerical_value_in (mp_units::one);
        rows.push_back ({ centre[0], centre[1], centre[2], radius });
        rows.push_back ({ up[0],
                          up[1],
                          up[2],
                          rock.moisture.numerical_value_in (mp_units::one) });
        rows.push_back ({ std::bit_cast<float> (rock.seed), 0, 0, 0 });
      }
      m_boulders.count = std::uint32_t (rows.size () / 3);
      if (!m_boulders.count)
        return;
      m_boulders.records = m_device->create_buffer (
        { .size = rows.size () * 16, .label = "boulder records" },
        std::as_bytes (std::span (rows)));
      auto candidates = [&] (std::uint32_t classes, const char* label) {
        return m_device->create_buffer (
          { .size = std::uint64_t (classes) * m_boulders.count * 8,
            .usage = buffer_storage_write,
            .label = label });
      };
      auto arguments = [&] (const char* label) {
        return m_device->create_buffer (
          { .size = 2 * sizeof (DrawIndexedIndirectArgs),
            .usage = buffer_storage_write | buffer_indirect,
            .label = label });
      };
      m_boulders.candidates = candidates (2, "boulder candidates");
      m_boulders.arguments = arguments ("boulder draws");
      m_boulders.shadow_candidates =
        candidates (1, "shadow boulder candidates");
      m_boulders.shadow_arguments = arguments ("shadow boulder draws");
      const Vec3 period = extent_value (setup.period);
      m_boulders.period[0] = period[0];
      m_boulders.period[1] = period[2];

      if (!m_boulders.indices) {
        std::vector<std::array<float, 4>> corners;
        for (const auto& c : icosahedron)
          corners.push_back ({ c[0], c[1], c[2], 0 });
        m_boulders.corners = m_device->create_buffer (
          { .size = corners.size () * 16, .label = "icosahedron" },
          std::as_bytes (std::span (corners)));
        m_boulders.faces = m_device->create_buffer (
          { .size = sizeof icosahedron_faces, .label = "icosahedron faces" },
          std::as_bytes (std::span (icosahedron_faces)));
        // Coarse: the faces themselves. Fine: four triangles a face, over
        // its corners (slots 0-2) and edge midpoints (slots 3-5).
        std::vector<std::uint16_t> indices (std::begin (icosahedron_faces),
                                            std::end (icosahedron_faces));
        constexpr std::uint16_t quarters[12] = { 0, 3, 5, 3, 1, 4,
                                                 5, 4, 2, 3, 4, 5 };
        for (std::uint16_t face = 0; face < 20; ++face)
          for (std::uint16_t slot : quarters)
            indices.push_back (std::uint16_t (face * 6 + slot));
        m_boulders.indices = m_device->create_buffer (
          { .size = indices.size () * 2, .label = "boulder topology" },
          std::as_bytes (std::span (indices)));
      }
    }

    void NhalRenderer::cull_rocks (const Mat4& view,
                                   const Vec3& eye,
                                   float height,
                                   bool shadow) {
      if (!m_boulders.count)
        return;
      auto row = [&] (int r) {
        return std::sqrt (view.element (r) * view.element (r) +
                          view.element (4 + r) * view.element (4 + r) +
                          view.element (8 + r) * view.element (8 + r));
      };
      shaders::boulder_cull::Forest block {};
      block.view = matrix (view);
      block.eye = lanes (eye);
      block.world = { m_boulders.period[0],
                      m_boulders.period[1],
                      float (m_boulders.count),
                      float (m_boulders.count) };
      block.cull = { row (0), row (1), row (3), height };
      block.mode = { shadow ? 1.0f : 0.0f, 0, 0, 0 };
      Transient& uniforms = shadow ? m_boulders.shadow_block : m_boulders.block;
      uniforms = m_device->allocate (sizeof block);
      std::memcpy (uniforms.data, &block, sizeof block);
      const DrawIndexedIndirectArgs reset[2] = { { 60, 0, 0, 0, 0 },
                                                 { 240, 0, 60, 0, 0 } };
      const Buffer arguments =
        shadow ? m_boulders.shadow_arguments : m_boulders.arguments;
      m_device->copy_to_buffer (
        arguments,
        0,
        m_device->upload (std::span<const DrawIndexedIndirectArgs> (reset)));
      m_device->begin_compute_pass (shadow ? "shadow boulder culling"
                                           : "boulder culling");
      m_device->set_pipeline (m_boulders.cull);
      m_device->set_buffer (1, uniforms);
      m_device->set_buffer (2, m_boulders.records);
      m_device->set_buffer (
        3, shadow ? m_boulders.shadow_candidates : m_boulders.candidates);
      m_device->set_buffer (4, arguments);
      m_device->dispatch ((m_boulders.count + 63) / 64);
      m_device->end_compute_pass ();
    }

    void NhalRenderer::draw_boulders () {
      if (!m_boulders.count)
        return;
      open_scene ();
      m_device->set_pipeline (m_boulders.draw);
      m_device->set_buffer (0, m_frame_block);
      m_device->set_buffer (1, m_boulders.block);
      m_device->set_buffer (3, m_boulders.records);
      m_device->set_buffer (4, m_boulders.candidates);
      m_device->set_buffer (5, m_boulders.corners);
      m_device->set_buffer (6, m_boulders.faces);
      m_device->set_texture (8, m_sun_shadow.map);
      for (std::uint32_t fine = 0; fine < 2; ++fine) {
        shaders::boulders::Draw draw {};
        draw.class_ = { float (fine * m_boulders.count), float (fine), 0, 0 };
        m_device->set_uniforms (2, draw);
        m_device->draw_indexed_indirect (m_boulders.indices,
                                         IndexType::uint16,
                                         m_boulders.arguments,
                                         fine *
                                           sizeof (DrawIndexedIndirectArgs));
      }
    }
  }
}
