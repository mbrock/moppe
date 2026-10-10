#include <moppe/game/game_session.hh>

#include <algorithm>
#include <cmath>
#include <random>

namespace moppe::game {
  namespace {
    void sync_attached_bike (GameSession& session) {
      const mov::Glider& glider = session.glider ();
      const Vec3 up =
        Quaternion::rotate (Vec3 (0, 1, 0), glider.heading (), -glider.bank ());
      session.bike ().carry (glider.physical_position () - up * 2.4f * u::m,
                             glider.physical_velocity (),
                             glider.heading (),
                             up);
    }

    void set_turn (GameSession& session, float value) {
      GameLogicState& logic = session.logic ();
      logic.m_turn_input = value;
      // On foot the mouse turns; the steering keys step sideways.
      if (logic.m_mode == M_FOOT)
        session.walker ().set_strafe (value);
      else if (logic.m_mode == M_GLIDER)
        session.glider ().set_turn (value);
      else
        session.bike ().set_yaw ((90 * value) * u::deg);
    }

    void set_go (GameSession& session, float value) {
      GameLogicState& logic = session.logic ();
      logic.m_go_input = value;
      if (logic.m_mode == M_FOOT)
        session.walker ().set_walk (value > 0 ? value : value * 0.6f);
      else if (logic.m_mode == M_GLIDER)
        session.glider ().set_speed_control (value);
      else {
        session.bike ().set_thrust (value);
        session.bike ().set_boost (logic.m_boost_input, logic.m_go_input);
      }
    }

    void set_boost (GameSession& session, float value) {
      GameLogicState& logic = session.logic ();
      const float previous = logic.m_boost_input;
      logic.m_boost_input = std::max (0.0f, std::min (1.0f, value));
      if (logic.m_mode == M_FOOT) {
        if (logic.m_boost_input > 0.1f && previous <= 0.1f)
          session.walker ().jump ();
      } else if (logic.m_mode == M_GLIDER) {
        session.glider ().set_flare (logic.m_boost_input > 0.1f);
      } else {
        session.bike ().set_boost (logic.m_boost_input, logic.m_go_input);
      }
    }

    void deploy_glider (GameSession& session,
                        const map::SurfaceGeometry& terrain) {
      if (!session.can_deploy_glider (terrain))
        return;
      const Vec3 position = session.bike ().position ();
      const Vec3 heading = session.bike ().orientation ();
      const velocity_t inherited = session.bike ().physical_velocity ();
      session.bike ().set_thrust (0);
      session.bike ().set_yaw (0 * u::deg);
      session.bike ().set_boost (0, 0);
      session.glider ().launch (moppe::position (position + Vec3 (0, 2.4f, 0)),
                                inherited,
                                heading,
                                true);
      GameLogicState& logic = session.logic ();
      session.set_mode (M_GLIDER);
      session.glider ().set_turn (logic.m_turn_input);
      session.glider ().set_speed_control (logic.m_go_input);
      session.glider ().set_flare (logic.m_boost_input > 0.1f);
      sync_attached_bike (session);
    }

    void use_glider_control (GameSession& session,
                             const map::SurfaceGeometry& terrain) {
      if (session.logic ().m_mode == M_GLIDER) {
        session.glider ().drop_bike ();
        return;
      }
      deploy_glider (session, terrain);
    }

    void finish_glide (GameSession& session) {
      GameLogicState& logic = session.logic ();
      if (session.glider ().bike_attached ()) {
        sync_attached_bike (session);
        session.glider ().drop_bike ();
        session.set_mode (M_BIKE);
        set_turn (session, logic.m_turn_input);
        set_go (session, logic.m_go_input);
        set_boost (session, 0);
        return;
      }

      const Vec3 position = session.glider ().position ();
      session.walker ().spawn (moppe::position (position + Vec3 (0, 0.15f, 0)),
                               session.glider ().heading ());
      session.set_mode (M_FOOT);
      set_turn (session, logic.m_turn_input);
      set_go (session, logic.m_go_input);
      set_boost (session, 0);
    }

    void toggle_mount (GameSession& session) {
      GameLogicState& logic = session.logic ();
      if (logic.m_mode == M_GLIDER)
        return;

      if (logic.m_mode != M_FOOT) {
        // Step off to the side of the bike.
        mov::Vehicle& vehicle = session.bike ();
        const Vec3 heading = vehicle.orientation ();
        const Vec3 side (heading[2], 0, -heading[0]);
        session.walker ().spawn (
          moppe::position (vehicle.position () + side * 1.8f), heading);
        vehicle.set_thrust (0);
        vehicle.set_yaw (0 * u::deg);
        vehicle.set_boost (0, 0);
        session.set_mode (M_FOOT);
        set_turn (session, logic.m_turn_input);
        set_go (session, logic.m_go_input);
        return;
      }

      // On foot: remount the bike when standing next to it.
      if (length2 (session.walker ().position () -
                   session.bike ().position ()) < 5.0f * 5.0f) {
        session.bike ().set_thrust (0);
        session.bike ().set_yaw (0 * u::deg);
        session.set_mode (M_BIKE);
        set_turn (session, logic.m_turn_input);
        set_go (session, logic.m_go_input);
        set_boost (session, logic.m_boost_input);
      }
    }

    void apply_input_frame (GameSession& session,
                            const map::SurfaceGeometry& terrain,
                            const InputFrame& input) {
      set_turn (session, input_value (input.turn));
      set_go (session, input_value (input.drive));
      set_boost (session, input_value (input.boost));
      GameLogicState& logic = session.logic ();
      logic.m_look_pitch =
        std::clamp (logic.m_look_pitch + input.look_pitch, -1.35f, 1.35f);
      if (input.look_yaw != 0.0f || input.look_pitch != 0.0f)
        logic.m_look_idle = 0.0f;
      if (logic.m_mode == M_FOOT) {
        // The walker turns with the look, and a following camera turns
        // with them at once rather than catching up.
        session.walker ().turn_by (input.look_yaw);
        session.walker ().set_run (input.run);
        if (logic.m_cam_mode != CAM_HELMET)
          session.camera ().turn (input.look_yaw, 0.0f);
        logic.m_look_yaw = 0.0f;
      } else {
        // Riding, the look swings about the way ahead, all the way round.
        logic.m_look_yaw =
          std::remainder (logic.m_look_yaw + input.look_yaw, 6.2831853f);
      }

      if (input.deploy_glider)
        use_glider_control (session, terrain);
      else if (input.deploy_glider_held && session.logic ().m_mode != M_GLIDER)
        deploy_glider (session, terrain);
      if (input.toggle_mount) {
        if (session.can_deploy_glider (terrain) || session.can_drop_bike ())
          use_glider_control (session, terrain);
        else
          toggle_mount (session);
      }
      if (input.cycle_camera) {
        GameLogicState& logic = session.logic ();
        logic.m_cam_mode = (CamMode)((logic.m_cam_mode + 1) % 3);
        if (logic.m_cam_mode == CAM_HELMET)
          logic.m_fp_eye = session.camera ().position ();
      }
    }
  }

  GameSession::GameSession (const WorldParams& world,
                            const map::SurfaceGeometry& surface,
                            mov::BikePhysics physics)
      : m_bike (world.spawn_position (),
                45 * u::deg,
                surface,
                2600 * u::N,
                30 * u::kW,
                150 * u::kg,
                physics),
        m_glider (surface), m_camera (18 * u::deg, 6.5f * u::m) {}

  Vec3 GameSession::subject_position () const {
    if (m_logic.m_mode == M_FOOT)
      return m_walker.position ();
    if (m_logic.m_mode == M_GLIDER)
      return m_glider.position ();
    return m_bike.position ();
  }

  Vec3 GameSession::subject_heading () const {
    if (m_logic.m_mode == M_FOOT)
      return m_walker.heading ();
    if (m_logic.m_mode == M_GLIDER)
      return m_glider.heading ();
    return m_bike.orientation ();
  }

  float GameSession::subject_speed_kmh () const {
    if (m_logic.m_mode == M_FOOT)
      return 0.0f;
    if (m_logic.m_mode == M_GLIDER)
      return m_glider.airspeed ().numerical_value_in (u::m / u::s) * 3.6f;
    return length (m_bike.velocity ()) * 3.6f;
  }

  bool
  GameSession::can_deploy_glider (const map::SurfaceGeometry& terrain) const {
    if (m_logic.m_mode != M_BIKE || !m_bike.airborne ())
      return false;
    const Vec3 position = m_bike.position ();
    const float ground = terrain::surface_elevation_value (
      spatial::sample<terrain::surface_elevation> (
        terrain, moppe::position (Vec3 (position[0], 0.0f, position[2]))));
    return position[1] - ground > 3.0f;
  }

  bool GameSession::can_drop_bike () const {
    return m_logic.m_mode == M_GLIDER && m_glider.bike_attached ();
  }

  void GameSession::set_mode (Mode mode) {
    const bool was_on_foot = m_logic.m_mode == M_FOOT;
    const bool on_foot = mode == M_FOOT;
    m_logic.m_mode = mode;
    if (was_on_foot == on_foot)
      return;
    (was_on_foot ? m_logic.m_foot_cam : m_logic.m_ride_cam) =
      m_logic.m_cam_mode;
    m_logic.m_cam_mode = on_foot ? m_logic.m_foot_cam : m_logic.m_ride_cam;
    // Eyes open where the camera was and settle into the head, as when
    // Tab chooses them.
    if (m_logic.m_cam_mode == CAM_HELMET)
      m_logic.m_fp_eye = m_camera.position ();
  }

  void GameSession::start_on_foot () {
    if (m_logic.m_mode == M_BIKE)
      toggle_mount (*this);
    m_logic.m_cam_mode = CAM_HELMET;
    m_logic.m_look_pitch = 0.0f;
    m_logic.m_fp_eye = m_walker.eye_position ();
  }

  void GameSession::clear_controls () {
    set_turn (*this, 0.0f);
    set_go (*this, 0.0f);
    set_boost (*this, 0.0f);
  }

  GameSession::State GameSession::state () const {
    return { m_logic,           m_bike.state (),   m_glider.state (),
             m_walker.state (), m_camera.state (), m_stars.state (),
             m_dust.state () };
  }

  void GameSession::restore (const State& state) {
    m_logic = state.logic;
    m_bike.restore (state.vehicle);
    m_glider.restore (state.glider);
    m_walker.restore (state.walker);
    m_camera.restore (state.camera);
    m_stars.restore (state.stars);
    m_dust.restore (state.dust);
  }

  GameSessionAdvanceResult
  advance_game_session (const WorldParams& world,
                        const map::SurfaceGeometry& surface,
                        GameSession& session,
                        const InputFrame& input,
                        seconds_t dt,
                        const mov::TrunkField* trunks,
                        const GroundCover& ground) {
    const float elapsed = dt.numerical_value_in (u::s);
    GameLogicState& logic = session.logic ();
    session.bike ().set_water_level (world.water_level);
    session.bike ().set_trunks (trunks);

    apply_input_frame (session, surface, input);
    // Only a ridden bike moves under its own physics; one left standing is
    // parked until someone mounts it again.
    session.bike ().set_parked (logic.m_mode != M_BIKE &&
                                !session.can_drop_bike ());

    if (!session.can_drop_bike ())
      session.bike ().update (dt);
    if (logic.m_mode == M_GLIDER) {
      const bool landed = session.glider ().update (dt);
      if (session.glider ().bike_attached ())
        sync_attached_bike (session);
      if (landed)
        finish_glide (session);
    }
    if (logic.m_mode == M_FOOT)
      session.walker ().update (dt, surface, world, trunks);

    const Vec3 vehicle_position = session.subject_position ();
    mov::Vehicle& vehicle = session.bike ();

    // A parked bike's impacts shouldn't linger until remount.
    if (logic.m_mode != M_BIKE) {
      session.bike ().pop_impact ();
      session.bike ().pop_fall_drop ();
    }

    const bool in_water =
      vehicle_position[1] <
      (world.water_level).numerical_value_in (moppe::u::m) + 1.0f;
    const bool driving = logic.m_mode == M_BIKE;
    const float impact = driving ? vehicle.pop_impact () : 0.0f;

    // Air steering is a visible motocross whip and a scoring mechanic. The
    // shortest signed angle between successive headings survives wraparound.
    // Returning the bike to line unwinds the live angle, but the largest
    // committed angle stays banked so a proper whip-and-recover earns credit.
    bool clean_stunt_landing = false;
    int landed_trick_quarters = 0;
    if (driving && vehicle.airtime () > 0.0f) {
      Vec3 heading = vehicle.orientation ();
      heading[1] = 0.0f;
      if (length2 (heading) > 0.0001f)
        normalize (heading);
      else
        heading = logic.m_jump_last_heading;

      if (logic.m_jump_airtime > 0.0f) {
        Vec3 previous = logic.m_jump_last_heading;
        previous[1] = 0.0f;
        if (length2 (previous) > 0.0001f) {
          normalize (previous);
          const float sine = cross (previous, heading)[1];
          const float cosine =
            std::clamp (dot (previous, heading), -1.0f, 1.0f);
          logic.m_jump_spin_radians += std::atan2 (sine, cosine);
          logic.m_jump_peak_spin_radians =
            std::max (logic.m_jump_peak_spin_radians,
                      std::abs (logic.m_jump_spin_radians));
        }
      } else {
        logic.m_jump_spin_radians = 0.0f;
        logic.m_jump_peak_spin_radians = 0.0f;
      }
      logic.m_jump_last_heading = heading;
      logic.m_jump_airtime = vehicle.airtime ();
      logic.m_landed_age += elapsed;
    } else {
      if (driving && logic.m_jump_airtime >= 0.75f) {
        constexpr float pi = 3.14159265f;
        const float spin_degrees = logic.m_jump_peak_spin_radians * 180.0f / pi;
        landed_trick_quarters =
          (int)std::floor ((spin_degrees + 10.0f) / 90.0f);

        Vec3 horizontal_velocity = vehicle.velocity ();
        horizontal_velocity[1] = 0.0f;
        float alignment = 1.0f;
        if (length2 (horizontal_velocity) > 1.0f) {
          normalize (horizontal_velocity);
          alignment =
            std::abs (dot (horizontal_velocity, vehicle.orientation ()));
        }
        clean_stunt_landing = impact < 7.0f && alignment > 0.82f;

        const float air_points =
          60.0f * logic.m_jump_airtime * logic.m_jump_airtime;
        const float trick_points =
          125.0f * landed_trick_quarters * landed_trick_quarters;
        const float landing_multiplier = clean_stunt_landing ? 1.5f : 1.0f;
        logic.m_landed_airtime = logic.m_jump_airtime;
        logic.m_landed_spin_degrees = spin_degrees;
        logic.m_landed_clean = clean_stunt_landing;
        logic.m_landed_points =
          (int)std::round ((air_points + trick_points) * landing_multiplier);
        logic.m_score += logic.m_landed_points;
        logic.m_landed_age = 0.0f;

        // A clean stunt keeps the ride flowing: the better the whip, the more
        // jump-jet reserve comes back for the next launch.
        if (clean_stunt_landing)
          vehicle.replenish_boost (
            std::min (0.35f, 0.10f + 0.04f * landed_trick_quarters));
      }
      logic.m_jump_airtime = 0.0f;
      logic.m_jump_spin_radians = 0.0f;
      logic.m_jump_peak_spin_radians = 0.0f;
      logic.m_landed_age += elapsed;
    }
    const DisplayColor dust_color = ground.dust_color;
    const DisplayColor clod_color = ground.clod_color;
    // Loose ground gives up its dust; turf and litter hold most of theirs.
    const auto loose = [&ground] (float count) {
      return static_cast<int> (std::round (count * ground.dust));
    };
    const DisplayColor spray_color (0.85f, 0.92f, 1.0f);
    const Vec3 forward = session.subject_heading ();
    const Vec3 rear_wheel =
      vehicle_position - forward * 1.4f + Vec3 (0, -0.7f, 0);

    if (clean_stunt_landing && landed_trick_quarters > 0) {
      Dust::Style trick_flash;
      trick_flash.size = 0.22f * u::m;
      trick_flash.lifetime = 0.65f * u::s;
      trick_flash.downward_acceleration =
        5.0f * isq::acceleration[u::m / pow<2> (u::s)];
      trick_flash.spread = 1.2f * one;
      trick_flash.additive = true;
      session.dust ().emit (
        moppe::position (vehicle_position + Vec3 (0, -0.4f, 0)),
        velocity (vehicle.velocity () * 0.15f + Vec3 (0, 3, 0)),
        std::min (24, 8 + landed_trick_quarters * 4),
        DisplayColor (0.25f, 0.78f, 1.0f),
        trick_flash);
    }

    // Drift kicks up dirt from the rear wheel (or spray).
    if (driving && vehicle.grounded () && vehicle.drift_speed () > 6.0f) {
      const float count = std::min (4.0f, vehicle.drift_speed () * 0.2f);
      session.dust ().emit (moppe::position (rear_wheel),
                            velocity (vehicle.velocity () * 0.15f),
                            in_water ? (int)count : loose (count),
                            in_water ? spray_color : dust_color);
    }

    // Fallen leaves fly up behind the rear wheel and flutter back down.
    if (driving && vehicle.grounded () && !in_water && ground.leaves > 0.15f) {
      const float speed = length (vehicle.velocity ());
      const int count =
        static_cast<int> (std::min (3.0f, ground.leaves * speed * 0.08f));
      if (count > 0) {
        Dust::Style leaf;
        leaf.size = 0.08f * u::m;
        leaf.lifetime = 1.8f * u::s;
        leaf.downward_acceleration =
          1.6f * isq::acceleration[u::m / pow<2> (u::s)];
        leaf.spread = 1.1f * one;
        leaf.flake = true;
        const Vec3 lift = vehicle.velocity () * 0.25f + Vec3 (0, 2.4f, 0);
        session.dust ().emit (moppe::position (rear_wheel),
                              velocity (lift),
                              (count + 1) / 2,
                              DisplayColor (0.93f, 0.67f, 0.20f),
                              leaf);
        session.dust ().emit (moppe::position (rear_wheel),
                              velocity (lift),
                              count / 2,
                              DisplayColor (0.86f, 0.44f, 0.14f),
                              leaf);
      }
    }

    // Roost: hard throttle sprays an arc of dirt clods backward off the rear
    // knobby, heaviest when the engine wins against the ground.
    if (driving && vehicle.grounded () && !in_water &&
        vehicle.thrust () > 0.6f) {
      const float speed = length (vehicle.velocity ());
      const float slip = scalar_value (vehicle.thrust ()) *
                         (1.0f - std::min (1.0f, speed / 30.0f));
      if (slip > 0.15f) {
        Dust::Style roost;
        roost.size = 0.16f * u::m;
        roost.flake = true;
        roost.lifetime = 0.9f * u::s;
        roost.downward_acceleration =
          12.0f * isq::acceleration[u::m / pow<2> (u::s)];
        roost.spread = 0.5f * one;
        session.dust ().emit (moppe::position (rear_wheel),
                              velocity (forward * (-6.0f - 14.0f * slip) +
                                        Vec3 (0, 3.5f + 3.0f * slip, 0)),
                              1 + (int)(slip * 3.0f),
                              clod_color,
                              roost);
      }
    }

    // While the jets burn: hot additive embers shot out along them, a
    // thin smoke trailing off their ends, and, close over the ground, a
    // blast of whatever the ground is made of. The nozzles swing from
    // straight down at rest to back and down under full drive.
    if (driving && vehicle.boost_level () > 0.05f) {
      const float level = vehicle.boost_level ();
      const float swing = 1.047f * vehicle.boost_drive ();
      const Vec3 jet = normalized (Vec3 (0, -std::cos (swing), 0) -
                                   forward * std::sin (swing));
      const Vec3 nozzles =
        vehicle_position - forward * 0.3f + Vec3 (0, -0.45f, 0);
      std::uniform_real_distribution<float> chance (0.0f, 1.0f);
      const auto happens = [&] (float per_second) {
        const probability_t odds (per_second / u::s * (elapsed * u::s));
        return chance (logic.m_fx_rng) < scalar_value (odds);
      };
      if (happens (90.0f * level)) {
        Dust::Style ember;
        ember.size = 0.12f * u::m;
        ember.lifetime = 0.40f * u::s;
        ember.downward_acceleration =
          6.0f * isq::acceleration[u::m / pow<2> (u::s)];
        ember.spread = 0.35f * one;
        ember.additive = true;
        session.dust ().emit (
          moppe::position (nozzles + jet * 0.4f),
          velocity (vehicle.velocity () * 0.6f + jet * 16.0f),
          2,
          chance (logic.m_fx_rng) < 0.5f ? DisplayColor (1.0f, 0.62f, 0.20f)
                                         : DisplayColor (1.0f, 0.86f, 0.50f),
          ember);
      }
      if (happens (22.0f * level)) {
        Dust::Style smoke;
        smoke.size = 0.55f * u::m;
        smoke.lifetime = 1.1f * u::s;
        smoke.downward_acceleration =
          -1.5f * isq::acceleration[u::m / pow<2> (u::s)];
        smoke.spread = 0.45f * one;
        session.dust ().emit (
          moppe::position (nozzles + jet * (2.0f + 2.5f * level)),
          velocity (vehicle.velocity () * 0.3f + jet * 3.0f),
          1,
          DisplayColor (0.62f, 0.60f, 0.58f),
          smoke);
      }
      if (vehicle.grounded () && jet[1] < -0.5f && happens (40.0f * level)) {
        Dust::Style blast;
        blast.size = 0.30f * u::m;
        blast.lifetime = 0.9f * u::s;
        blast.downward_acceleration =
          3.0f * isq::acceleration[u::m / pow<2> (u::s)];
        blast.spread = 1.4f * one;
        const float around = 6.2831853f * chance (logic.m_fx_rng);
        const Vec3 out (std::cos (around), 0, std::sin (around));
        session.dust ().emit (moppe::position (nozzles + Vec3 (0, -0.4f, 0)),
                              velocity (out * 7.0f + Vec3 (0, 1.5f, 0)),
                              in_water ? 3 : std::max (1, loose (3.0f)),
                              in_water ? spray_color : dust_color);
      }
    }

    // Exhaust smoke: faint gray puffs rise off the muffler while the
    // throttle is open.
    if (driving && abs (vehicle.thrust ()) > 0.3f) {
      std::uniform_real_distribution<float> chance (0.0f, 1.0f);
      const probability_t puff (14.0f / u::s * (elapsed * u::s));
      if (chance (logic.m_fx_rng) < scalar_value (puff)) {
        Dust::Style smoke;
        smoke.size = 0.35f * u::m;
        smoke.lifetime = 0.8f * u::s;
        smoke.downward_acceleration =
          -2.5f * isq::acceleration[u::m / pow<2> (u::s)];
        smoke.spread = 0.25f * one;
        session.dust ().emit (moppe::position (vehicle_position -
                                               forward * 1.2f +
                                               Vec3 (0, -0.4f, 0)),
                              velocity (vehicle.velocity () * 0.25f),
                              1,
                              DisplayColor (0.45f, 0.45f, 0.48f),
                              smoke);
      }
    }

    // Wading fast throws up a bow wave.
    if (driving && in_water && length (vehicle.velocity ()) > 15.0f)
      session.dust ().emit (
        moppe::position (vehicle_position + Vec3 (0, -0.5f, 0)),
        velocity (vehicle.velocity () * 0.3f),
        3,
        spray_color);

    // Hard landings shake the camera and burst dirt outward: a low pancake of
    // dust plus a ring of ballistic clods.
    if (impact > 8.0f) {
      logic.m_shake = std::min (0.28f, 0.010f * impact);
      logic.m_shake_time = 0.0f;
      session.dust ().emit (
        moppe::position (vehicle_position + Vec3 (0, -0.7f, 0)),
        velocity (vehicle.velocity () * 0.2f),
        in_water ? 12 : loose (12.0f),
        in_water ? spray_color : dust_color);
      if (!in_water) {
        Dust::Style burst;
        burst.size = 0.2f * u::m;
        burst.flake = true;
        burst.lifetime = 1.1f * u::s;
        burst.downward_acceleration =
          10.0f * isq::acceleration[u::m / pow<2> (u::s)];
        burst.spread = 1.4f * one;
        session.dust ().emit (
          moppe::position (vehicle_position + Vec3 (0, -0.6f, 0)),
          velocity (vehicle.velocity () * 0.15f +
                    Vec3 (0, 2.0f + 0.15f * impact, 0)),
          (int)std::min (10.0f, impact * 0.5f),
          clod_color,
          burst);
      }
    }

    GameSessionAdvanceResult result;
    // Crashes hurt; health trickles back slowly. Falls from above a hundred
    // metres are simply fatal -- house rule.
    if (impact > 9.0f)
      logic.m_health -= (impact - 9.0f) * 4.5f;
    if (driving && vehicle.pop_fall_drop () > 100.0f)
      logic.m_health = 0.0f;
    logic.m_health = std::min (100.0f, logic.m_health + 1.5f * elapsed);
    if (logic.m_health <= 0.0f) {
      session.dust ().emit (moppe::position (vehicle_position),
                            velocity (Vec3 (0, 6, 0)),
                            40,
                            DisplayColor (1.0f, 0.5f, 0.1f));
      --logic.m_lives;
      if (logic.m_lives <= 0) {
        logic.m_game_over = true;
      } else {
        // Halfway through the hearts, the game offers its sympathies out
        // loud. The app realizes this platform effect after the advance.
        result.say_ouchies = logic.m_lives == 5;

        // Respawn where you crashed, upright on the ground.
        const float ground = terrain::surface_elevation_value (
          spatial::sample<terrain::surface_elevation> (
            surface,
            moppe::position (
              Vec3 (vehicle_position[0], 0.0f, vehicle_position[2]))));
        vehicle.reset (
          Vec3 (vehicle_position[0], ground + 1.2f, vehicle_position[2]));
        logic.m_health = 100.0f;
        logic.m_shake = 1.0f;
        logic.m_shake_time = 0.0f;
      }
    }

    // Star pickups sparkle gold and top up the boost reserve.
    {
      const int picked =
        session.stars ().update (vehicle_position, logic.m_total_time, elapsed);
      if (picked > 0) {
        Dust::Style sparkle;
        sparkle.size = 0.38f * u::m;
        sparkle.lifetime = 0.85f * u::s;
        sparkle.downward_acceleration =
          -1.5f * isq::acceleration[u::m / pow<2> (u::s)];
        sparkle.spread = 1.7f * one;
        sparkle.additive = true;
        session.dust ().emit (moppe::position (session.stars ().last_pos ()),
                              velocity (Vec3 (0, 4, 0)),
                              32,
                              DisplayColor (1.0f, 0.72f, 0.12f),
                              sparkle);
        Dust::Style flash;
        flash.size = 0.9f * u::m;
        flash.lifetime = 0.35f * u::s;
        flash.spread = 0.25f * one;
        flash.additive = true;
        session.dust ().emit (moppe::position (session.stars ().last_pos ()),
                              velocity (Vec3 ()),
                              5,
                              DisplayColor (1.0f, 0.95f, 0.55f),
                              flash);
        if (logic.m_mode != M_GLIDER)
          vehicle.replenish_boost (0.25f * picked);
      }
    }

    if (driving)
      logic.m_odometer += length (vehicle.velocity ()) * elapsed;

    session.dust ().update (dt);
    logic.m_shake_time += elapsed;
    logic.m_shake *= decay (7.0f / u::s, elapsed * u::s);

    // Riding or gliding, a look left alone drifts back to the way ahead.
    logic.m_look_idle += elapsed;
    if (logic.m_mode != M_FOOT && logic.m_look_idle > 1.2f) {
      const float settle = 1.0f - decay (2.5f / u::s, elapsed * u::s);
      logic.m_look_yaw -= logic.m_look_yaw * settle;
      logic.m_look_pitch -= logic.m_look_pitch * settle;
    }

    if (logic.m_cam_mode == CAM_HELMET) {
      // Ride inside the rider's head; lightly smoothed so terrain bumps do
      // not rattle the eyeballs.
      Vec3 eye, look;
      if (logic.m_mode == M_FOOT) {
        eye = session.walker ().eye_position ();
        look = session.walker ().heading () * std::cos (logic.m_look_pitch) +
               Vec3 (0, std::sin (logic.m_look_pitch), 0);
      } else {
        Vec3 ahead;
        if (logic.m_mode == M_GLIDER) {
          eye = session.glider ().position () - Vec3 (0, 0.75f, 0);
          ahead = session.glider ().heading ();
        } else {
          eye = vehicle.position () + Vec3 (0, 0.95f, 0) +
                vehicle.orientation () * 0.4f;
          ahead = vehicle.orientation ();
        }
        // The head turns about the vertical and then nods about its own
        // right, from the machine's heading.
        const Vec3 up (0, 1, 0);
        look = Quaternion::rotate (ahead, up, -logic.m_look_yaw * u::rad);
        Vec3 right = cross (look, up);
        if (length2 (right) > 1e-6f) {
          normalize (right);
          look =
            Quaternion::rotate (look, right, logic.m_look_pitch * u::rad);
        }
      }
      logic.m_fp_eye =
        logic.m_fp_eye +
        (eye - logic.m_fp_eye) * smoothing_alpha (25.0f / u::s, elapsed * u::s);
      session.camera ().place (logic.m_fp_eye, logic.m_fp_eye + look * 10.0f);
    } else {
      const float flip = logic.m_cam_mode == CAM_FRONT ? -1.0f : 1.0f;
      const float elevation = logic.m_mode == M_FOOT ? 10.0f : 18.0f;
      session.camera ().frame (elevation * u::deg,
                               logic.m_mode == M_FOOT ? 3.8f * u::m
                                                      : 6.5f * u::m);
      // Looking up swings the camera down behind the subject and looking
      // down lifts it, between skimming the ground and nearly overhead.
      const float base = elevation * 0.017453293f;
      const float raised =
        std::clamp (base - logic.m_look_pitch, -0.12f, 1.25f);
      session.camera ().aim (logic.m_look_yaw, raised - base);
      if (logic.m_mode == M_FOOT)
        session.camera ().update (
          moppe::position (session.walker ().position () + Vec3 (0, 1.35f, 0)),
          session.walker ().heading () * flip,
          velocity (session.walker ().velocity ()),
          dt);
      else if (logic.m_mode == M_GLIDER)
        session.camera ().update (session.glider ().physical_position (),
                                  session.glider ().heading () * flip,
                                  session.glider ().physical_velocity (),
                                  dt);
      else
        session.camera ().update (vehicle.physical_position (),
                                  vehicle.orientation () * flip,
                                  vehicle.physical_velocity (),
                                  dt);
      session.camera ().limit (surface);
    }

    // Speed widens the field of view a touch.
    const float kmh = (driving || logic.m_mode == M_GLIDER)
                        ? session.subject_speed_kmh ()
                        : 0.0f;
    const float fov_target =
      std::min (1.0f, std::max (0.0f, (kmh - 70.0f) / 180.0f));
    logic.m_fov_k += (fov_target - logic.m_fov_k) *
                     smoothing_alpha (5.0f / u::s, elapsed * u::s);

    return result;
  }
}
