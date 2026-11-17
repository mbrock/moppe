// The post effects over the scene: ambient occlusion and light shafts
// before the resolve, then underwater, motion blur, the sun's glare, and
// bloom over the resolved image.

#include <moppe/nhal/renderer/nhal_renderer_impl.hh>

namespace moppe::nhal {
  namespace detail {
    void NhalRenderer::make_post_pipelines () {
      RenderPipelineDesc bright;
      bright.program = &shaders::bloom_bright::program;
      bright.vertex = m_shaders.bloom_bright[0];
      bright.fragment = m_shaders.bloom_bright[1];
      bright.color_formats[0] = Format::rgba16_float;
      m_post.bloom_bright = m_device->create_render_pipeline (bright);
      RenderPipelineDesc blur = bright;
      blur.program = &shaders::bloom_blur::program;
      blur.vertex = m_shaders.bloom_blur[0];
      blur.fragment = m_shaders.bloom_blur[1];
      m_post.bloom_blur = m_device->create_render_pipeline (blur);
      RenderPipelineDesc smear = bright;
      smear.program = &shaders::motion_blur::program;
      smear.vertex = m_shaders.motion_blur[0];
      smear.fragment = m_shaders.motion_blur[1];
      m_post.motion_blur = m_device->create_render_pipeline (smear);

      RenderPipelineDesc underwater = bright;
      underwater.program = &shaders::underwater::program;
      underwater.vertex = m_shaders.underwater[0];
      underwater.fragment = m_shaders.underwater[1];
      underwater.label = "underwater";
      m_post.underwater = m_device->create_render_pipeline (underwater);

      RenderPipelineDesc occlusion;
      occlusion.program = &shaders::gtao::program;
      occlusion.vertex = m_shaders.gtao[0];
      occlusion.fragment = m_shaders.gtao[1];
      occlusion.color_formats[0] = Format::r16_float;
      m_post.gtao = m_device->create_render_pipeline (occlusion);
      RenderPipelineDesc occlusion_blur = occlusion;
      occlusion_blur.program = &shaders::gtao_blur::program;
      occlusion_blur.vertex = m_shaders.gtao_blur[0];
      occlusion_blur.fragment = m_shaders.gtao_blur[1];
      m_post.gtao_blur = m_device->create_render_pipeline (occlusion_blur);
      RenderPipelineDesc occlusion_apply = occlusion_blur;
      occlusion_apply.color_formats[0] = Format::rgba16_float;
      occlusion_apply.blend[0] = Blend::premultiplied;
      m_post.gtao_apply = m_device->create_render_pipeline (occlusion_apply);
      RenderPipelineDesc shafts;
      shafts.program = &shaders::shafts::program;
      shafts.vertex = m_shaders.shafts[0];
      shafts.fragment = m_shaders.shafts[1];
      shafts.color_formats[0] = Format::rgba16_float;
      shafts.blend[0] = Blend::additive;
      m_post.shafts = m_device->create_render_pipeline (shafts);
    }

    shaders::gtao::Post NhalRenderer::post_block (const position_t& eye,
                                                  const direction_t& forward,
                                                  const Vec3& right,
                                                  const Vec3& up) const {
      shaders::gtao::Post post {};
      post.eye = lanes (position_value (eye));
      post.ray_forward = lanes (direction_value (forward));
      post.ray_right = lanes (right);
      post.ray_up = lanes (up);
      post.occlusion = m_post.block.occlusion;
      return post;
    }

    void NhalRenderer::apply_gtao (const render::GtaoParams& params) {
      if (m_resolved)
        return;
      close_scene ();
      shaders::gtao::Post post = post_block (
        params.camera_pos, params.forward, params.right_span, params.up_span);
      post.occlusion = { params.radius.numerical_value_in (u::m),
                         params.strength.numerical_value_in (mp_units::one),
                         params.near_plane.numerical_value_in (u::m),
                         params.far_plane.numerical_value_in (u::m) };
      m_post.block = post;
      auto pass = [&] (Texture target, const char* label) {
        RenderPassDesc desc;
        desc.label = label;
        desc.color_count = 1;
        desc.colors[0] = { target,
                           target == m_scene_color ? Load::load : Load::discard,
                           Store::store };
        m_device->begin_render_pass (desc);
      };
      pass (m_post.ao[0], "occlusion");
      m_device->set_pipeline (m_post.gtao);
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
        pass (i ? m_scene_color : m_post.ao[1], "occlusion blur");
        m_device->set_pipeline (i ? m_post.gtao_apply : m_post.gtao_blur);
        shaders::gtao::Post blurred = post;
        blurred.blur = { steps[i][0], steps[i][1], float (i), 0 };
        m_device->set_uniforms (0, blurred);
        m_device->set_texture (0, m_post.ao[i]);
        m_device->set_texture (1, m_scene_depth);
        m_device->draw (3);
        m_device->end_render_pass ();
      }
    }

    void
    NhalRenderer::apply_light_shafts (const render::LightShaftParams& params) {
      if (m_frame_values.shadow[0] <= 0.0f || m_resolved)
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
      m_device->set_pipeline (m_post.shafts);
      m_device->set_buffer (0, m_frame_block);
      m_device->set_uniforms (1, post);
      m_device->set_texture (0, m_scene_depth);
      m_device->set_texture (8, m_sun_shadow.map);
      m_device->draw (3);
      m_device->end_render_pass ();
    }

    void
    NhalRenderer::apply_underwater (const render::UnderwaterParams& params) {
      if (m_resolved || !m_water.levels || !m_terrain.have)
        return;
      close_scene ();
      if (!m_post.submerged)
        m_post.submerged =
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
      pass.colors[0] = { m_post.submerged, Load::discard, Store::store };
      m_device->begin_render_pass (pass);
      m_device->set_pipeline (m_post.underwater);
      m_device->set_buffer (0, m_frame_block);
      m_device->set_uniforms (1, terrain_block ());
      m_device->set_uniforms (2, below);
      m_device->set_uniforms (3, sea_block (seconds_value (m_params.time)));
      m_device->set_texture (0, m_scene_color);
      m_device->set_texture (1, m_scene_depth);
      m_device->set_texture (2, m_terrain.heights);
      m_device->set_texture (11, m_water.levels);
      m_device->set_texture (12, m_water.flow ? m_water.flow : m_water.still);
      m_device->draw (3);
      m_device->end_render_pass ();
      std::swap (m_scene_color, m_post.submerged);
    }

    void NhalRenderer::apply_motion_blur (float strength) {
      if (strength <= 0.0f)
        return;
      reconstruct_scene ();
      if (!m_post.blurred)
        m_post.blurred =
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
      desc.colors[0] = { m_post.blurred, Load::discard, Store::store };
      m_device->begin_render_pass (desc);
      m_device->set_pipeline (m_post.motion_blur);
      m_device->set_buffer (0, m_frame_block);
      m_device->set_uniforms (1, smear);
      m_device->set_texture (0, m_image);
      m_device->set_texture (1, m_scene_motion);
      m_device->set_texture (2, m_scene_depth);
      m_device->draw (3);
      m_device->end_render_pass ();
      m_image = m_post.blurred;
    }

    std::array<float, 4> NhalRenderer::sun_glare () const {
      const Mat4 view_proj = m_params.proj * m_params.view;
      const Vec3& sun = direction_value (m_params.sun_dir);
      const float visibility =
        m_params.sun_visibility.numerical_value_in (mp_units::one);
      auto row = [&] (int r) {
        return view_proj.element (r) * sun[0] +
               view_proj.element (4 + r) * sun[1] +
               view_proj.element (8 + r) * sun[2];
      };
      const float w = row (3);
      if (w <= 0.0f || visibility <= 0.0f)
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
               visibility * std::min (edge, 1.0f),
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
      pass (m_post.bloom[0], "bloom bright pass");
      m_device->set_pipeline (m_post.bloom_bright);
      m_device->set_uniforms (0, present);
      m_device->set_buffer (1, m_exposure_value);
      m_device->set_texture (0, m_image);
      m_device->draw (3);
      m_device->end_render_pass ();
      const std::array<float, 2> steps[2] = {
        { 1.0f / float (m_post.bloom_width), 0.0f },
        { 0.0f, 1.0f / float (m_post.bloom_height) },
      };
      for (int i = 0; i < 2; ++i) {
        pass (m_post.bloom[1 - i], "bloom blur");
        m_device->set_pipeline (m_post.bloom_blur);
        shaders::bloom_blur::Blur blur {};
        blur.direction = { steps[i][0], steps[i][1], 0, 0 };
        m_device->set_uniforms (0, blur);
        m_device->set_texture (0, m_post.bloom[i]);
        m_device->draw (3);
        m_device->end_render_pass ();
      }
    }
  }
}
