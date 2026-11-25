// The frame: render() in the pass order of the original GL build, the
// loading and game-over screens; the captures taken of it are
// frame_capture.hh's.

#include <moppe/game/game.hh>
#include <moppe/environment.hh>
#include <moppe/profile.hh>

namespace moppe {
  namespace game {
    void MoppeGame::render (render::Renderer& r) {
      MOPPE_PROFILE_FRAME ();
      MOPPE_PROFILE_ZONE ("MoppeGame::render");
      if (!m_ready) {
        render_loading (r);
        return;
      }
      if (logic ().m_game_over) {
        render_game_over (r);
        return;
      }

      const float aspect =
        (float)r.width_pts () / std::max (1, r.height_pts ());
      auto input = frame_view_input (aspect);
      if (environment ("MOPPE_MATERIAL_PROBE")) {
        static unsigned index = 0;
        const float jump = index >= 244 && environment ("MOPPE_PROBE_JUMP")
                             ? 2000.0f : 0.0f;
        const float along = 95.0f + jump + float (index++) / 3.0f;
        const auto& road = m_places.bus;
        if (road.empty ()) throw std::runtime_error ("Probe requires paved loop");
        const auto eye = position_value (road.point (along * u::m)) + Vec3 (0, 2.2f, 0);
        const auto target = position_value (road.point ((along + 10) * u::m)) + Vec3 (0, 0.7f, 0);
        if (const char* dir = environment ("MOPPE_PROBE_STILLS")) {
          if (index >= 160 && index < 176)
            r.request_screenshot (std::string (dir) + "/" + std::to_string (index) + ".png");
        }
        input.selected_camera.position = position (eye);
        input.selected_camera.forward = unit_direction (normalized (target - eye));
        input.selected_camera.view = Mat4::look_at (eye, target, Vec3 (0, 1, 0));
      }
      FrameView frame = compose_frame_view (input);
      m_company.light_fire (frame);
      m_places.light_beacons (frame, logic ().m_beacon_burn);
      m_places.light_barn (frame);
      const bool cinematic = frame.visibility.cinematic;
      // A cut is a new view: the temporal history of the last one would
      // only smear into it.
      if (m_opening.take_cut ())
        r.reset_temporal_state ();

      const bool captured =
        m_capture.before_frame (r,
                                frame,
                                seconds (static_cast<float> (
                                  seconds_into_play (logic ().m_total_time))),
                                m_sky.sky ().sun_height,
                                m_opening.active ());
      render::FrameParams params = frame_params_for (frame);
      const SnowPack& snow = logic ().m_snow;
      auto corner = SnowPack::tile_at (plan (frame.camera.position));
      for (int& axis : corner)
        axis -= SnowPack::window / 2;
      if (snow.revision () != m_snow_revision ||
          (snow.revision () && corner != m_snow_origin)) {
        const bool any = snow.read (corner, m_snow_pixels);
        if (!any)
          m_snow_pixels.clear ();
        const meters_t tile = SnowPack::tile * SnowPack::spacing;
        r.set_snow_relief (plan_position (corner[0] * tile, corner[1] * tile),
                           SnowPack::window * tile,
                           SnowPack::depth,
                           SnowPack::pixels,
                           m_snow_pixels);
        m_snow_revision = snow.revision ();
        m_snow_origin = corner;
      }
      if (!m_capture.keeping_pace ())
        params.frame_rate_target = 0.0f * u::Hz;
      if (!r.begin_frame (params))
        return;

      draw_world_layers (r, frame);
      draw_actor_layers (r, frame);
      draw_sky_and_undergrowth (r, frame);
      apply_scene_occlusion (r, frame);

      draw_water_surfaces (r, frame);
      r.draw_model_glass ();
      draw_boat_wakes (r, frame);
      draw_actor_glows (r, frame);
      draw_effect_layers (r, frame);

      m_heads_up.draw (r, hud_world (), m_opening, frame);

      r.end_frame ();
      m_capture.after_frame (captured);
    }

    void MoppeGame::render_loading (render::Renderer& r) {
      const float width = static_cast<float> (r.width_pts ());
      const float height = static_cast<float> (r.height_pts ());

      // Take the finished world now, but run the heavy finishing work
      // after this frame is submitted, so the panel first shows what is
      // about to happen.
      std::unique_ptr<GeneratedWorld> completed =
        m_loading.take_completed_world ();
      if (completed)
        m_loading.report ("Finishing the world",
                          "Growing forests and planning the first journey");

      const LoadingStatus loading = m_loading.status ();

      // A fixed camera watching the sky is the whole scene; the panel
      // below carries the actual information.
      const position_t eye = position (Vec3 (0.0f, 34.0f, 0.0f));
      const position_t target = position (Vec3 (0.0f, 27.0f, -100.0f));
      render::FrameParams fp;
      // The loading screen sizes the same render targets the game will use.
      // Leaving the budget off here would build a full-drawable set only to
      // replace it on the first world frame.
      fp.scene_scale = m_graphics.scene_scale;
      fp.render_scale_override = m_graphics.render_scale_override;
      fp.scene_megapixel_budget = m_graphics.scene_megapixel_budget;
      fp.view = Mat4::look_at (
        position_value (eye), position_value (target), Vec3 (0, 1, 0));
      fp.proj = Mat4::perspective_reversed (64.0f * u::deg,
                                            width / std::max (1.0f, height),
                                            render::camera_near_m,
                                            render::camera_far_m);
      fp.camera_pos = eye;
      const proportion_t loading_sun_height = 0.70f * proportion[one];
      fp.clear_color = horizon_color_for (loading_sun_height);
      fp.sun_dir = direction_of (displacement (Vec3 (0.82f, 0.58f, 0.0f)));
      sun_light_colors_for (
        loading_sun_height, fp.sun_diffuse, fp.sun_specular);
      fp.ambient = DisplayColor (0.58f, 0.55f, 0.48f);
      fp.time = loading.elapsed;
      fp.exposure_bias = 1.0f * scale_factor[one];
      fp.sun_visibility = 0.32f * proportion[one];
      if (!r.begin_frame (fp)) {
        // The world must not be dropped just because no frame started.
        if (completed)
          finish_loading (r, std::move (completed));
        return;
      }

      render::SkyParams sky;
      sky.time = fp.time;
      sky.sun_height = loading_sun_height;
      sky.cloudiness = 0.14f * proportion[one];
      sky.sun_dir = fp.sun_dir;
      sky.fog_color = fp.clear_color;
      r.draw_sky (sky);

      m_heads_up.dl.clear ();
      m_heads_up.text.clear ();
      render::DrawState state;
      state.blend = true;
      state.depth_test = false;
      state.depth_write = false;
      state.cull = false;
      m_heads_up.dl.state (state);
      m_heads_up.dl.lit (false);
      m_heads_up.dl.fogged (false);

      // No panel: a soft dusk rises from the bottom edge behind a few
      // lines of type, and the sky stays the picture.
      if (m_heads_up.hud.font ()) {
        const float scrim = std::min (height, 300.0f);
        m_heads_up.dl.begin (render::Prim::Quads);
        m_heads_up.dl.color (0.0f, 0.02f, 0.03f, 0.0f);
        m_heads_up.dl.vertex (0.0f, height - scrim);
        m_heads_up.dl.vertex (width, height - scrim);
        m_heads_up.dl.color (0.0f, 0.02f, 0.03f, 0.42f);
        m_heads_up.dl.vertex (width, height);
        m_heads_up.dl.vertex (0.0f, height);
        m_heads_up.dl.end ();

        const float left = 44.0f;
        const float content_width = std::min (520.0f, width - 2.0f * left);
        const float bottom = height - 44.0f;
        const auto style = [] (float size, float alpha, float tracking) {
          render::TextStyle s;
          s.size = size;
          s.red = s.green = s.blue = 0.96f;
          s.alpha = alpha;
          s.tracking = tracking;
          s.tabular_figures = true;
          return s;
        };

        std::ostringstream eyebrow;
        eyebrow << "SEED " << loading.seed;
        m_heads_up.hud.draw_shaded (m_heads_up.text,
                                    left * u::pt,
                                    (bottom - 118.0f) * u::pt,
                                    eyebrow.str (),
                                    style (9.0f, 0.62f, 0.16f));
        m_heads_up.hud.draw_shaded (m_heads_up.text,
                                    left * u::pt,
                                    (bottom - 86.0f) * u::pt,
                                    loading.title,
                                    style (27.0f, 0.96f, 0.0f));
        m_heads_up.hud.draw_shaded (m_heads_up.text,
                                    left * u::pt,
                                    (bottom - 62.0f) * u::pt,
                                    loading.detail,
                                    style (14.0f, 0.74f, 0.01f));

        // The rail fills only with a real measurement; stages that cannot
        // measure themselves show their text and nothing else.
        const auto fill_rect = [this] (float x, float y, float w, float h) {
          m_heads_up.dl.begin (render::Prim::Quads);
          m_heads_up.dl.vertex (x, y);
          m_heads_up.dl.vertex (x + w, y);
          m_heads_up.dl.vertex (x + w, y + h);
          m_heads_up.dl.vertex (x, y + h);
          m_heads_up.dl.end ();
        };
        const float rail_y = bottom - 44.0f;
        m_heads_up.dl.color (0.96f, 0.96f, 0.96f, 0.16f);
        fill_rect (left, rail_y, content_width, 1.0f);
        if (loading.progress) {
          const proportion_t done = *loading.progress;
          m_heads_up.dl.color (0.96f, 0.96f, 0.96f, 0.78f);
          fill_rect (left,
                     rail_y,
                     content_width *
                       std::clamp (done.numerical_value_in (one), 0.0f, 1.0f),
                     1.0f);
          std::ostringstream percent;
          percent << static_cast<int> (
                       std::lround (done.numerical_value_in (one) * 100.0f))
                  << '%';
          m_heads_up.hud.draw_shaded (m_heads_up.text,
                                      (left + content_width) * u::pt,
                                      (rail_y - 8.0f) * u::pt,
                                      percent.str (),
                                      style (9.0f, 0.62f, 0.06f),
                                      render::TextAlign::Right);
        }

        const std::size_t history_end =
          loading.events.empty () ? 0 : loading.events.size () - 1;
        const std::size_t history_begin = history_end > 2 ? history_end - 2 : 0;
        float line_y = bottom - 20.0f;
        for (std::size_t i = history_begin; i < history_end; ++i) {
          const LoadingEvent& event = loading.events[i];
          std::ostringstream line;
          line << std::fixed << std::setprecision (1)
               << event.elapsed.numerical_value_in (si::second) << " s   "
               << event.title;
          m_heads_up.hud.draw_shaded (m_heads_up.text,
                                      left * u::pt,
                                      line_y * u::pt,
                                      line.str (),
                                      style (10.0f, 0.46f, 0.02f));
          line_y += 16.0f;
        }
      }

      r.draw_hud_text (m_heads_up.text);
      r.draw_hud (m_heads_up.dl);
      r.end_frame ();

      // The frame announcing the finish is on its way to the display;
      // now do the finishing work.
      if (completed)
        finish_loading (r, std::move (completed));
    }

    void MoppeGame::render_game_over (render::Renderer& r) {
      render::FrameParams fp;
      fp.scene_scale = m_graphics.scene_scale;
      fp.render_scale_override = m_graphics.render_scale_override;
      fp.scene_megapixel_budget = m_graphics.scene_megapixel_budget;
      fp.clear_color = DisplayColor (0, 0, 0);
      fp.view = Mat4 ();
      fp.proj = Mat4 ();
      if (!r.begin_frame (fp))
        return;

      m_heads_up.dl.clear ();
      m_heads_up.text.clear ();
      m_heads_up.hud.draw_game_over (
        m_heads_up.dl,
        m_heads_up.text,
        static_cast<float> (r.width_pts ()) * u::pt,
        static_cast<float> (r.height_pts ()) * u::pt);
      m_heads_up.hud.publish_offers ();
      r.draw_hud_text (m_heads_up.text);
      r.draw_hud (m_heads_up.dl);
      r.end_frame ();
    }

  }
}
