// The game shell's construction, its input, and the simulation step: what
// each tick asks of the world, the session, and the scripts and captures
// driving them. The class is in game.hh; the rest of its members live in the
// game_*.cc files beside this one.

#include <moppe/game/game.hh>

namespace moppe {
  namespace game {
    MoppeGame::MoppeGame (const LaunchOptions& options,
                          terrain::WorldRecipe recipe)
        : m_params (bind_world_params (options.world, recipe)),
          m_recipe (std::move (recipe)),
          m_loading (this->recipe (), options.world_cache),
          m_graphics (options.graphics),
          m_spawn_position (position_value (this->world ().spawn_position ())),
          m_renderer (0), m_screenshot_path (options.screenshot_path),
          m_water_shot (options.water_shot), m_gazetteer (options.gazetteer),
          m_screenshot_frames (0), m_ready (false),
          m_benchmark (options.benchmark),
          m_benchmark_baseline (options.graphics),
          m_bike_physics (options.bike_physics) {
      m_day = plan_day ();
      m_sky = m_almanac.held (m_graphics.sun_height);
      if (const char* name = moppe::environment ("MOPPE_WEATHER");
          name && std::string_view (name) != "changing") {
        m_held_sky = sky_kind_named (name);
        if (!m_held_sky)
          std::cerr << "moppe: weather: no kind named " << name
                    << "; the weather changes" << std::endl;
      }
      if (m_benchmark)
        m_benchmark_replay.emplace (GraphicsBenchmarkReplay::Config {
          m_benchmark->prelude_frames,
          m_benchmark->settle_frames,
          m_benchmark->measured_frames,
          m_benchmark->partition,
        });
      if (options.multiplayer && !m_benchmark && !m_gazetteer && !m_water_shot)
        seek_company (*options.multiplayer);
    }

    void MoppeGame::setup (render::Renderer& r, int, int) {
      MOPPE_PROFILE_ZONE ("MoppeGame::setup");
      m_renderer = &r;
      std::cerr << "moppe: simulation: fixed-step=120 Hz, catch-up-limit="
                << MAX_SIMULATION_CATCH_UP_STEPS << " steps, bike-physics="
                << (m_bike_physics == mov::BikePhysics::rigid ? "rigid"
                                                              : "classic")
                << std::endl;

      // Fast, main-thread resource setup; the heavy world build
      // runs behind the loading screen.
      {
        MOPPE_PROFILE_ZONE ("startup.load_hud");
        m_hud.load (r);
      }
      {
        MOPPE_PROFILE_ZONE ("startup.load_blob_shadow");
        m_blob.load (r);
      }

      {
        MOPPE_PROFILE_ZONE ("startup.dispatch_world_generation");
        m_loading.start (world (), recipe ());
      }
    }

    void MoppeGame::pointer_move (float, float, float dx, float dy) {
      if (!m_spectator) {
        if (m_ready && !m_pointer_free)
          m_live_input.look (dx, dy);
        return;
      }
      if (m_spectator->pointer_free)
        return;
      m_spectator->yaw += 0.004f * dx;
      m_spectator->pitch =
        std::clamp (m_spectator->pitch - 0.004f * dy, -1.45f, 1.45f);
    }

    void MoppeGame::tick (float elapsed) {
      if (!m_ready) {
        m_simulation_clock.reset ();
        return;
      }
      keep_company (elapsed);

      // Offline replay and cinematic capture deliberately bind one logical
      // step to one rendered frame. Ordinary play instead consumes the
      // presentation interval through a fixed 120 Hz clock.
      const bool frame_locked =
        m_benchmark.has_value () ||
        (opening_active () &&
         moppe::environment ("MOPPE_CINEMATIC_CAPTURE_DIR"));
      if (frame_locked) {
        m_simulation_clock.reset ();
        tick_simulation (elapsed);
        return;
      }
      // Orbit inspections and ride captures advance exactly one 60 Hz frame
      // of world time per rendered frame, so captured motion is even
      // however slowly the frames are written.
      static const bool capture_locked =
        moppe::environment ("MOPPE_ORBIT") ||
        moppe::environment ("MOPPE_PAN") ||
        moppe::environment ("MOPPE_RIDE_CAPTURE_DIR") ||
        moppe::environment ("MOPPE_VIDEO");
      if (capture_locked) {
        m_simulation_clock.reset ();
        // The time a video lets pass before it begins is not looked at,
        // so it goes by many steps to a drawn frame, until two seconds
        // before the first frame taken, which gives the picture's
        // history time to settle.
        // A remote control's taps are timed by drawn frames, so a
        // driven video keeps to one step each.
        int steps = 1;
        if (m_video_plan && !m_video_done &&
            !moppe::environment ("MOPPE_CONTROL_FILE")) {
          const int first =
            static_cast<int> (std::lround (m_video_plan->start * 60.0f));
          steps = std::clamp (first - 120 - m_video_render_frame, 1, 20);
        }
        for (int step = 0; step < steps; ++step)
          tick_simulation (1.0f / 60.0f);
        m_video_render_frame += steps - 1;
        return;
      }

      const int steps = m_simulation_clock.consume (elapsed);
      MOPPE_PROFILE_PLOT ("simulation.steps", steps);
      for (int step = 0; step < steps; ++step)
        tick_simulation (static_cast<float> (FIXED_SIMULATION_STEP_SECONDS));

      // The HUD reports rendered-frame cadence, not the internal 120 Hz
      // integration frequency. Gazetteer views intentionally report zero.
      if (m_ready && !m_gazetteer)
        logic ().m_frame_time = elapsed;
    }

    void MoppeGame::tick_simulation (float dt) {
      MOPPE_PROFILE_ZONE ("MoppeGame::tick_simulation");
      m_stepped += dt;
      std::optional<InputFrame> scripted_input;
      if (opening_active () &&
          moppe::environment ("MOPPE_CINEMATIC_CAPTURE_DIR")) {
        const int fps = [] {
          if (const char* value =
                moppe::environment ("MOPPE_CINEMATIC_CAPTURE_FPS"))
            return std::clamp (::atoi (value), 1, 120);
          return 30;
        }();
        dt = 1.0f / fps;
      }
      if (m_benchmark) {
        dt = GRAPHICS_BENCHMARK_DT;
        if (m_benchmark_submitted) {
          m_benchmark_render_frame.reset ();
          if (m_renderer->benchmark_complete () &&
              !m_benchmark_results_written) {
            m_renderer->write_benchmark_results ();
            m_benchmark_results_written = true;
            platform::request_quit ();
          }
          return;
        }
        prepare_benchmark_epoch ();
        m_benchmark_render_frame = m_benchmark_replay->current_frame ();
        if (!m_benchmark_render_frame)
          throw std::logic_error ("graphics benchmark has no replay frame");
        scripted_input = m_benchmark_render_frame->input;
      }
      if (m_benchmark_render_frame) {
        MOPPE_PROFILE_PLOT ("benchmark.mask", m_benchmark_mask);
        MOPPE_PROFILE_PLOT ("benchmark.partition_mask",
                            m_benchmark_render_frame->partition_mask);
        MOPPE_PROFILE_PLOT ("benchmark.epoch", m_benchmark_render_frame->epoch);
        MOPPE_PROFILE_PLOT ("benchmark.logical_frame",
                            m_benchmark_render_frame->logical_frame);
        MOPPE_PROFILE_PLOT ("benchmark.measured",
                            m_benchmark_render_frame->measured);
      }
      if (!m_ready)
        return;
      logic ().m_frame_time = dt;
      if (logic ().m_game_over)
        return;

      // The gazetteer is an offline frame composer, not a demo playback.
      // Simulation, actors, wind, and weather are frozen; only the camera's
      // terrain-aware sun visibility is derived anew for the current shot.
      if (m_gazetteer) {
        constexpr float documentary_time = 41.0f;
        logic ().m_frame_time = 0.0f;
        logic ().m_total_time = documentary_time;
        update_world_atmosphere (documentary_time);
        const FrameView view = compose_frame_view (frame_view_input (1.0f));
        logic ().m_flare = sun_visibility_target (view, world (), surface ());
        return;
      }

      InputFrame input = m_live_input.take_frame (dt);
      if (scripted_input)
        input = *scripted_input;

      logic ().m_total_time += dt;
      const float total_time = logic ().m_total_time;
      update_world_atmosphere (total_time);

      if (m_cinematic.active ()) {
        if (input.leave_cinematic) {
          leave_cinematic ();
          input = {};
        } else {
          const CinematicFlightControls controls {
            .lateral = input_value (input.turn),
            .lift = input_value (input.boost),
            .pace = input_value (input.drive),
          };
          m_cinematic.tick (dt, surface (), controls);
          if (!m_cinematic.active ())
            leave_cinematic ();
          update_frame_flare ();
          return;
        }
      }
      if (m_opening.active ()) {
        if (input.leave_cinematic) {
          leave_cinematic ();
          input = {};
        } else {
          m_opening.tick (dt);
          follow_opening_clock ();
          if (!m_opening.active ()) {
            leave_cinematic ();
            // A capture of the opening is done when it is.
            if (moppe::environment ("MOPPE_CINEMATIC_CAPTURE_DIR"))
              platform::request_quit ();
          }
          update_frame_flare ();
          return;
        }
      }

      // Screenshot autopilot for headless verification: rides in a
      // lazy arc with periodic boost-assisted leaps.
      static const bool demo = moppe::environment ("MOPPE_DEMO") != 0;
      static const bool demo_glides = [] {
        const char* name = moppe::environment ("MOPPE_DEMO");
        return name && std::string_view (name) == "glide";
      }();
      static const bool demo_wheelies = [] {
        const char* name = moppe::environment ("MOPPE_DEMO");
        return name && std::string_view (name) == "wheelie";
      }();
      m_trunk_field.focus (session ().subject_position ());
      static const bool orbit = moppe::environment ("MOPPE_ORBIT") != 0 ||
                                moppe::environment ("MOPPE_PAN") != 0;
      if (m_spectator) {
        // The mouse turns the spectator directly; the right stick's look
        // arrives with the frame.
        m_spectator->yaw += input.look_yaw;
        m_spectator->pitch =
          std::clamp (m_spectator->pitch + input.look_pitch, -1.45f, 1.45f);
        input = {};
      }
      static const char* walk_script = moppe::environment ("MOPPE_WALK");
      // The bump script drives the jeep too, and boat-fish the boat.
      if (walk_script && !orbit &&
          (logic ().m_mode == M_FOOT ||
           std::string_view (walk_script) == "bump" ||
           std::string_view (walk_script) == "boat-fish"))
        input = scripted_walk (walk_script, dt);
      else if (demo && !m_water_inspection && !orbit) {
        // The autopilot rides; the player may still look around.
        const float look_yaw = input.look_yaw;
        const float look_pitch = input.look_pitch;
        if (logic ().m_mode == M_HELICOPTER) {
          // Spool up, climb out, hook what is under it, and cruise in
          // long easy curves.
          const float t = total_time;
          input = {
            .turn = t > 9.0f ? 0.45f * std::sin ((t - 9.0f) * 0.22f) : 0.0f,
            .drive = t > 8.0f ? 1.0f : 0.0f,
            .boost = t > 4.0f && t < 9.0f ? 1.0f : 0.0f,
            .toggle_mount = !logic ().m_sling.hooked &&
                            session ().hook_within_reach ().has_value (),
            .look_yaw = look_yaw,
            .look_pitch = look_pitch,
          };
        } else if (demo_wheelies && logic ().m_mode == M_BIKE) {
          // MOPPE_DEMO=wheelie: rolling for a few seconds of every
          // twelve, then up on the back wheel and held there as a rider
          // at the keyboard holds it, the throttle all or nothing --
          // open while the nose is low or falling, shut as it rises, and
          // a dab of the rear brake if it gets too high -- judged by
          // where the pitch is heading a moment on.
          const mov::Vehicle& bike = session ().bike ();
          const float pitch = bike.pitch ();
          const float rate = dt > 0.0f ? (pitch - m_demo_pitch) / dt : 0.0f;
          m_demo_pitch = pitch;
          const float ahead = pitch + 0.4f * rate;
          const bool up = std::fmod (total_time, 12.0f) > 3.0f;
          input = {
            .turn = 0.2f * std::sin (total_time * 0.25f),
            .drive = !up            ? 0.6f
                     : ahead < 0.7f ? 1.0f
                                    : 0.0f,
            .lean_back = up,
            .look_yaw = look_yaw,
            .look_pitch = look_pitch,
            .run = up && ahead > 1.1f,
          };
        } else if (logic ().m_mode == M_BOAT || logic ().m_mode == M_JET_SKI) {
          input = logic ().m_mode == M_BOAT ? boat_autopilot (total_time)
                                            : jet_ski_autopilot (total_time);
          input.look_yaw = look_yaw;
          input.look_pitch = look_pitch;
        } else {
          input = {
            .turn = 0.35f * std::sin (total_time * 0.25f),
            .drive = 1.0f,
            .boost =
              logic ().m_mode != M_TRACTOR && logic ().m_mode != M_CADDY &&
                  logic ().m_mode != M_BOAT &&
                  std::fmod (total_time, 11.0f) < (demo_glides ? 3.5f : 1.35f)
                ? 1.0f
                : 0.0f,
            // MOPPE_DEMO=glide opens the wing on the first leap high
            // enough to allow it.
            .deploy_glider_held = demo_glides,
            .look_yaw = look_yaw,
            .look_pitch = look_pitch,
          };
          // Look a few metres ahead; where a trunk stands in the way, steer
          // hard toward the side its contact pushes, so a ride through the
          // woods weaves between trees instead of stopping at the first.
          const Vec3 at = session ().subject_position ();
          const Vec3 heading = session ().subject_heading ();
          const Vec3 right = normalized (cross (heading, Vec3 (0, 1, 0)));
          for (const float ahead : { 4.0f, 9.0f }) {
            const Vec3 probe = at + heading * ahead;
            const mov::TrunkContact contact =
              m_trunk_field.collide (probe, probe + Vec3 (0, 1.5f, 0), 1.8f);
            if (contact.hit) {
              input.turn = dot (contact.normal, right) >= 0.0f ? 1.0f : -1.0f;
              break;
            }
          }
        }
      }

      pass_time (dt);
      ride_along (input);
      go_fishing (input, dt);
      cook_supper (input);
      tend_camp (input, dt);
      reach_for_mushroom (input);
      const GameSessionAdvanceResult advance =
        advance_game_session (world (),
                              surface (),
                              session (),
                              input,
                              seconds (dt),
                              &m_trunk_field,
                              ground_cover (session ().subject_position ()),
                              &generated_world ().water_surface (),
                              m_strangers);
      if (advance.say_ouchies)
        platform::say ("Ouchies. That hurts.");
      tend_animals (dt);

      if (m_water_inspection) {
        session ().camera ().place (m_water_inspection->eye,
                                    m_water_inspection->target);
        session ().camera ().keep_above (surface ());
      }
      orbit_camera ();
      spectator_camera (dt);
      rest_camera ();
      walk_side_camera ();
      ride_capture_camera (dt);
      animal_camera (dt);

      if (m_benchmark)
        finish_benchmark_frame (m_benchmark_replay->finish_frame ());
      update_frame_flare ();
    }

    std::optional<double> MoppeGame::script_seconds () const {
      if (!m_video_plan)
        return std::nullopt;
      return m_ready && !opening_active () ? m_video_render_frame / 60.0 : -1.0;
    }

    void MoppeGame::controls (const platform::ControlState& state) {
      if (!m_ready || logic ().m_game_over)
        return;
      m_live_input.controls (state);
    }

    void MoppeGame::key (platform::Key k, bool down) {
      using platform::Key;

      if (!m_ready) {
        if (k == Key::Space && down)
          m_skip_cinematic_requested = true;
        else if (k == Key::Escape && down)
          platform::request_quit ();
        return;
      }

      // In great pain, only R (ride again) and ESC work.
      if (logic ().m_game_over) {
        if ((k == Key::R || k == Key::Restart) && down)
          revive ();
        else if (k == Key::Escape && down)
          platform::request_quit ();
        return;
      }

      if (k == Key::Q && down && logic ().m_mode == M_FOOT &&
          !logic ().m_camp.resting) {
        start_scan (0.0);
        return;
      }

      if (k == Key::H && down) {
        m_hud.set_diagnostics (!m_hud.diagnostics ());
        return;
      }

      if (k == Key::G && down) {
        m_graphics.terrain_topology = !m_graphics.terrain_topology;
        m_renderer->set_terrain_topology_overlay (m_graphics.terrain_topology);
        std::cerr << "moppe: terrain vertex grid "
                  << (m_graphics.terrain_topology ? "on" : "off") << '\n';
        return;
      }

      if (k == Key::Screenshot && down) {
        m_snapshot_requested = true;
        return;
      }

      // V starts a video of what is on screen, and V again ends it.
      if (k == Key::Record && down) {
        if (m_video.recording ())
          m_video.finish ();
        else if (!m_video_plan)
          m_video.begin (next_clip_path (), 30);
        return;
      }
      // Esc in play asks whether to quit: Esc again quits, and any
      // other key goes back to the game (its releases still count, so
      // nothing stays held).
      if (m_quit_asked && down) {
        m_quit_asked = false;
        if (k == Key::Escape) {
          m_video.finish ();
          platform::request_quit ();
        }
        return;
      }

      if (m_spectator) {
        // L lets the mouse loose -- to start a screen recording, say --
        // and takes it back.
        if (k == Key::L && down) {
          m_spectator->pointer_free = !m_spectator->pointer_free;
          platform::set_pointer_captured (!m_spectator->pointer_free);
        }
        if (down)
          m_spectator->held.insert (k);
        else
          m_spectator->held.erase (k);
        if (k == Key::Escape && down) {
          m_video.finish ();
          platform::request_quit ();
        }
        return;
      }
      if (opening_active ()) {
        if (k == Key::Escape && down) {
          m_video.finish ();
          platform::request_quit ();
        } else
          m_live_input.cinematic_key (k, down);
        return;
      }

      if (k == Key::L && down) {
        m_pointer_free = !m_pointer_free;
        platform::set_pointer_captured (!m_pointer_free);
        return;
      }

      // M unfolds the map, looks closer, and folds it away; Esc folds
      // it away too, and with it folded asks whether to quit.
      if (k == Key::M && down && m_ready) {
        m_map.turn ();
        return;
      }
      if (k == Key::Escape && down) {
        if (m_map.open ())
          m_map.show (MapSheet::View::Closed);
        else
          m_quit_asked = true;
        return;
      }

      if (k == Key::N && down && m_ready) {
        regenerate_world ();
        return;
      }

      m_live_input.key (k, down);
    }

    int MoppeGame::glide_frame_limit () {
      static const int frames = [] {
        if (!moppe::environment ("MOPPE_GLIDE"))
          return 0;
        const char* count = moppe::environment ("MOPPE_GLIDE_FRAMES");
        return count ? std::max (2, ::atoi (count)) : 120;
      }();
      return frames;
    }

    float MoppeGame::glide_speed_mps () {
      static const float speed = [] {
        const char* value = moppe::environment ("MOPPE_GLIDE_SPEED");
        return value ? std::max (0.1f, (float)::atof (value)) : 12.0f;
      }();
      return speed;
    }

    float MoppeGame::glide_vertical_speed_mps () {
      static const float speed = [] {
        const char* value = moppe::environment ("MOPPE_GLIDE_VERTICAL_SPEED");
        return value ? (float)::atof (value) : 0.0f;
      }();
      return speed;
    }

    const GazetteerShot* MoppeGame::current_gazetteer_shot () const noexcept {
      if (!m_gazetteer || m_gazetteer_shot >= m_gazetteer_plan.shots.size ())
        return nullptr;
      return &m_gazetteer_plan.shots[m_gazetteer_shot];
    }

    FrameViewInput MoppeGame::frame_view_input (float aspect) const {
      FrameSceneMode scene = FrameSceneMode::Gameplay;
      FrameCameraReading camera;
      const bool cinematic = opening_active ();

      if (const GazetteerShot* shot = current_gazetteer_shot ()) {
        scene = FrameSceneMode::Gazetteer;
        Vec3 eye = position_value (shot->eye);
        Vec3 subject = position_value (shot->subject);
        // A glide translates the frozen shot's camera along its own
        // horizontal heading, one render frame at a time, so consecutive
        // captures differ by camera motion and nothing else.
        if (glide_frame_limit () > 0) {
          Vec3 heading = subject - eye;
          heading[1] = 0.0f;
          const Vec3 step = normalized (heading) * (glide_speed_mps () / 60.0f);
          const Vec3 vertical_step =
            Vec3 (0, glide_vertical_speed_mps () / 60.0f, 0);
          const Vec3 travel =
            (step + vertical_step) * static_cast<float> (m_glide_frame);
          eye += travel;
          subject += travel;
        }
        camera = {
          .position = eye,
          .forward = normalized (subject - eye),
          .view = Mat4::look_at (eye, subject, Vec3 (0, 1, 0)),
          .field_of_view = shot->vertical_field_of_view,
        };
      } else if (m_opening.active ()) {
        scene = FrameSceneMode::Cinematic;
        camera = {
          .position = m_opening.position (),
          .forward = m_opening.forward (),
          .view = m_opening.view_matrix (),
          .field_of_view = m_opening.field_of_view () * u::deg,
        };
      } else if (cinematic) {
        scene = FrameSceneMode::Cinematic;
        camera = {
          .position = m_cinematic.position (),
          .forward = m_cinematic.forward (),
          .view = m_cinematic.view_matrix (),
          .field_of_view = m_cinematic.field_of_view () * u::deg,
        };
      } else {
        if (m_water_inspection)
          scene = FrameSceneMode::WaterInspection;
        camera = {
          .position = session ().camera ().position (),
          .forward = session ().camera ().forward (),
          .view = session ().camera ().view_matrix (),
          .field_of_view = 70.0f * u::deg,
        };
      }

      FrameBenchmarkTag benchmark { .mask = m_benchmark_mask };
      if (m_benchmark_render_frame) {
        benchmark.partition_mask = m_benchmark_render_frame->partition_mask;
        benchmark.epoch = m_benchmark_render_frame->epoch;
        benchmark.logical_frame = m_benchmark_render_frame->logical_frame;
        benchmark.measured = m_benchmark_render_frame->measured;
      }

      return {
        .world = world (),
        .surface = surface (),
        .session = session (),
        .graphics = m_graphics,
        .selected_camera = camera,
        .scene = scene,
        .aspect = aspect,
        .cinematic_motion_blur =
          m_cinematic.active () ? m_cinematic.motion_blur () : 0.0f,
        .cinematic_elapsed = m_opening.active () ? m_opening.elapsed ()
                             : cinematic         ? m_cinematic.elapsed ()
                                                 : 0.0f,
        .reveal_player = m_opening.active () && !m_opening.shot ().settle,
        .capture_field_of_view =
          m_film_fov && !cinematic && !m_opening.active ()
            ? std::optional<degrees_t> (*m_film_fov * u::deg)
            : std::nullopt,
        .benchmark = benchmark,
        .sky = m_sky,
        .water = &generated_world ().water_surface (),
      };
    }

    void MoppeGame::update_frame_flare () {
      const FrameView frame = compose_frame_view (frame_view_input (1.0f));
      const float target = sun_visibility_target (frame, world (), surface ());
      logic ().m_flare += (target - logic ().m_flare) * 0.12f;
    }

    void MoppeGame::prepare_benchmark_epoch () {
      if (!m_benchmark_epoch_pending)
        return;
      const std::optional<GraphicsBenchmarkReplay::Frame> frame =
        m_benchmark_replay->current_frame ();
      if (!frame || frame->prelude || !m_benchmark_checkpoint)
        throw std::logic_error ("graphics benchmark lost its checkpoint");

      // The first epoch restores too: a restore rebuilds the rigid bike's
      // physics world without the solver's warm-start history, so only a
      // restored session replays exactly like every other epoch.
      session ().restore (*m_benchmark_checkpoint);
      m_renderer->reset_temporal_state ();
      m_graphics = m_benchmark_baseline;
      m_benchmark_mask = apply_graphics_benchmark_mask (
        m_graphics, frame->partition_mask, m_benchmark->partition);
      update_benchmark_title (frame->epoch, frame->partition_mask);
      m_benchmark_epoch_pending = false;
    }

    void MoppeGame::finish_benchmark_frame (
      GraphicsBenchmarkReplay::Boundary boundary) {
      switch (boundary) {
      case GraphicsBenchmarkReplay::Boundary::none:
        return;
      case GraphicsBenchmarkReplay::Boundary::prelude_complete:
        m_benchmark_checkpoint = session ().state ();
        m_benchmark_epoch_pending = true;
        std::cerr << "moppe: graphics benchmark: "
                  << m_benchmark_replay->configuration_count ()
                  << " configurations, " << m_benchmark->settle_frames
                  << " settle + " << m_benchmark->measured_frames
                  << " measured frames each\n";
        return;
      case GraphicsBenchmarkReplay::Boundary::epoch_complete:
        m_benchmark_epoch_pending = true;
        return;
      case GraphicsBenchmarkReplay::Boundary::complete:
        m_benchmark_submitted = true;
        platform::set_window_title ("Moppe benchmark - finishing GPU samples");
        return;
      }
    }

    void MoppeGame::update_benchmark_title (int epoch,
                                            uint32_t partition_mask) const {
      if (!m_benchmark)
        return;
      const int configurations =
        1 << graphics_benchmark_dimension_count (m_benchmark->partition);
      std::ostringstream title;
      title << "Moppe benchmark " << (epoch + 1) << '/' << configurations
            << " - ";
      bool any = false;
      for (int bit = 0;
           bit < graphics_benchmark_dimension_count (m_benchmark->partition);
           ++bit)
        if (partition_mask & (1u << bit)) {
          if (any)
            title << " + ";
          title << graphics_benchmark_block_name (m_benchmark->partition, bit);
          any = true;
        }
      if (!any)
        title << "none";
      platform::set_window_title (title.str ());
    }

    void MoppeGame::leave_cinematic () {
      m_cinematic.stop ();
      m_opening.stop ();
      m_live_input.clear ();
      // The opening has already arrived where play takes up the view.
      session ().settle_camera (surface ());
    }

    OpeningShot MoppeGame::arrival () {
      session ().settle_camera (surface ());
      const ChaseCamera& camera = session ().camera ();
      const float fov = logic ().m_cam_mode == CAM_HELMET
                          ? 70.0f
                          : CHASE_FIELD_OF_VIEW.numerical_value_in (u::deg);
      return arrival_shot (
        m_opening_arrival, camera.position (), camera.forward (), fov);
    }

    void MoppeGame::regenerate_world () {
      session ().clear_controls ();
      m_live_input.clear ();
      m_ready = false;
      m_skip_cinematic_requested = false;
      m_cinematic.stop ();
      m_cinematic_plan = {};
      m_opening.stop ();
      m_opening_shots.clear ();
      m_waterfall_surface.clear ();
      m_bridges.clear ();
      m_water_inspection.reset ();
      const terrain::Seed next_seed = terrain::next_seed (recipe ().seed ());
      terrain::WorldRecipe next_recipe = terrain::make_world_recipe (
        recipe ().extent (),
        recipe ().resolution (),
        next_seed,
        recipe ().water_datum (),
        recipe ().generation_profile (),
        recipe ().evolution ().uplift_duration,
        recipe ().evolution ().channel_initiation_area,
        recipe ().evolution ().fluvial_transport.concentration_at_unit_slope,
        recipe ().evolution ().critical_hillslope_gradient,
        recipe ().evolution ().maximum_hillslope_diffusivity_multiplier);
      session ().set_mode (M_BIKE);
      logic ().m_game_over = false;
      logic ().m_health = 100.0f;
      m_params = bind_world_params (m_params, next_recipe);
      m_recipe = next_recipe;
      m_loading.start (world (), std::move (next_recipe));
    }

    void MoppeGame::revive () {
      logic ().m_lives = 10;
      logic ().m_health = 100.0f;
      logic ().m_shake = 0.0f;
      logic ().m_shake_time = 0.0f;
      logic ().m_jump_airtime = 0.0f;
      logic ().m_jump_spin_radians = 0.0f;
      logic ().m_jump_peak_spin_radians = 0.0f;
      logic ().m_landed_age = 10.0f;
      logic ().m_wheelie_m = 0.0f;
      session ().set_mode (M_BIKE);
      // Back to the start, but ON the ground rather than 600 m
      // over it.
      const float ground =
        spatial::sample<terrain::surface_elevation> (
          surface (),
          position (Vec3 (m_spawn_position[0], 0, m_spawn_position[2])))
          .quantity_from_zero ()
          .numerical_value_in (u::m);
      session ().bike ().reset (
        Vec3 (m_spawn_position[0], ground + 1.2f, m_spawn_position[2]));
      // Key releases were swallowed during the game-over screen;
      // don't resume with the throttle stuck open.
      m_live_input.clear ();
      session ().clear_controls ();
      logic ().m_game_over = false;
    }

    std::unique_ptr<platform::Game>
    make_moppe_game (const LaunchOptions& options,
                     terrain::WorldRecipe recipe) {
      return std::make_unique<MoppeGame> (options, std::move (recipe));
    }
  }
}
