#include <moppe/game/game_session.hh>

#include <algorithm>
#include <cmath>
#include <random>

namespace moppe::game {
  namespace {
    void set_turn (GameSession& session, float value) {
      GameLogicState& logic = session.logic ();
      logic.m_turn_input = value;
      if (logic.m_mode == M_FOOT)
        session.walker ().set_turn (value);
      else if (logic.m_mode == M_GLIDER)
        session.glider ().set_turn (value);
      else
        session.active_vehicle ().set_yaw ((90 * value) * u::deg);
    }

    void set_go (GameSession& session, float value) {
      GameLogicState& logic = session.logic ();
      logic.m_go_input = value;
      if (logic.m_mode == M_FOOT)
        session.walker ().set_walk (value > 0 ? value : value * 0.6f);
      else if (logic.m_mode == M_GLIDER)
        session.glider ().set_speed_control (value);
      else {
        session.active_vehicle ().set_thrust (value);
        session.active_vehicle ().set_boost (logic.m_boost_input,
                                             logic.m_go_input);
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
        session.active_vehicle ().set_boost (logic.m_boost_input,
                                             logic.m_go_input);
      }
    }

    void deploy_glider (GameSession& session, const map::HeightMap& terrain) {
      if (!session.can_deploy_glider (terrain))
        return;
      const Vec3 position = session.bike ().position ();
      const Vec3 heading = session.bike ().orientation ();
      const velocity_t inherited = session.bike ().physical_velocity ();
      session.bike ().set_thrust (0);
      session.bike ().set_yaw (0 * u::deg);
      session.bike ().set_boost (0, 0);
      session.glider ().launch (
        moppe::position (position + Vec3 (0, 1.0f, 0)), inherited, heading);
      GameLogicState& logic = session.logic ();
      logic.m_mode = M_GLIDER;
      session.glider ().set_turn (logic.m_turn_input);
      session.glider ().set_speed_control (logic.m_go_input);
      session.glider ().set_flare (logic.m_boost_input > 0.1f);
    }

    void finish_glide (GameSession& session) {
      const Vec3 position = session.glider ().position ();
      session.walker ().spawn (moppe::position (position + Vec3 (0, 0.15f, 0)),
                               session.glider ().heading ());
      GameLogicState& logic = session.logic ();
      logic.m_mode = M_FOOT;
      set_turn (session, logic.m_turn_input);
      set_go (session, logic.m_go_input);
      set_boost (session, 0);
    }

    void toggle_mount (GameSession& session) {
      GameLogicState& logic = session.logic ();
      if (logic.m_mode == M_GLIDER)
        return;

      if (logic.m_mode != M_FOOT) {
        // Step off to the side of whatever we're driving.
        mov::Vehicle& vehicle = session.active_vehicle ();
        const Vec3 heading = vehicle.orientation ();
        const Vec3 side (heading[2], 0, -heading[0]);
        session.walker ().spawn (
          moppe::position (vehicle.position () +
                           side * (logic.m_mode == M_CAR ? 2.4f : 1.8f)),
          heading);
        vehicle.set_thrust (0);
        vehicle.set_yaw (0 * u::deg);
        vehicle.set_boost (0, 0);
        logic.m_mode = M_FOOT;
        set_turn (session, logic.m_turn_input);
        set_go (session, logic.m_go_input);
        return;
      }

      // On foot: bike first, then our parked car, then grand theft.
      if (length2 (session.walker ().position () -
                   session.bike ().position ()) < 5.0f * 5.0f) {
        session.bike ().set_thrust (0);
        session.bike ().set_yaw (0 * u::deg);
        logic.m_mode = M_BIKE;
        set_turn (session, logic.m_turn_input);
        set_go (session, logic.m_go_input);
        set_boost (session, logic.m_boost_input);
        return;
      }

      if (logic.m_car_exists &&
          length2 (session.walker ().position () - session.car ().position ()) <
            6.0f * 6.0f) {
        session.car ().set_thrust (0);
        session.car ().set_yaw (0 * u::deg);
        logic.m_mode = M_CAR;
        set_turn (session, logic.m_turn_input);
        set_go (session, logic.m_go_input);
        set_boost (session, logic.m_boost_input);
      }
    }

    void apply_input_frame (GameSession& session,
                            const map::HeightMap& terrain,
                            const InputFrame& input) {
      set_turn (session, input_value (input.turn));
      set_go (session, input_value (input.drive));
      set_boost (session, input_value (input.boost));

      if (input.deploy_glider)
        deploy_glider (session, terrain);
      if (input.toggle_mount) {
        if (session.can_deploy_glider (terrain))
          deploy_glider (session, terrain);
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
                            const map::RandomHeightMap& terrain,
                            const map::Surface& surface)
      : m_bike (world.spawn_position (),
                45 * u::deg,
                terrain,
                2600 * u::N,
                30 * u::kW,
                150 * u::kg),
        m_car (world.spawn_position (),
               45 * u::deg,
               terrain,
               14 * u::kN,
               100 * u::kW,
               900 * u::kg),
        m_glider (surface), m_camera (18 * u::deg, 6.5f * u::m) {}

  mov::Vehicle& GameSession::active_vehicle () noexcept {
    return m_logic.m_mode == M_CAR ? m_car : m_bike;
  }

  const mov::Vehicle& GameSession::active_vehicle () const noexcept {
    return m_logic.m_mode == M_CAR ? m_car : m_bike;
  }

  Vec3 GameSession::subject_position () const {
    if (m_logic.m_mode == M_FOOT)
      return m_walker.position ();
    if (m_logic.m_mode == M_GLIDER)
      return m_glider.position ();
    return active_vehicle ().position ();
  }

  Vec3 GameSession::subject_heading () const {
    if (m_logic.m_mode == M_FOOT)
      return m_walker.heading ();
    if (m_logic.m_mode == M_GLIDER)
      return m_glider.heading ();
    return active_vehicle ().orientation ();
  }

  float GameSession::subject_speed_kmh () const {
    if (m_logic.m_mode == M_FOOT)
      return 0.0f;
    if (m_logic.m_mode == M_GLIDER)
      return m_glider.airspeed ().numerical_value_in (u::m / u::s) * 3.6f;
    return length (active_vehicle ().velocity ()) * 3.6f;
  }

  bool GameSession::can_deploy_glider (const map::HeightMap& terrain) const {
    if (m_logic.m_mode != M_BIKE || !m_bike.airborne ())
      return false;
    const Vec3 position = m_bike.position ();
    const float ground = terrain.interpolated_height (position[0], position[2]);
    return position[1] - ground > 3.0f;
  }

  void GameSession::clear_controls () {
    set_turn (*this, 0.0f);
    set_go (*this, 0.0f);
    set_boost (*this, 0.0f);
  }

  GameSession::State GameSession::state () const {
    return { m_logic,           m_bike.state (),   m_car.state (),
             m_glider.state (), m_walker.state (), m_camera.state (),
             m_stars.state (),  m_dust.state () };
  }

  void GameSession::restore (const State& state) {
    m_logic = state.logic;
    m_bike.restore (state.vehicle);
    m_car.restore (state.car);
    m_glider.restore (state.glider);
    m_walker.restore (state.walker);
    m_camera.restore (state.camera);
    m_stars.restore (state.stars);
    m_dust.restore (state.dust);
  }

  GameSessionAdvanceResult
  advance_game_session (const GameSessionAdvanceContext& context,
                        GameSession& session,
                        const InputFrame& input,
                        seconds_t dt) {
    const float elapsed = dt.numerical_value_in (u::s);
    GameLogicState& logic = session.logic ();
    session.bike ().set_water_level (context.world.water_level);
    session.car ().set_water_level (context.world.water_level);
    session.bike ().set_obstacles (&context.obstacles);
    session.car ().set_obstacles (&context.obstacles);

    apply_input_frame (session, context.terrain, input);

    session.bike ().update (dt);
    if (logic.m_car_exists)
      session.car ().update (dt);
    if (logic.m_mode == M_GLIDER && session.glider ().update (dt))
      finish_glide (session);
    if (logic.m_mode == M_FOOT)
      session.walker ().update (
        dt, context.terrain, context.obstacles, context.world);

    const Vec3 vehicle_position = session.subject_position ();
    mov::Vehicle& vehicle = session.active_vehicle ();

    // Parked vehicles' impacts shouldn't linger until remount.
    if (logic.m_mode != M_BIKE) {
      session.bike ().pop_impact ();
      session.bike ().pop_fall_drop ();
    }
    if (logic.m_car_exists && logic.m_mode != M_CAR) {
      session.car ().pop_impact ();
      session.car ().pop_fall_drop ();
    }

    const bool in_water =
      vehicle_position[1] < meters_value (context.world.water_level) + 1.0f;
    const bool driving = logic.m_mode == M_BIKE || logic.m_mode == M_CAR;

    // Long jumps become score events after three seconds. Keep the last
    // airborne time locally because Vehicle clears its timer on touchdown.
    if (driving && vehicle.airtime () > 0.0f) {
      logic.m_jump_airtime = vehicle.airtime ();
      logic.m_landed_age += elapsed;
    } else {
      if (driving && logic.m_jump_airtime >= 3.0f) {
        logic.m_landed_airtime = logic.m_jump_airtime;
        logic.m_landed_points = (int)std::round (100.0f * logic.m_jump_airtime *
                                                 logic.m_jump_airtime);
        logic.m_score += logic.m_landed_points;
        logic.m_landed_age = 0.0f;
      }
      logic.m_jump_airtime = 0.0f;
      logic.m_landed_age += elapsed;
    }
    const DisplayColor dust_color (0.60f, 0.52f, 0.40f);
    const DisplayColor clod_color (0.42f, 0.34f, 0.24f);
    const DisplayColor spray_color (0.85f, 0.92f, 1.0f);
    const Vec3 forward = session.subject_heading ();
    const Vec3 rear_wheel =
      vehicle_position - forward * 1.4f + Vec3 (0, -0.7f, 0);

    // Drift kicks up dirt from the rear wheel (or spray).
    if (driving && vehicle.grounded () && vehicle.drift_speed () > 6.0f) {
      const int count = std::min (4, (int)(vehicle.drift_speed () * 0.2f));
      session.dust ().emit (moppe::position (rear_wheel),
                            velocity (vehicle.velocity () * 0.15f),
                            count,
                            in_water ? spray_color : dust_color);
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
        roost.size = 0.45f * u::m;
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

    // Jet embers: hot additive sparks stream out of the nozzles while the
    // jets burn, arcing down and dying fast.
    if (driving && vehicle.boost_level () > 0.05f) {
      std::uniform_real_distribution<float> chance (0.0f, 1.0f);
      const probability_t spark (34.0f / u::s * vehicle.boost_level () *
                                 (elapsed * u::s));
      if (chance (logic.m_fx_rng) < scalar_value (spark)) {
        Dust::Style ember;
        ember.size = 0.15f * u::m;
        ember.lifetime = 0.45f * u::s;
        ember.downward_acceleration =
          6.0f * isq::acceleration[u::m / pow<2> (u::s)];
        ember.spread = 0.3f * one;
        ember.additive = true;
        session.dust ().emit (moppe::position (vehicle_position -
                                               forward * 0.5f +
                                               Vec3 (0, -0.5f, 0)),
                              velocity (vehicle.velocity () * 0.5f -
                                        forward * 2.0f + Vec3 (0, -4.0f, 0)),
                              1,
                              DisplayColor (1.0f, 0.55f, 0.18f),
                              ember);
      }
    }

    // Exhaust smoke: faint gray puffs rise off the muffler while the
    // throttle is open.
    if (driving && abs (vehicle.thrust ()) > 0.3f && logic.m_mode == M_BIKE) {
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
    const float impact = driving ? vehicle.pop_impact () : 0.0f;
    if (impact > 8.0f) {
      logic.m_shake = std::min (0.28f, 0.010f * impact);
      logic.m_shake_time = 0.0f;
      session.dust ().emit (
        moppe::position (vehicle_position + Vec3 (0, -0.7f, 0)),
        velocity (vehicle.velocity () * 0.2f),
        12,
        in_water ? spray_color : dust_color);
      if (!in_water) {
        Dust::Style burst;
        burst.size = 0.5f * u::m;
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
        const float ground = context.terrain.interpolated_height (
          vehicle_position[0], vehicle_position[2]);
        vehicle.reset (
          Vec3 (vehicle_position[0], ground + 1.2f, vehicle_position[2]));
        logic.m_health = 100.0f;
        logic.m_shake = 1.0f;
        logic.m_shake_time = 0.0f;
      }
    }

    // Star pickups sparkle gold and top up fuel and boost reserves.
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
        logic.m_fuel = std::min (100.0f, logic.m_fuel + 25.0f * picked);
        if (logic.m_mode != M_GLIDER)
          vehicle.replenish_boost (0.25f * picked);
      }
    }

    // Fuel: the throttle burns it; an empty tank limps along at a third power
    // (never fully stranded).
    if (driving) {
      logic.m_fuel = std::max (
        0.0f,
        logic.m_fuel - scalar_value (abs (vehicle.thrust ())) * 0.9f * elapsed);
      logic.m_odometer += length (vehicle.velocity ()) * elapsed;

      const float want =
        logic.m_go_input *
        ((logic.m_fuel <= 0.5f && logic.m_go_input > 0) ? 0.3f : 1.0f);
      vehicle.set_thrust (want);
    }

    session.dust ().update (dt);
    logic.m_shake_time += elapsed;
    logic.m_shake *= decay (7.0f / u::s, elapsed * u::s);

    if (logic.m_cam_mode == CAM_HELMET) {
      // Ride inside the rider's head; lightly smoothed so terrain bumps do
      // not rattle the eyeballs.
      Vec3 eye, look;
      if (logic.m_mode == M_FOOT) {
        eye = session.walker ().position () +
              Vec3 (0, 1.55f / context.landscape_scale_y, 0);
        look = session.walker ().heading ();
      } else if (logic.m_mode == M_GLIDER) {
        eye = session.glider ().position () -
              Vec3 (0, 0.75f / context.landscape_scale_y, 0);
        look = session.glider ().heading ();
      } else {
        eye = vehicle.position () +
              Vec3 (0, 0.95f / context.landscape_scale_y, 0) +
              vehicle.orientation () * (0.4f / context.landscape_scale_x);
        look = vehicle.orientation ();
      }
      logic.m_fp_eye =
        logic.m_fp_eye +
        (eye - logic.m_fp_eye) * smoothing_alpha (25.0f / u::s, elapsed * u::s);
      session.camera ().place (logic.m_fp_eye, logic.m_fp_eye + look * 10.0f);
    } else {
      session.camera ().set_landscape_scale (context.landscape_scale_x,
                                             context.landscape_scale_y);
      const float flip = logic.m_cam_mode == CAM_FRONT ? -1.0f : 1.0f;
      if (logic.m_mode == M_FOOT)
        session.camera ().update (
          moppe::position (session.walker ().position () +
                           Vec3 (0, 1.0f / context.landscape_scale_y, 0)),
          session.walker ().heading () * flip,
          velocity (Vec3 ()),
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
      session.camera ().limit (context.terrain);
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
