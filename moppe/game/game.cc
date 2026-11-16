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
          m_screenshot_frames (0), m_ready (false) {
      m_day = plan_day ();
      m_sky = m_almanac.held (m_graphics.sun_height);
      if (const char* name = moppe::environment ("MOPPE_WEATHER");
          name && std::string_view (name) != "changing") {
        m_held_sky = sky_kind_named (name);
        if (!m_held_sky)
          std::cerr << "moppe: weather: no kind named " << name
                    << "; the weather changes" << std::endl;
      }
      if (options.multiplayer)
        seek_company (*options.multiplayer);
    }

    void MoppeGame::setup (render::Renderer& r, int, int) {
      MOPPE_PROFILE_ZONE ("MoppeGame::setup");
      m_renderer = &r;
      std::cerr << "moppe: simulation: fixed-step=120 Hz, catch-up-limit="
                << MAX_SIMULATION_CATCH_UP_STEPS << " steps" << std::endl;

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
      if (m_ready && !m_pointer_free)
        m_live_input.look (dx, dy);
    }

    void MoppeGame::tick (seconds_t interval) {
      const float elapsed = seconds_value (interval);
      if (!m_ready) {
        m_simulation_clock.reset ();
        return;
      }
      keep_company (interval);

      // A video advances exactly one 60 Hz frame of world time per rendered
      // frame, so recorded motion is even however slowly the frames are
      // written.
      if (m_video_plan) {
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
          tick_simulation (seconds (1.0f / 60.0f));
        m_video_render_frame += steps - 1;
        return;
      }

      const int steps = m_simulation_clock.consume (elapsed);
      MOPPE_PROFILE_PLOT ("simulation.steps", steps);
      for (int step = 0; step < steps; ++step)
        tick_simulation (
          seconds (static_cast<float> (FIXED_SIMULATION_STEP_SECONDS)));

      // The HUD reports rendered-frame cadence, not the internal 120 Hz
      // integration frequency.
      if (m_ready)
        logic ().m_frame_time = elapsed;
    }

    void MoppeGame::tick_simulation (seconds_t interval) {
      float dt = seconds_value (interval);
      MOPPE_PROFILE_ZONE ("MoppeGame::tick_simulation");
      if (!m_ready)
        return;
      logic ().m_frame_time = dt;
      if (logic ().m_game_over)
        return;

      InputFrame input = m_live_input.take_frame (dt);

      // The companion's pose clock counts only time the world was stepped
      // by, at the step it was stepped by.
      m_stepped += dt;
      logic ().m_total_time += dt * si::second;
      const float total_time =
        static_cast<float> (seconds_into_play (logic ().m_total_time));
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
          m_cinematic.tick (seconds (dt), surface (), controls);
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
          m_opening.tick (seconds (dt));
          follow_opening_clock ();
          if (!m_opening.active ())
            leave_cinematic ();
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
      static const char* walk_script = moppe::environment ("MOPPE_WALK");
      // The bump script drives the jeep too, and boat-fish the boat.
      if (walk_script && (logic ().m_mode == M_FOOT ||
                          std::string_view (walk_script) == "bump" ||
                          std::string_view (walk_script) == "boat-fish"))
        input = scripted_walk (walk_script, seconds (dt));
      else if (demo) {
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
          const float pitch = radians_value (bike.pitch ());
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

      ride_along (input);
      // The ground shows what the basket holds, a restored one too.
      m_mushrooms.follow (logic ().m_basket);
      const AnimalGround animals = animal_ground ();
      const Surroundings around = surroundings (&animals);
      const GameSessionAdvanceResult advance =
        advance_game_session (world (),
                              surface (),
                              session (),
                              input,
                              seconds (dt),
                              &m_trunk_field,
                              ground_cover (session ().subject_position ()),
                              &generated_world ().water_surface (),
                              m_strangers,
                              &around);
      if (advance.say_ouchies)
        platform::say ("Ouchies. That hurts.");

      rest_camera ();
      walk_side_camera ();
      ride_capture_camera (seconds (dt));
      animal_camera (seconds (dt));

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

    FrameViewInput MoppeGame::frame_view_input (float aspect) const {
      FrameSceneMode scene = FrameSceneMode::Gameplay;
      FrameCameraReading camera;
      const bool cinematic = opening_active ();

      if (m_opening.active ()) {
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
          .field_of_view = m_cinematic.field_of_view (),
        };
      } else {
        camera = {
          .position = session ().camera ().position (),
          .forward = session ().camera ().forward (),
          .view = session ().camera ().view_matrix (),
          .field_of_view = 70.0f * u::deg,
        };
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
                             : cinematic
                               ? seconds_value (m_cinematic.elapsed ())
                               : 0.0f,
        .reveal_player = m_opening.active () && !m_opening.shot ().settle,
        .capture_field_of_view =
          m_film_fov && !cinematic && !m_opening.active ()
            ? std::optional<degrees_t> (*m_film_fov * u::deg)
            : std::nullopt,
        .sky = m_sky,
        .water = &generated_world ().water_surface (),
      };
    }

    void MoppeGame::update_frame_flare () {
      const FrameView frame = compose_frame_view (frame_view_input (1.0f));
      const float target = sun_visibility_target (frame, world (), surface ());
      logic ().m_flare += (target - logic ().m_flare) * 0.12f;
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
      const terrain::Seed next_seed = terrain::next_seed (recipe ().seed ());
      terrain::WorldRecipe next_recipe =
        terrain::make_world_recipe (recipe ().extent (),
                                    recipe ().resolution (),
                                    next_seed,
                                    recipe ().water_datum (),
                                    recipe ().generation_profile ());
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
