// The frame: render() in the pass order of the original GL build, the
// loading and game-over screens, and the captures taken of it (screenshots,
// posed shots, and videos).

#include <moppe/game/game.hh>

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
      light_companion_fire (frame);
      light_beacons (frame);
      light_barn (frame);
      const bool cinematic = frame.visibility.cinematic;
      // A cut is a new view: the temporal history of the last one would
      // only smear into it.
      if (m_opening.active () &&
          m_opening.shot_index () != m_opening_rendered_shot) {
        if (m_opening_rendered_shot != SIZE_MAX)
          r.reset_temporal_state ();
        m_opening_rendered_shot = m_opening.shot_index ();
      }

      static const int screenshot_delay = [] {
        if (const char* frames = moppe::environment ("MOPPE_SCREENSHOT_FRAMES"))
          return std::max (1, ::atoi (frames));
        return 30;
      }();
      const bool captured = !m_screenshot_path.empty () &&
                            ++m_screenshot_frames >= screenshot_delay;
      if (captured)
        r.request_screenshot (m_screenshot_path);
      if (m_snapshot_requested) {
        m_snapshot_requested = false;
        const std::string path = next_snapshot_path ();
        r.request_screenshot (path);
        record_shot_pose (frame, path);
      }
      record_frame (r);
      render::FrameParams params = frame_params_for (frame);
      if (!keeping_pace ())
        params.frame_rate_target = 0.0f;
      if (!r.begin_frame (params))
        return;

      draw_world_layers (r, frame);
      draw_actor_layers (r, frame);

      draw_water_surfaces (r, frame);
      draw_boat_wakes (r, frame);
      draw_effect_layers (r, frame);

      draw_overlays (r, frame);

      r.end_frame ();
      if (captured) {
        m_screenshot_path.clear ();
        platform::request_quit ();
      }
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
      fp.proj = Mat4::perspective_reversed (
        64.0f * u::deg, width / std::max (1.0f, height), 0.5f, 9000.0f);
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

      m_hud_dl.clear ();
      m_hud_text.clear ();
      render::DrawState state;
      state.blend = true;
      state.depth_test = false;
      state.depth_write = false;
      state.cull = false;
      m_hud_dl.state (state);
      m_hud_dl.lit (false);
      m_hud_dl.fogged (false);

      // No panel: a soft dusk rises from the bottom edge behind a few
      // lines of type, and the sky stays the picture.
      if (m_hud.font ()) {
        const float scrim = std::min (height, 300.0f);
        m_hud_dl.begin (render::Prim::Quads);
        m_hud_dl.color (0.0f, 0.02f, 0.03f, 0.0f);
        m_hud_dl.vertex (0.0f, height - scrim);
        m_hud_dl.vertex (width, height - scrim);
        m_hud_dl.color (0.0f, 0.02f, 0.03f, 0.42f);
        m_hud_dl.vertex (width, height);
        m_hud_dl.vertex (0.0f, height);
        m_hud_dl.end ();

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
        m_hud.draw_shaded (m_hud_text,
                           left,
                           bottom - 118.0f,
                           eyebrow.str (),
                           style (9.0f, 0.62f, 0.16f));
        m_hud.draw_shaded (m_hud_text,
                           left,
                           bottom - 86.0f,
                           loading.title,
                           style (27.0f, 0.96f, 0.0f));
        m_hud.draw_shaded (m_hud_text,
                           left,
                           bottom - 62.0f,
                           loading.detail,
                           style (14.0f, 0.74f, 0.01f));

        // The rail fills only with a real measurement; stages that cannot
        // measure themselves show their text and nothing else.
        const auto fill_rect = [this] (float x, float y, float w, float h) {
          m_hud_dl.begin (render::Prim::Quads);
          m_hud_dl.vertex (x, y);
          m_hud_dl.vertex (x + w, y);
          m_hud_dl.vertex (x + w, y + h);
          m_hud_dl.vertex (x, y + h);
          m_hud_dl.end ();
        };
        const float rail_y = bottom - 44.0f;
        m_hud_dl.color (0.96f, 0.96f, 0.96f, 0.16f);
        fill_rect (left, rail_y, content_width, 1.0f);
        if (loading.progress >= 0.0f) {
          m_hud_dl.color (0.96f, 0.96f, 0.96f, 0.78f);
          fill_rect (left,
                     rail_y,
                     content_width * std::clamp (loading.progress, 0.0f, 1.0f),
                     1.0f);
          std::ostringstream percent;
          percent << static_cast<int> (std::lround (loading.progress * 100.0f))
                  << '%';
          m_hud.draw_shaded (m_hud_text,
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
          m_hud.draw_shaded (
            m_hud_text, left, line_y, line.str (), style (10.0f, 0.46f, 0.02f));
          line_y += 16.0f;
        }
      }

      r.draw_hud_text (m_hud_text);
      r.draw_hud (m_hud_dl);
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

      m_hud_dl.clear ();
      m_hud_text.clear ();
      m_hud.draw_game_over (
        m_hud_dl, m_hud_text, r.width_pts (), r.height_pts ());
      m_hud.publish_offers ();
      r.draw_hud_text (m_hud_text);
      r.draw_hud (m_hud_dl);
      r.end_frame ();
    }

    std::optional<MoppeGame::VideoPlan> MoppeGame::plan_video () {
      const char* path = moppe::environment ("MOPPE_VIDEO");
      if (!path || !*path)
        return std::nullopt;
      VideoPlan plan;
      plan.path = path;
      const auto number = [] (const char* name, float otherwise) {
        const char* value = moppe::environment (name);
        return value ? (float)::atof (value) : otherwise;
      };
      plan.fps = number ("MOPPE_VIDEO_FPS", 30.0f) > 45.0f ? 60 : 30;
      plan.seconds = std::max (0.0f, number ("MOPPE_VIDEO_SECONDS", 0.0f));
      plan.start = std::max (0.0f, number ("MOPPE_VIDEO_START", 0.0f));
      plan.speed =
        std::max (1, static_cast<int> (number ("MOPPE_VIDEO_SPEED", 1.0f)));
      plan.height =
        std::max (0, static_cast<int> (number ("MOPPE_VIDEO_HEIGHT", 0.0f)));
      return plan;
    }

    void MoppeGame::record_frame (render::Renderer& r) {
      const auto take = [&] {
        r.request_frame (
          [this] (const render::FramePixels& frame) { m_video.add (frame); });
      };
      const int index = m_video_render_frame++;
      if (!m_video_plan) {
        // A video begun with V takes every second frame as they come.
        if (m_video.recording () && index % 2 == 0)
          take ();
        return;
      }
      if (m_video_done)
        return;
      const VideoPlan& plan = *m_video_plan;
      const int first = static_cast<int> (std::lround (plan.start * 60.0f));
      if (index < first)
        return;
      if (index == first)
        m_video.begin (plan.path, plan.fps, plan.height);
      // A video of the river flight is over when the flight is: the
      // drone comes down a little sooner than its planned seconds, and
      // what follows is only the walker standing at the trailhead.
      static const bool flight_only = [] {
        const char* opening = moppe::environment ("MOPPE_OPENING");
        return opening && std::string_view (opening) == "river";
      }();
      if (flight_only && opening_active ())
        m_video_saw_flight = true;
      const bool landed =
        flight_only && m_video_saw_flight && !opening_active ();
      if (!m_video.recording () || landed ||
          (plan.seconds > 0.0f &&
           m_video.frames () >= std::lround (plan.seconds * plan.fps))) {
        m_video.finish ();
        m_video_done = true;
        platform::request_quit ();
        return;
      }
      if ((index - first) % (60 / plan.fps * plan.speed) == 0)
        take ();
    }

    std::string MoppeGame::next_clip_path () {
      std::ostringstream path;
      path << snapshot_directory () << "/clip-" << std::setfill ('0')
           << std::setw (3) << m_clip_count++ << ".mp4";
      return path.str ();
    }

    const std::string& MoppeGame::snapshot_directory () {
      namespace fs = std::filesystem;
      if (m_snapshot_directory.empty ()) {
        char stamp[32];
        const std::time_t now = std::time (nullptr);
        std::strftime (
          stamp, sizeof stamp, "run-%Y%m%d-%H%M%S", std::localtime (&now));
        fs::path directory = fs::path ("screenshots") / stamp;
        std::error_code error;
        fs::create_directories (directory, error);
        if (error) {
          directory = fs::temp_directory_path () / "moppe-screenshots" / stamp;
          fs::create_directories (directory, error);
        }
        m_snapshot_directory = directory.string ();
      }
      return m_snapshot_directory;
    }

    std::string MoppeGame::next_snapshot_path () {
      std::ostringstream path;
      path << snapshot_directory () << "/shot-" << std::setfill ('0')
           << std::setw (3) << m_snapshot_count++ << ".png";
      std::cerr << "moppe: screenshot " << path.str () << '\n';
      return path.str ();
    }

    void MoppeGame::record_shot_pose (const FrameView& frame,
                                      const std::string& path) {
      const std::string name = std::filesystem::path (path).stem ().string ();
      const std::string line = format_opening_shot (
        name,
        frame.camera.position,
        frame.camera.forward,
        frame.camera.field_of_view.numerical_value_in (u::deg),
        logic ().m_total_time,
        m_sky.sun_height);
      std::cerr << "moppe: pose: " << line << '\n';
      std::ofstream shots (std::filesystem::path (m_snapshot_directory) /
                             "shots.txt",
                           std::ios::app);
      shots << line << '\n';
    }
  }
}
