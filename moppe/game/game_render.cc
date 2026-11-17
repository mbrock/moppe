// The frame: render() in the pass order of the original GL build, the
// loading and game-over screens; the captures taken of it are
// frame_capture.hh's.

#include <moppe/game/game.hh>
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
      FrameView frame = compose_frame_view (frame_view_input (aspect));
      m_company.light_fire (frame);
      m_places.light_beacons (frame, logic ().m_beacon_burn);
      m_places.light_barn (frame);
      const bool cinematic = frame.visibility.cinematic;
      // A cut is a new view: the temporal history of the last one would
      // only smear into it.
      if (m_opening.take_cut ())
        r.reset_temporal_state ();

      const bool captured = m_capture.before_frame (
        r,
        frame,
        static_cast<float> (seconds_into_play (logic ().m_total_time)),
        m_sky.sky ().sun_height,
        m_opening.active ());
      render::FrameParams params = frame_params_for (frame);
      if (!m_capture.keeping_pace ())
        params.frame_rate_target = 0.0f;
      if (!r.begin_frame (params))
        return;

      draw_world_layers (r, frame);
      draw_actor_layers (r, frame);

      draw_water_surfaces (r, frame);
      draw_boat_wakes (r, frame);
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
      const Vec3 eye (0.0f, 34.0f, 0.0f);
      const Vec3 target (0.0f, 27.0f, -100.0f);
      render::FrameParams fp;
      // The loading screen sizes the same render targets the game will use.
      // Leaving the budget off here would build a full-drawable set only to
      // replace it on the first world frame.
      fp.scene_scale = m_graphics.scene_scale;
      fp.render_scale_override = m_graphics.render_scale_override;
      fp.scene_megapixel_budget = m_graphics.scene_megapixel_budget;
      fp.view = Mat4::look_at (eye, target, Vec3 (0, 1, 0));
      fp.proj = Mat4::perspective_reversed (64.0f * u::deg,
                                            width / std::max (1.0f, height),
                                            render::camera_near_m,
                                            render::camera_far_m);
      fp.camera_pos = eye;
      constexpr float loading_sun_height = 0.70f;
      fp.clear_color = horizon_color_for (loading_sun_height);
      fp.sun_dir = normalized (Vec3 (0.82f, 0.58f, 0.0f));
      sun_light_colors_for (
        loading_sun_height, fp.sun_diffuse, fp.sun_specular);
      fp.ambient = DisplayColor (0.58f, 0.55f, 0.48f);
      fp.time = loading.elapsed;
      fp.exposure_bias = 1.0f;
      fp.sun_visibility = 0.32f;
      if (!r.begin_frame (fp)) {
        // The world must not be dropped just because no frame started.
        if (completed)
          finish_loading (r, std::move (completed));
        return;
      }

      render::SkyParams sky;
      sky.time = loading.elapsed;
      sky.sun_height = loading_sun_height;
      sky.cloudiness = 0.14f;
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
                                    left,
                                    bottom - 118.0f,
                                    eyebrow.str (),
                                    style (9.0f, 0.62f, 0.16f));
        m_heads_up.hud.draw_shaded (m_heads_up.text,
                                    left,
                                    bottom - 86.0f,
                                    loading.title,
                                    style (27.0f, 0.96f, 0.0f));
        m_heads_up.hud.draw_shaded (m_heads_up.text,
                                    left,
                                    bottom - 62.0f,
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
        if (loading.progress >= 0.0f) {
          m_heads_up.dl.color (0.96f, 0.96f, 0.96f, 0.78f);
          fill_rect (left,
                     rail_y,
                     content_width * std::clamp (loading.progress, 0.0f, 1.0f),
                     1.0f);
          std::ostringstream percent;
          percent << static_cast<int> (std::lround (loading.progress * 100.0f))
                  << '%';
          m_heads_up.hud.draw_shaded (m_heads_up.text,
                                      left + content_width,
                                      rail_y - 8.0f,
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
          line << std::fixed << std::setprecision (1) << event.elapsed
               << " s   " << event.title;
          m_heads_up.hud.draw_shaded (m_heads_up.text,
                                      left,
                                      line_y,
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
        m_heads_up.dl, m_heads_up.text, r.width_pts (), r.height_pts ());
      m_heads_up.hud.publish_offers ();
      r.draw_hud_text (m_heads_up.text);
      r.draw_hud (m_heads_up.dl);
      r.end_frame ();
    }

  }
}
