// The frame around the scene: beginning it, the scene's and the drawable's
// targets, the scene pass opened and closed, the temporal resolve, the
// tonemap and the HUD, and the end of the frame with its timings.

#include <moppe/nhal/renderer/nhal_renderer_impl.hh>

namespace moppe::nhal {
  namespace detail {
    void NhalRenderer::make_frame_pipelines () {
      const RenderPipelineDesc scene = scene_target ();
      m_exposure = m_device->create_compute_pipeline (
        { &shaders::exposure::program, m_shaders.exposure, "exposure" });
      RenderPipelineDesc rain = scene;
      rain.program = &shaders::rain::program;
      rain.vertex = m_shaders.rain[0];
      rain.fragment = m_shaders.rain[1];
      rain.blend[0] = Blend::alpha;
      rain.depth_write = false;
      rain.cull = Cull::none;
      rain.label = "drizzle";
      m_rain = m_device->create_render_pipeline (rain);

      RenderPipelineDesc resolve;
      resolve.program = &shaders::resolve::program;
      resolve.vertex = m_shaders.resolve[0];
      resolve.fragment = m_shaders.resolve[1];
      resolve.color_formats[0] = Format::rgba16_float;
      m_resolve = m_device->create_render_pipeline (resolve);
      RenderPipelineDesc neighbourhood = resolve;
      neighbourhood.program = &shaders::neighbourhood::program;
      neighbourhood.vertex = m_shaders.neighbourhood[0];
      neighbourhood.fragment = m_shaders.neighbourhood[1];
      neighbourhood.color_count = 2;
      neighbourhood.color_formats[1] = Format::rgba16_float;
      m_neighbourhood = m_device->create_render_pipeline (neighbourhood);

      RenderPipelineDesc present;
      present.program = &shaders::present::program;
      present.vertex = m_shaders.present[0];
      present.fragment = m_shaders.present[1];
      present.color_formats[0] = m_device->surface_format ();
      m_present = m_device->create_render_pipeline (present);

      RenderPipelineDesc hud = present;
      hud.program = &shaders::hud::program;
      hud.vertex = m_shaders.hud[0];
      hud.fragment = m_shaders.hud[1];
      hud.blend[0] = Blend::alpha;
      m_hud = m_device->create_render_pipeline (hud);

      RenderPipelineDesc text = hud;
      text.program = &shaders::slug_text::program;
      text.vertex = m_shaders.slug_text[0];
      text.fragment = m_shaders.slug_text[1];
      m_slug_text = m_device->create_render_pipeline (text);
    }

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
      m_terrain.chunks.clear ();
      m_scene_open = m_scene_begun = m_scene_closed = m_resolved = m_presented =
        false;
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
      frame.relief = { m_terrain.params.sea_level,
                       m_terrain.params.land_relief,
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
      frame.temporal_blend = {
        m_restart ? 1.0f : 0.1f, 0.0f, float (m_width), float (m_height)
      };
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

    void NhalRenderer::end_frame () {
      open_present ();
      m_device->end_render_pass ();
      end_device_frame ();
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
      for (Texture t : { m_history[0],
                         m_history[1],
                         m_post.blurred,
                         m_post.bloom[0],
                         m_post.bloom[1] })
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
      m_post.blurred = {};
      m_post.bloom_width = std::max (1u, m_width / 4);
      m_post.bloom_height = std::max (1u, m_height / 4);
      for (Texture& t : m_post.bloom)
        t = m_device->create_texture ({ m_post.bloom_width,
                                        m_post.bloom_height,
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
                         m_water.beneath,
                         m_water.beneath_depth,
                         m_bounds[0],
                         m_bounds[1],
                         m_post.ao[0],
                         m_post.ao[1],
                         m_post.submerged })
        if (t)
          m_device->destroy (t);
      m_post.submerged = {};
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
      m_water.beneath =
        m_device->create_texture ({ m_scene_width,
                                    m_scene_height,
                                    Format::rgba16_float,
                                    usage_render_target | usage_sampled,
                                    1,
                                    "scene beneath water" });
      m_water.beneath_depth =
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
      for (Texture& t : m_post.ao)
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
      m_device->set_texture (0, m_image);
      m_device->set_texture (1, m_post.bloom[0]);
      m_device->draw (3);
      m_presented = true;
    }
  }
}
