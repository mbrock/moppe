// The game shell's construction, its input, and the simulation step: what
// each tick asks of the world, the session, and the scripts and captures
// driving them. The class is in game.hh; the rest of its members live in the
// game_*.cc files beside this one.

#include <moppe/environment.hh>
#include <moppe/game/game.hh>
#include <moppe/profile.hh>

namespace moppe {
  namespace game {
    MoppeGame::MoppeGame (const LaunchOptions& options,
                          terrain::WorldRecipe recipe)
        : m_params (bind_world_params (options.world, recipe)),
          m_recipe (std::move (recipe)),
          m_loading (this->recipe (), options.world_cache),
          m_graphics (options.graphics), m_sky (m_graphics.sun_height),
          m_spawn_position (position_value (this->world ().spawn_position ())),
          m_renderer (0), m_capture (options.screenshot_path), m_ready (false) {
      if (options.multiplayer)
        m_company.seek (*options.multiplayer);
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
        m_heads_up.hud.load (r);
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

    void MoppeGame::pointer_move (platform::view_points_t,
                                  platform::view_points_t,
                                  platform::view_points_t dx,
                                  platform::view_points_t dy) {
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
      if (m_capture.video_planned ()) {
        m_simulation_clock.reset ();
        // The time a video lets pass before it begins is not looked at,
        // so it goes by many steps to a drawn frame, until two seconds
        // before the first frame taken, which gives the picture's
        // history time to settle.
        // A remote control's taps are timed by drawn frames, so a
        // driven video keeps to one step each.
        const int steps = m_capture.video_steps (
          moppe::environment ("MOPPE_CONTROL_FILE") != nullptr);
        for (int step = 0; step < steps; ++step)
          tick_simulation (seconds (1.0f / 60.0f));
        m_capture.skip_video_frames (steps - 1);
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
        logic ().m_frame_time = elapsed * u::s;
    }

    void MoppeGame::tick_simulation (seconds_t interval) {
      float dt = seconds_value (interval);
      MOPPE_PROFILE_ZONE ("MoppeGame::tick_simulation");
      if (!m_ready)
        return;
      logic ().m_frame_time = dt * u::s;
      if (logic ().m_game_over)
        return;

      InputFrame input = m_live_input.take_frame (dt);

      // The companion's pose clock counts only time the world was stepped
      // by, at the step it was stepped by.
      m_company.stepped += dt;
      logic ().m_total_time += dt * si::second;
      const float total_time =
        static_cast<float> (seconds_into_play (logic ().m_total_time));
      m_sky.update (logic (), total_time);

      const OpeningDirector::Step opening =
        m_opening.step (input, seconds (dt), session (), surface (), m_sky);
      if (opening.left)
        leave_cinematic ();
      if (opening.played) {
        update_frame_flare ();
        return;
      }

      m_trunk_field.focus (session ().subject_position ());
      static const char* walk_script = moppe::environment ("MOPPE_WALK");
      // A walk script drives (the bump script drives the jeep too, and
      // boat-fish the boat), or else the demo autopilot (demo_pilot.hh).
      if (walk_script && (logic ().m_doing == Doing::on_foot () ||
                          std::string_view (walk_script) == "bump" ||
                          std::string_view (walk_script) == "boat-fish"))
        input = m_walk.step (script_stage (), walk_script, seconds (dt));
      else if (DemoPilot::active ())
        input = m_demo.steer (
          input, session (), m_parking, world_ground (), total_time, dt);
      m_company.ride_along (session (), input);
      m_company.keep_time (session ());
      // The ground shows what the basket holds, a restored one too.
      m_mushrooms.follow (logic ().m_basket);
      const AnimalGround animals = m_places.animal_ground (world_ground ());
      const Surroundings around = surroundings (&animals);
      // Ours bump into the other player's machines and into the buses.
      std::vector<MachineBody> strangers = m_company.strangers;
      for (const BusPose& bus : m_places.bus.where (bus_time (logic ())))
        strangers.push_back (bus_body (bus,
                                       session ().subject_position (),
                                       m_places.layout.period_x,
                                       m_places.layout.period_z));
      const GameSessionAdvanceResult advance =
        advance_game_session (world (),
                              surface (),
                              session (),
                              input,
                              seconds (dt),
                              &m_trunk_field,
                              ground_cover (session ().subject_position ()),
                              &generated_world ().water_surface (),
                              strangers,
                              &around);
      if (advance.say_ouchies)
        platform::say ("Ouchies. That hurts.");

      m_capture_camera.override_camera (
        session (), world_ground (), seconds (dt));

      update_frame_flare ();
    }

    std::optional<platform::host_time_t> MoppeGame::script_seconds () const {
      return m_capture.script_seconds (m_ready && !m_opening.active ());
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

      if (k == Key::Q && down && logic ().m_doing == Doing::on_foot () &&
          !logic ().m_camp.resting) {
        start_scan (0.0);
        return;
      }

      if (k == Key::H && down) {
        m_heads_up.hud.set_diagnostics (!m_heads_up.hud.diagnostics ());
        return;
      }

      if (k == Key::Screenshot && down) {
        m_capture.request_snapshot ();
        return;
      }

      // V starts a video of what is on screen, and V again ends it.
      if (k == Key::Record && down) {
        m_capture.toggle_clip ();
        return;
      }
      // Esc in play asks whether to quit: Esc again quits, and any
      // other key goes back to the game (its releases still count, so
      // nothing stays held).
      if (m_heads_up.quit_asked && down) {
        m_heads_up.quit_asked = false;
        if (k == Key::Escape) {
          m_capture.finish_video ();
          platform::request_quit ();
        }
        return;
      }

      if (m_opening.active ()) {
        if (k == Key::Escape && down) {
          m_capture.finish_video ();
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
        m_heads_up.map.turn ();
        return;
      }
      if (k == Key::Escape && down) {
        if (m_heads_up.map.open ())
          m_heads_up.map.show (MapSheet::View::Closed);
        else
          m_heads_up.quit_asked = true;
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
      FrameCameraReading camera = {
        .position = session ().camera ().position (),
        .forward = session ().camera ().forward (),
        .view = session ().camera ().view_matrix (),
        .field_of_view = 70.0f * u::deg,
      };
      const bool cinematic = m_opening.active ();
      if (const std::optional<FrameCameraReading> shot = m_opening.camera ()) {
        scene = FrameSceneMode::Cinematic;
        camera = *shot;
      }

      return {
        .world = world (),
        .surface = surface (),
        .session = session (),
        .graphics = m_graphics,
        .selected_camera = camera,
        .scene = scene,
        .aspect = aspect,
        .cinematic_motion_blur = m_opening.motion_blur (),
        .cinematic_elapsed = m_opening.elapsed (),
        .reveal_player = m_opening.reveal_player (),
        .capture_field_of_view =
          m_capture_camera.lens () && !cinematic
            ? std::optional<degrees_t> (*m_capture_camera.lens () * u::deg)
            : std::nullopt,
        .sky = m_sky.sky (),
        .water = &generated_world ().water_surface (),
      };
    }

    void MoppeGame::update_frame_flare () {
      const FrameView frame = compose_frame_view (frame_view_input (1.0f));
      const float target = sun_visibility_target (frame, world (), surface ());
      logic ().m_flare += (target * proportion[one] - logic ().m_flare) * 0.12f;
    }

    void MoppeGame::leave_cinematic () {
      m_opening.stop ();
      m_live_input.clear ();
      // The opening has already arrived where play takes up the view.
      session ().settle_camera (surface ());
    }

    void MoppeGame::regenerate_world () {
      session ().clear_controls ();
      m_live_input.clear ();
      m_ready = false;
      m_skip_cinematic_requested = false;
      m_opening.forget ();
      m_waterfall_surface.clear ();
      m_bridges.clear ();
      const terrain::Seed next_seed = terrain::next_seed (recipe ().seed ());
      terrain::WorldRecipe next_recipe =
        terrain::make_world_recipe (recipe ().extent (),
                                    recipe ().resolution (),
                                    next_seed,
                                    recipe ().water_datum (),
                                    recipe ().generation_profile ());
      session ().set_mode (Doing::driving (Machine::bike));
      logic ().m_game_over = false;
      logic ().m_health = 100.0f * health[one];
      m_params = bind_world_params (m_params, next_recipe);
      m_recipe = next_recipe;
      m_loading.start (world (), std::move (next_recipe));
    }

    void MoppeGame::revive () {
      logic ().m_lives = 10;
      logic ().m_health = 100.0f * health[one];
      logic ().m_shake = 0.0f * u::deg;
      logic ().m_shake_time = 0.0f * u::s;
      logic ().m_jump_airtime = 0.0f * u::s;
      logic ().m_jump_spin_radians = 0.0f * u::rad;
      logic ().m_jump_peak_spin_radians = 0.0f * u::rad;
      logic ().m_landed_age = 10.0f * u::s;
      logic ().m_wheelie_m = 0.0f * u::m;
      session ().set_mode (Doing::driving (Machine::bike));
      // Back to the start, but ON the ground rather than 600 m
      // over it.
      const float ground =
        spatial::sample<terrain::surface_elevation> (
          surface (),
          position (Vec3 (m_spawn_position[0], 0, m_spawn_position[2])))
          .delta_from_zero ()
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
