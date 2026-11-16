// The frame around the scene: beginning it, the shadow cascades and local
// shadows, the scene's and the drawable's targets, the temporal resolve and
// the post effects (ambient occlusion, light shafts, underwater, motion
// blur, bloom), the tonemap and the HUD, and the end of the frame with its
// screenshots, captured frames, and timings.

#include <moppe/nhal/renderer/nhal_renderer_impl.hh>

namespace moppe::nhal {
  namespace detail {
    bool NhalRenderer::begin_frame (const FrameParams& params) {
      if (!m_device->begin_frame ())
        return false;
      hold_frame_rate (params);
      const render::Viewport view = viewport_pixels ();
      const float scale = scene_scale (params);
      const std::uint32_t scene_width =
        std::max (1u, std::uint32_t (std::lround (view.width * scale)));
      const std::uint32_t scene_height =
        std::max (1u, std::uint32_t (std::lround (view.height * scale)));
      if (m_width != view.width || m_height != view.height)
        make_drawable_targets ();
      if (m_scene_width != scene_width || m_scene_height != scene_height)
        make_scene_targets (scene_width, scene_height);
      m_params = params;
      m_chunks.clear ();
      m_scene_open = m_scene_begun = m_scene_closed = m_resolved = m_presented =
        false;
      // MOPPE_NHAL_PROBE switches a diagnostic for consoles where the
      // renderer cannot be debugged directly: "white" draws every draw
      // list untextured, "additive" draws alpha-blended runs additively,
      // "nohistory" resolves every frame from itself alone, "nomotion"
      // resolves ignoring motion, "scene" presents the unresolved scene,
      // "noao" and "noshafts" leave out the occlusion and the sun shafts, and
      // "nosky", "nosward", and "noblades" the sky and the grass's
      // canopy and blades, to time each apart; "noterrain" and
      // "noribbons" separate the ground from its fine road geometry.
      {
        const char* probe = moppe::environment ("MOPPE_NHAL_PROBE");
        m_probe = probe ? probe : "";
      }
      if (m_probe == "nohistory")
        m_restart = true;

      const Mat4 view_proj = params.proj * params.view;
      if (m_restart)
        m_previous_view_proj = view_proj, m_previous_time = params.time;
      const float tan_x = 1.0f / params.proj.at (0, 0);
      const float tan_y = 1.0f / params.proj.at (1, 1);
      const std::uint32_t phase = m_frame++ % 16 + 1;

      FrameBlock frame {};
      frame.view_proj = matrix (view_proj);
      frame.previous_view_proj = matrix (m_previous_view_proj);
      frame.camera_position = lanes (params.camera_pos, params.time);
      frame.sun_direction = lanes (params.sun_dir, params.cloud_cover);
      frame.sun_diffuse = linear (params.sun_diffuse);
      frame.sun_specular = linear (params.sun_specular);
      frame.ambient = linear (params.ambient);
      frame.fog_color = linear (params.clear_color, params.fog_scale);
      frame.relief = { m_terrain_params.sea_level,
                       m_terrain_params.land_relief,
                       m_previous_time,
                       std::clamp (params.mist, 0.0f, 1.0f) };
      frame.view_right = lanes (params.cam_right * tan_x);
      frame.view_up = lanes (params.cam_up * tan_y);
      frame.view_forward = lanes (params.cam_forward);
      frame.temporal = { float (m_scene_width),
                         float (m_scene_height),
                         (halton (phase, 2) - 0.5f) * 2 / float (m_scene_width),
                         (halton (phase, 3) - 0.5f) * 2 /
                           float (m_scene_height) };
      frame.temporal_blend = { m_restart ? 1.0f : 0.1f,
                               m_probe == "nomotion" ? 1.0f : 0.0f,
                               float (m_width),
                               float (m_height) };
      frame.shadow = { 0, 1.0f / shadow_size, 0, 0 };
      const Vec3 sky_sun = length2 (params.sky_sun_dir) > 1e-6f
                             ? params.sky_sun_dir
                             : params.sun_dir;
      frame.sky_sun = lanes (sky_sun, params.sky_turn);
      frame.sky_moon = lanes (params.sky_moon_dir, params.moonlight);
      frame.sky_pole = lanes (params.sky_pole);
      frame.lamp = lanes (params.lamp_pos, params.lamp_reach);
      frame.lamp_color = linear (params.lamp_color);
      m_frame_values = frame;
      upload_frame ();
      cull_trees (view_proj, params.camera_pos, float (m_scene_height), false);
      cull_grass (view_proj, params);
      cull_rocks (view_proj, params.camera_pos, float (m_scene_height), false);

      m_previous_view_proj = view_proj;
      m_previous_time = params.time;
      m_restart = false;
      return true;
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
      const bool trees = params.include_forest && m_tree_count;
      const bool rocks = params.include_boulders && m_rock_count;
      const std::size_t cascades = params.include_far ? 2 : 1;
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
                                params.include_far
                                  ? per_metre (params.cascades[Shadow::far])
                                  : 0.0f };
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

    void NhalRenderer::close_scene () {
      if (m_scene_closed)
        return;
      open_scene ();
      if (m_params.rain > 0.0f) {
        // The rain, last in the scene so it falls in front of
        // everything it does not pass behind: 1 is a drizzle, and
        // heavier rain is more drops of it, up to a downpour at 2.5.
        const std::uint32_t drops =
          std::uint32_t (7000.0f * std::clamp (m_params.rain, 0.0f, 2.5f));
        m_device->set_pipeline (m_rain);
        m_device->set_buffer (0, m_frame_block);
        shaders::rain::Weather weather {};
        weather.rain = {
          std::clamp (m_params.rain, 0.0f, 1.0f), float (drops), 0, 0
        };
        m_device->set_uniforms (1, weather);
        m_device->draw (6, drops);
      }
      m_device->end_render_pass ();
      m_scene_open = false;
      m_scene_closed = true;
    }

    void NhalRenderer::reconstruct_scene () {
      if (m_resolved)
        return;
      close_scene ();
      // What every output pixel in a scene texel shares, gathered once
      // for the texel: its neighbourhood's bounds and the motion to
      // follow.
      RenderPassDesc gather;
      gather.label = "temporal neighbourhood";
      gather.color_count = 2;
      gather.colors[0] = { m_bounds[0], Load::discard, Store::store };
      gather.colors[1] = { m_bounds[1], Load::discard, Store::store };
      m_device->begin_render_pass (gather);
      m_device->set_pipeline (m_neighbourhood);
      m_device->set_buffer (0, m_frame_block);
      m_device->set_texture (0, m_scene_color);
      m_device->set_texture (1, m_scene_motion);
      m_device->set_texture (2, m_scene_depth);
      m_device->draw (3);
      m_device->end_render_pass ();
      Texture& previous = m_history[m_frame % 2];
      Texture& next = m_history[(m_frame + 1) % 2];
      RenderPassDesc pass;
      pass.label = "temporal resolve";
      pass.color_count = 1;
      pass.colors[0] = { next, Load::discard, Store::store };
      m_device->begin_render_pass (pass);
      m_device->set_pipeline (m_resolve);
      m_device->set_buffer (0, m_frame_block);
      m_device->set_texture (0, m_scene_color);
      m_device->set_texture (1, m_bounds[0]);
      m_device->set_texture (2, m_bounds[1]);
      m_device->set_texture (3, previous);
      m_device->draw (3);
      m_device->end_render_pass ();
      m_image = next;
      m_resolved = true;

      if (m_params.auto_exposure) {
        m_device->begin_compute_pass ("exposure");
        m_device->set_pipeline (m_exposure);
        m_device->set_texture (0, m_image);
        m_device->set_buffer (1, m_exposure_value);
        m_device->dispatch (1);
        m_device->end_compute_pass ();
      }
    }

    void NhalRenderer::apply_gtao (const render::GtaoParams& params) {
      if (m_resolved || m_probe == "noao")
        return;
      close_scene ();
      shaders::gtao::Post post = post_block (
        params.camera_pos, params.forward, params.right_span, params.up_span);
      post.occlusion = { params.radius.numerical_value_in (u::m),
                         params.strength.numerical_value_in (mp_units::one),
                         params.near_plane.numerical_value_in (u::m),
                         params.far_plane.numerical_value_in (u::m) };
      m_post_block = post;
      auto pass = [&] (Texture target, const char* label) {
        RenderPassDesc desc;
        desc.label = label;
        desc.color_count = 1;
        desc.colors[0] = { target,
                           target == m_scene_color ? Load::load : Load::discard,
                           Store::store };
        m_device->begin_render_pass (desc);
      };
      pass (m_ao[0], "occlusion");
      m_device->set_pipeline (m_gtao);
      m_device->set_uniforms (0, post);
      m_device->set_texture (0, m_scene_depth);
      m_device->draw (3);
      m_device->end_render_pass ();
      const std::array<float, 2> steps[2] = {
        { 1.0f / float (m_scene_width), 0.0f },
        { 0.0f, 1.0f / float (m_scene_height) },
      };
      // Across into the other target, then down into the scene.
      for (int i = 0; i < 2; ++i) {
        pass (i ? m_scene_color : m_ao[1], "occlusion blur");
        m_device->set_pipeline (i ? m_gtao_apply : m_gtao_blur);
        shaders::gtao::Post blurred = post;
        blurred.blur = { steps[i][0], steps[i][1], float (i), 0 };
        m_device->set_uniforms (0, blurred);
        m_device->set_texture (0, m_ao[i]);
        m_device->set_texture (1, m_scene_depth);
        m_device->draw (3);
        m_device->end_render_pass ();
      }
    }

    void
    NhalRenderer::apply_light_shafts (const render::LightShaftParams& params) {
      if (m_frame_values.shadow[0] <= 0.0f || m_resolved ||
          m_probe == "noshafts")
        return;
      close_scene ();
      shaders::gtao::Post post = post_block (
        params.camera_pos, params.forward, params.right_span, params.up_span);
      post.shafts = {
        params.max_distance.numerical_value_in (u::m), 0.011f, 24.0f, 0
      };
      post.shaft_color = linear (
        params.sun_color, params.strength.numerical_value_in (mp_units::one));
      const float strength = post.shaft_color[3];
      for (int c = 0; c < 3; ++c)
        post.shaft_color[c] *= strength;
      RenderPassDesc desc;
      desc.label = "sun shafts";
      desc.color_count = 1;
      desc.colors[0] = { m_scene_color, Load::load, Store::store };
      m_device->begin_render_pass (desc);
      m_device->set_pipeline (m_shafts_pipeline);
      m_device->set_buffer (0, m_frame_block);
      m_device->set_uniforms (1, post);
      m_device->set_texture (0, m_scene_depth);
      m_device->set_texture (8, m_shadow_map);
      m_device->draw (3);
      m_device->end_render_pass ();
    }

    void
    NhalRenderer::apply_underwater (const render::UnderwaterParams& params) {
      if (m_resolved || !m_water_levels || !m_have_terrain)
        return;
      close_scene ();
      if (!m_submerged)
        m_submerged =
          m_device->create_texture ({ m_scene_width,
                                      m_scene_height,
                                      Format::rgba16_float,
                                      usage_render_target | usage_sampled,
                                      1,
                                      "submerged scene" });
      shaders::underwater::Submersion below {};
      below.surface = { params.water_level.numerical_value_in (u::m),
                        params.near_plane.numerical_value_in (u::m),
                        params.far_plane.numerical_value_in (u::m),
                        0 };
      RenderPassDesc pass;
      pass.label = "underwater";
      pass.color_count = 1;
      pass.colors[0] = { m_submerged, Load::discard, Store::store };
      m_device->begin_render_pass (pass);
      m_device->set_pipeline (m_underwater);
      m_device->set_buffer (0, m_frame_block);
      m_device->set_uniforms (1, terrain_block ());
      m_device->set_uniforms (2, below);
      m_device->set_uniforms (3, sea_block (m_params.time));
      m_device->set_texture (0, m_scene_color);
      m_device->set_texture (1, m_scene_depth);
      m_device->set_texture (2, m_heights);
      m_device->set_texture (11, m_water_levels);
      m_device->set_texture (12, m_water_flow ? m_water_flow : m_still_water);
      m_device->draw (3);
      m_device->end_render_pass ();
      std::swap (m_scene_color, m_submerged);
    }

    void NhalRenderer::apply_motion_blur (float strength) {
      if (strength <= 0.0f || m_probe == "scene")
        return;
      reconstruct_scene ();
      if (!m_blurred)
        m_blurred =
          m_device->create_texture ({ m_width,
                                      m_height,
                                      Format::rgba16_float,
                                      usage_render_target | usage_sampled,
                                      1,
                                      "motion blurred" });
      shaders::motion_blur::Smear smear {};
      // At most some three and a half hundredths of the frame's height,
      // in taps three output pixels apart.
      constexpr float most_taps = 12.0f;
      const float longest = 0.035f * float (m_height);
      smear.streak = { std::min (strength, 1.0f),
                       longest,
                       most_taps,
                       std::max (longest / most_taps, 1.0f) };
      RenderPassDesc desc;
      desc.label = "motion blur";
      desc.color_count = 1;
      desc.colors[0] = { m_blurred, Load::discard, Store::store };
      m_device->begin_render_pass (desc);
      m_device->set_pipeline (m_motion_blur);
      m_device->set_buffer (0, m_frame_block);
      m_device->set_uniforms (1, smear);
      m_device->set_texture (0, m_image);
      m_device->set_texture (1, m_scene_motion);
      m_device->set_texture (2, m_scene_depth);
      m_device->draw (3);
      m_device->end_render_pass ();
      m_image = m_blurred;
    }

    void NhalRenderer::draw_hud_text (const render::TextList& text) {
      m_hud_text = text;
    }

    void NhalRenderer::draw_hud (const DrawList& list) {
      open_present ();
      shaders::hud::Hud hud {};
      hud.projection = matrix (Mat4::hud_ortho (width_pts (), height_pts ()));
      hud.output = { m_device->surface_format () == Format::rgba16_float ? 1.0f
                                                                         : 0.0f,
                     m_scale,
                     0,
                     0 };
      if (!list.empty ()) {
        m_device->set_pipeline (m_hud);
        m_device->set_uniforms (0, hud);
        const Transient vertices =
          m_device->upload (std::span (list.vertices ()));
        for (const DrawList::Run& run : list.runs ()) {
          if (!run.count)
            continue;
          const std::uint32_t first = m_first_vertex_counts ? run.first : 0;
          m_device->set_buffer (1,
                                advanced (vertices,
                                          std::uint64_t (run.first - first) *
                                            sizeof (render::Vertex)));
          m_device->set_texture (0, texture_or_white (run.texture));
          m_device->draw (run.count, 1, first);
        }
      }
      if (!m_hud_text.empty ()) {
        m_device->set_pipeline (m_slug_text);
        m_device->set_uniforms (0, hud);
        const auto& quads = m_hud_text.quads ();
        for (const render::TextList::Run& run : m_hud_text.runs ()) {
          const auto* glyphs =
            static_cast<const NhalGlyphSet*> (run.glyphs.get ());
          if (!glyphs || !run.count)
            continue;
          // Each run's quads in a slice of their own: Direct3D's instance
          // index does not count from a draw's first instance.
          m_device->set_buffer (1,
                                m_device->upload (std::span (quads).subspan (
                                  run.first, run.count)));
          m_device->set_buffer (2, glyphs->curves);
          m_device->set_buffer (3, glyphs->bands);
          m_device->draw (6, run.count);
        }
        m_hud_text.clear ();
      }
    }

    void NhalRenderer::request_screenshot (const std::string& path) {
      m_screenshot = path;
    }

    void NhalRenderer::request_frame (
      std::function<void (const render::FramePixels&)> sink) {
      m_frame_sink = std::move (sink);
    }

    const std::array<std::uint8_t, 65536>& NhalRenderer::display_bytes () {
      static const std::unique_ptr<std::array<std::uint8_t, 65536>> table = [] {
        auto bytes = std::make_unique<std::array<std::uint8_t, 65536>> ();
        for (std::uint32_t h = 0; h < 65536; ++h) {
          const float v = std::clamp (
            half_to_float (static_cast<std::uint16_t> (h)), 0.0f, 1.0f);
          (*bytes)[h] = std::uint8_t (std::pow (v, 1 / 2.2f) * 255 + 0.5f);
        }
        return bytes;
      }();
      return *table;
    }

    std::vector<std::uint8_t>
    NhalRenderer::capture_rgb (const Capture& capture) {
      const Format format = capture.format;
      const std::size_t pixel_bytes = bytes_per_pixel (format);
      std::vector<std::uint8_t> rgb (std::size_t (capture.width) *
                                     capture.height * 3);
      const std::array<std::uint8_t, 65536>& shown = display_bytes ();
      const bool bgra = format == Format::bgra8_unorm;
      parallel_for (
        capture.height, 64, [&] (std::size_t begin, std::size_t end) {
          for (std::size_t y = begin; y < end; ++y) {
            const std::byte* p = capture.pixels.data () + y * capture.row_bytes;
            std::uint8_t* out = &rgb[y * capture.width * 3];
            for (std::uint32_t x = 0; x < capture.width;
                 ++x, p += pixel_bytes, out += 3) {
              if (format == Format::rgba16_float) {
                std::uint16_t h[3];
                std::memcpy (h, p, sizeof (h));
                out[0] = shown[h[0]];
                out[1] = shown[h[1]];
                out[2] = shown[h[2]];
              } else {
                out[0] = std::uint8_t (p[bgra ? 2 : 0]);
                out[1] = std::uint8_t (p[1]);
                out[2] = std::uint8_t (p[bgra ? 0 : 2]);
              }
            }
          }
        });
      return rgb;
    }

    void NhalRenderer::end_frame () {
      open_present ();
      m_device->end_render_pass ();
      if (!m_screenshot.empty () || m_frame_sink) {
        m_device->capture_frame (
          [path = std::move (m_screenshot),
           sink = std::move (m_frame_sink)] (const Capture& capture) {
            const std::vector<std::uint8_t> rgb = capture_rgb (capture);
            if (sink)
              sink ({ int (capture.width), int (capture.height), rgb });
            if (!path.empty ()) {
              write_png (path, capture.width, capture.height, rgb);
              std::cerr << "moppe: wrote " << path << std::endl;
            }
          });
        m_screenshot.clear ();
        m_frame_sink = nullptr;
        // Hosts may quit straight after the frame they asked to capture,
        // so the readback completes here rather than next frame.
        m_device->end_frame ();
        m_device->wait_idle ();
      } else {
        m_device->end_frame ();
      }
      m_previous_models.swap (m_current_models);
      m_current_models.clear ();
      m_previous_vertices.swap (m_current_vertices);
      for (auto& [id, vertices] : m_current_vertices)
        vertices.clear ();
      report_timings ();
    }

    void NhalRenderer::report_timings () {
      static const bool wanted = [] {
        const char* v = moppe::environment ("MOPPE_NHAL_TIMINGS");
        return v && *v && *v != '0';
      }();
      if (!wanted)
        return;
      for (const PassTiming& pass : m_device->pass_timings ())
        m_timing_sums[pass.label] += pass.milliseconds;
      if (++m_timing_frames < 120)
        return;
      double total = 0;
      std::cerr << "moppe: NHAL GPU ms:";
      for (const auto& [label, sum] : m_timing_sums) {
        std::cerr << ' ' << label << '=' << sum / m_timing_frames;
        total += sum / m_timing_frames;
      }
      std::cerr << " total=" << total << ", the scene at " << m_scene_width
                << 'x' << m_scene_height << std::endl;
      m_timing_sums.clear ();
      m_timing_frames = 0;
    }

    void NhalRenderer::reset_temporal_state () {
      m_restart = true;
    }

    void NhalRenderer::set_aspect_limit (float maximum) {
      m_aspect_limit = maximum;
    }

    render::Viewport NhalRenderer::viewport_pixels () const {
      return render::fit_viewport (m_device->surface_width (),
                                   m_device->surface_height (),
                                   m_aspect_limit);
    }

    int NhalRenderer::width_pts () const {
      return int (float (viewport_pixels ().width) / m_scale);
    }

    int NhalRenderer::height_pts () const {
      return int (float (viewport_pixels ().height) / m_scale);
    }

    float NhalRenderer::scale_factor () const {
      return m_scale;
    }

    shaders::gtao::Post NhalRenderer::post_block (const position_t& eye,
                                                  const Vec3& forward,
                                                  const Vec3& right,
                                                  const Vec3& up) const {
      shaders::gtao::Post post {};
      post.eye = lanes (position_value (eye));
      post.ray_forward = lanes (forward);
      post.ray_right = lanes (right);
      post.ray_up = lanes (up);
      post.occlusion = m_post_block.occlusion;
      return post;
    }

    void NhalRenderer::upload_frame () {
      m_frame_block = m_device->allocate (sizeof m_frame_values);
      std::memcpy (m_frame_block.data, &m_frame_values, sizeof m_frame_values);
    }

    float NhalRenderer::scene_scale (const FrameParams& params) const {
      const float point_relative = 1.0f / std::max (1.0f, m_scale);
      const render::Viewport view = viewport_pixels ();
      const double pixels = double (view.width) * view.height;
      const float affordable =
        params.scene_megapixel_budget > 0.0f && pixels > 0
          ? float (std::sqrt (params.scene_megapixel_budget * 1.0e6 / pixels))
          : 1.0f;
      float scale = params.scene_scale * std::min (point_relative, affordable);
      if (params.render_scale_override > 0.0f)
        scale = params.render_scale_override;
      // Bound the ordinary size, not the controller's result: a quarter
      // drawable floor otherwise defeats adaptation on large displays.
      return std::clamp (scale, 0.25f, 1.0f) * m_dynamic.share ();
    }

    void NhalRenderer::hold_frame_rate (const FrameParams& params) {
      if (params.frame_rate_target <= 0.0f) {
        m_dynamic = {};
        return;
      }
      double milliseconds = 0;
      for (const PassTiming& pass : m_device->pass_timings ())
        milliseconds += pass.milliseconds;
      const float refresh =
        float (m_device->next_frame_timing ().refresh_seconds);
      m_dynamic.observe (
        seconds (float (milliseconds) * 0.001f),
        seconds (std::max (1.0f / params.frame_rate_target, refresh)));
    }

    void NhalRenderer::make_drawable_targets () {
      for (Texture t :
           { m_history[0], m_history[1], m_blurred, m_bloom[0], m_bloom[1] })
        if (t)
          m_device->destroy (t);
      const render::Viewport view = viewport_pixels ();
      m_width = view.width;
      m_height = view.height;
      for (Texture& t : m_history)
        t = m_device->create_texture ({ m_width,
                                        m_height,
                                        Format::rgba16_float,
                                        usage_render_target | usage_sampled,
                                        1,
                                        "temporal history" });
      // Made when a frame first blurs.
      m_blurred = {};
      m_bloom_width = std::max (1u, m_width / 4);
      m_bloom_height = std::max (1u, m_height / 4);
      for (Texture& t : m_bloom)
        t = m_device->create_texture ({ m_bloom_width,
                                        m_bloom_height,
                                        Format::rgba16_float,
                                        usage_render_target | usage_sampled,
                                        1,
                                        "bloom" });
      m_restart = true;
    }

    void NhalRenderer::make_scene_targets (std::uint32_t scene_width,
                                           std::uint32_t scene_height) {
      for (Texture t : { m_scene_color,
                         m_scene_motion,
                         m_scene_depth,
                         m_beneath,
                         m_beneath_depth,
                         m_bounds[0],
                         m_bounds[1],
                         m_ao[0],
                         m_ao[1],
                         m_submerged })
        if (t)
          m_device->destroy (t);
      m_submerged = {};
      // The scene at its share of the drawable, upscaled temporally.
      m_scene_width = scene_width;
      m_scene_height = scene_height;
      m_scene_color =
        m_device->create_texture ({ m_scene_width,
                                    m_scene_height,
                                    Format::rgba16_float,
                                    usage_render_target | usage_sampled,
                                    1,
                                    "scene" });
      m_scene_motion =
        m_device->create_texture ({ m_scene_width,
                                    m_scene_height,
                                    Format::rg16_float,
                                    usage_render_target | usage_sampled,
                                    1,
                                    "scene motion" });
      m_scene_depth = m_device->create_texture ({ m_scene_width,
                                                  m_scene_height,
                                                  Format::d32_float,
                                                  usage_depth | usage_sampled,
                                                  1,
                                                  "scene depth" });
      m_beneath =
        m_device->create_texture ({ m_scene_width,
                                    m_scene_height,
                                    Format::rgba16_float,
                                    usage_render_target | usage_sampled,
                                    1,
                                    "scene beneath water" });
      m_beneath_depth =
        m_device->create_texture ({ m_scene_width,
                                    m_scene_height,
                                    Format::r32_float,
                                    usage_render_target | usage_sampled,
                                    1,
                                    "scene depth beneath water" });
      for (Texture& t : m_bounds)
        t = m_device->create_texture ({ m_scene_width,
                                        m_scene_height,
                                        Format::rgba16_float,
                                        usage_render_target | usage_sampled,
                                        1,
                                        "temporal neighbourhood" });
      for (Texture& t : m_ao)
        t = m_device->create_texture ({ m_scene_width,
                                        m_scene_height,
                                        Format::r16_float,
                                        usage_render_target | usage_sampled,
                                        1,
                                        "occlusion" });
    }

    void NhalRenderer::open_scene () {
      if (m_scene_open || m_scene_closed)
        return;
      RenderPassDesc pass;
      pass.label = m_scene_begun ? "scene, resumed" : "scene";
      pass.color_count = 2;
      const Load load = m_scene_begun ? Load::load : Load::clear;
      const auto& fog = m_params.clear_color;
      auto lin = [] (float x) { return std::pow (std::max (x, 0.0f), 2.2f); };
      pass.colors[0] = {
        m_scene_color,
        load,
        Store::store,
        { lin (fog.red), lin (fog.green), lin (fog.blue), 1 }
      };
      pass.colors[1] = { m_scene_motion, load, Store::store, { 0, 0, 0, 0 } };
      pass.depth = { m_scene_depth, load, Store::store, 0.0f };
      m_device->begin_render_pass (pass);
      m_scene_open = m_scene_begun = true;
    }

    void NhalRenderer::copy_beneath_water () {
      open_scene ();
      m_device->end_render_pass ();
      m_scene_open = false;
      RenderPassDesc pass;
      pass.label = "scene beneath water";
      pass.color_count = 2;
      pass.colors[0] = { m_beneath, Load::discard, Store::store };
      pass.colors[1] = { m_beneath_depth, Load::discard, Store::store };
      m_device->begin_render_pass (pass);
      m_device->set_pipeline (m_scene_copy);
      m_device->set_texture (0, m_scene_color);
      m_device->set_texture (1, m_scene_depth);
      m_device->draw (3);
      m_device->end_render_pass ();
      open_scene ();
    }

    std::array<float, 4> NhalRenderer::sun_glare () const {
      const Mat4 view_proj = m_params.proj * m_params.view;
      const Vec3 sun = m_params.sun_dir;
      auto row = [&] (int r) {
        return view_proj.element (r) * sun[0] +
               view_proj.element (4 + r) * sun[1] +
               view_proj.element (8 + r) * sun[2];
      };
      const float w = row (3);
      if (w <= 0.0f || m_params.sun_visibility <= 0.0f)
        return { 0, 0, 0, 1 };
      const float nx = row (0) / w, ny = row (1) / w;
      const float edge =
        1.0f -
        std::max (0.0f,
                  (std::max (std::abs (nx), std::abs (ny)) - 0.85f) / 0.45f);
      if (edge <= 0.0f)
        return { 0, 0, 0, 1 };
      return { nx * 0.5f + 0.5f,
               1.0f - (ny * 0.5f + 0.5f),
               m_params.sun_visibility * std::min (edge, 1.0f),
               float (m_width) / float (std::max (m_height, 1u)) };
    }

    void NhalRenderer::bloom (const shaders::present::Present& present) {
      auto pass = [&] (Texture target, const char* label) {
        RenderPassDesc desc;
        desc.label = label;
        desc.color_count = 1;
        desc.colors[0] = { target, Load::discard, Store::store };
        m_device->begin_render_pass (desc);
      };
      pass (m_bloom[0], "bloom bright pass");
      m_device->set_pipeline (m_bloom_bright);
      m_device->set_uniforms (0, present);
      m_device->set_buffer (1, m_exposure_value);
      m_device->set_texture (0, m_image);
      m_device->draw (3);
      m_device->end_render_pass ();
      const std::array<float, 2> steps[2] = {
        { 1.0f / float (m_bloom_width), 0.0f },
        { 0.0f, 1.0f / float (m_bloom_height) },
      };
      for (int i = 0; i < 2; ++i) {
        pass (m_bloom[1 - i], "bloom blur");
        m_device->set_pipeline (m_bloom_blur);
        shaders::bloom_blur::Blur blur {};
        blur.direction = { steps[i][0], steps[i][1], 0, 0 };
        m_device->set_uniforms (0, blur);
        m_device->set_texture (0, m_bloom[i]);
        m_device->draw (3);
        m_device->end_render_pass ();
      }
    }

    void NhalRenderer::open_present () {
      if (m_presented)
        return;
      reconstruct_scene ();
      shaders::present::Present present {};
      present.grade = { std::max (m_params.exposure_bias, 0.01f),
                        m_device->surface_format () == Format::rgba16_float
                          ? 0.0f
                          : 1.0f,
                        std::fmod (m_params.time, 1000.0f),
                        m_params.bloom ? 1.0f : 0.0f };
      present.sun_glare = sun_glare ();
      if (m_params.bloom)
        bloom (present);
      RenderPassDesc pass;
      pass.label = "present";
      pass.color_count = 1;
      pass.colors[0] = {
        m_device->backbuffer (), Load::clear, Store::store, { 0, 0, 0, 1 }
      };
      m_device->begin_render_pass (pass);
      const render::Viewport view = viewport_pixels ();
      m_device->set_viewport (float (view.x),
                              float (view.y),
                              float (view.width),
                              float (view.height));
      m_device->set_pipeline (m_present);
      m_device->set_uniforms (0, present);
      m_device->set_buffer (1, m_exposure_value);
      m_device->set_texture (0, m_probe == "scene" ? m_scene_color : m_image);
      m_device->set_texture (1, m_bloom[0]);
      m_device->draw (3);
      m_presented = true;
    }
  }
}
