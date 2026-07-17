#include <moppe/game/game_session.hh>
#include <moppe/game/game_state.hh>
#include <moppe/game/input_frame_adapter.hh>
#include <moppe/map/generate.hh>
#include <moppe/map/surface.hh>

#include <tests/test.hh>

#include <algorithm>
#include <type_traits>
#include <vector>

namespace {
  void check_vector (const moppe::Vec3& actual, const moppe::Vec3& expected) {
    MOPPE_CHECK_NEAR (actual[0], expected[0], 1e-6f);
    MOPPE_CHECK_NEAR (actual[1], expected[1], 1e-6f);
    MOPPE_CHECK_NEAR (actual[2], expected[2], 1e-6f);
  }

  void check_position (const moppe::position_t& actual,
                       const moppe::position_t& expected) {
    check_vector (moppe::position_value (actual),
                  moppe::position_value (expected));
  }

  void check_velocity (const moppe::velocity_t& actual,
                       const moppe::velocity_t& expected) {
    check_vector (moppe::velocity_value (actual),
                  moppe::velocity_value (expected));
  }

  void check_color (moppe::DisplayColor actual, moppe::DisplayColor expected) {
    MOPPE_CHECK_NEAR (actual.red, expected.red, 1e-6f);
    MOPPE_CHECK_NEAR (actual.green, expected.green, 1e-6f);
    MOPPE_CHECK_NEAR (actual.blue, expected.blue, 1e-6f);
  }
}

MOPPE_TEST (vehicle_state_restores_hidden_simulation_state) {
  using namespace moppe;
  map::RandomHeightMap map (
    9, 9, Vec3 (100, 20, 100), 1, terrain::Topology::Torus);
  map.randomize_geologically ();
  map.recompute_normals ();
  mov::Vehicle vehicle (position (Vec3 (20, 0, 20)),
                        15 * u::deg,
                        map,
                        1000 * u::N,
                        10 * u::kW,
                        100 * u::kg);
  vehicle.set_thrust (0.8f);
  vehicle.set_yaw (25 * u::deg);
  vehicle.set_boost (0.7f, 0.5f);
  vehicle.update (seconds (1.0f / 60.0f));
  const mov::Vehicle::State saved = vehicle.state ();

  vehicle.set_thrust (-1.0f);
  vehicle.set_yaw (-70 * u::deg);
  vehicle.update (seconds (0.5f));
  vehicle.restore (saved);
  const mov::Vehicle::State restored = vehicle.state ();

  static_assert (
    std::is_same_v<decltype (vehicle.physical_position ()), position_t>);
  static_assert (
    std::is_same_v<decltype (vehicle.physical_velocity ()), velocity_t>);
  check_position (restored.position, saved.position);
  check_velocity (restored.velocity, saved.velocity);
  check_vector (restored.heading, saved.heading);
  check_vector (restored.thrust_orientation, saved.thrust_orientation);
  check_vector (restored.render_heading, saved.render_heading);
  check_vector (restored.render_normal, saved.render_normal);
  check_color (restored.body_color, saved.body_color);
  MOPPE_CHECK_NEAR (
    radians_value (restored.yaw), radians_value (saved.yaw), 1e-6f);
  MOPPE_CHECK_NEAR (radians_value (restored.yaw_target),
                    radians_value (saved.yaw_target),
                    1e-6f);
  MOPPE_CHECK_NEAR (restored.lean, saved.lean, 1e-6f);
  MOPPE_CHECK_NEAR (restored.susp, saved.susp, 1e-6f);
  MOPPE_CHECK_NEAR (restored.susp_v, saved.susp_v, 1e-6f);
  MOPPE_CHECK_NEAR (restored.wheel_spin, saved.wheel_spin, 1e-6f);
  MOPPE_CHECK (restored.boost_flight == saved.boost_flight);
  MOPPE_CHECK_NEAR (
    scalar_value (restored.thrust), scalar_value (saved.thrust), 1e-6f);
  MOPPE_CHECK_NEAR (restored.boost_input, saved.boost_input, 1e-6f);
  MOPPE_CHECK_NEAR (restored.boost_drive, saved.boost_drive, 1e-6f);
  MOPPE_CHECK_NEAR (restored.boost_level, saved.boost_level, 1e-6f);
  MOPPE_CHECK_NEAR (restored.boost_charge, saved.boost_charge, 1e-6f);
  MOPPE_CHECK_NEAR (seconds_value (restored.boost_recharge_delay),
                    seconds_value (saved.boost_recharge_delay),
                    1e-6f);
  MOPPE_CHECK_NEAR (seconds_value (restored.airborne_time),
                    seconds_value (saved.airborne_time),
                    1e-6f);
  MOPPE_CHECK_NEAR (restored.impact.numerical_value_in (u::m / u::s),
                    saved.impact.numerical_value_in (u::m / u::s),
                    1e-6f);
  MOPPE_CHECK_NEAR (
    meters_value (restored.fall_top), meters_value (saved.fall_top), 1e-6f);
  MOPPE_CHECK_NEAR (
    meters_value (restored.fall_drop), meters_value (saved.fall_drop), 1e-6f);
  MOPPE_CHECK (restored.body_kind == saved.body_kind);
}

MOPPE_TEST (camera_and_walker_state_round_trip) {
  using namespace moppe;
  game::ChaseCamera camera (18 * u::deg, 6.5f * u::m);
  camera.update (position (Vec3 (10, 2, 20)),
                 Vec3 (0, 0, 1),
                 velocity (Vec3 (4, 0, 2)),
                 seconds (1.0f / 60.0f));
  const game::ChaseCamera::State camera_state = camera.state ();
  camera.place (Vec3 (100, 100, 100), Vec3 ());
  camera.restore (camera_state);
  check_position (camera.state ().position, camera_state.position);
  check_position (camera.state ().target, camera_state.target);
  check_velocity (camera.state ().position_velocity,
                  camera_state.position_velocity);

  game::Walker walker;
  walker.spawn (position (Vec3 (3, 4, 5)), Vec3 (1, 0, 0));
  walker.set_turn (0.4f);
  walker.set_walk (0.8f);
  const game::Walker::State walker_state = walker.state ();
  walker.spawn (position (Vec3 (30, 40, 50)), Vec3 (0, 0, -1));
  walker.restore (walker_state);
  check_position (walker.state ().position, walker_state.position);
  check_vector (walker.state ().heading, walker_state.heading);
  MOPPE_CHECK_NEAR (scalar_value (walker.state ().turn),
                    scalar_value (walker_state.turn),
                    1e-6f);
  MOPPE_CHECK_NEAR (scalar_value (walker.state ().walk),
                    scalar_value (walker_state.walk),
                    1e-6f);
}

MOPPE_TEST (glider_polar_and_flight_use_soaring_quantities) {
  using namespace moppe;
  map::RandomHeightMap map (
    17, 17, Vec3 (200, 20, 200), 1, terrain::Topology::Bounded);
  std::fill (map.raw_heights (),
             map.raw_heights () + map.width () * map.height (),
             0.5f);
  map.recompute_normals ();
  map::Surface surface (map);

  const auto ratio =
    mov::Glider::glide_ratio_at (16.0f * airspeed[u::m / u::s]);
  static_assert (std::is_same_v<decltype (ratio), const mov::glide_ratio_t>);
  MOPPE_CHECK (ratio.numerical_value_in (one) > 18.0f);

  mov::Glider glider (surface);
  glider.launch (
    position (Vec3 (80, 50, 80)), velocity (Vec3 (0, 2, 18)), Vec3 (0, 0, 1));
  glider.set_turn (0.6f);
  const float start_y = glider.position ()[1];
  const float start_z = glider.position ()[2];
  for (int i = 0; i < 60; ++i)
    glider.update (seconds (1.0f / 60.0f));

  MOPPE_CHECK (glider.position ()[2] > start_z + 10.0f);
  MOPPE_CHECK (glider.position ()[1] < start_y + 2.0f);
  MOPPE_CHECK (glider.heading ()[0] > 0.05f);
  MOPPE_CHECK (glider.air_mass_lift ().numerical_value_in (u::m / u::s) ==
               0.0f);

  mov::Glider landing (surface);
  landing.launch (
    position (Vec3 (100, 14, 100)), velocity (Vec3 (0, 0, 14)), Vec3 (0, 0, 1));
  for (int i = 0; i < 1800 && !landing.landed (); ++i)
    landing.update (seconds (1.0f / 60.0f));
  MOPPE_CHECK (landing.landed ());
  MOPPE_CHECK_NEAR (landing.position ()[1], 10.75f, 1e-4f);
}

MOPPE_TEST (glider_state_restores_the_flight_computer) {
  using namespace moppe;
  map::RandomHeightMap map (
    17, 17, Vec3 (200, 20, 200), 1, terrain::Topology::Torus);
  std::fill (map.raw_heights (),
             map.raw_heights () + map.width () * map.height (),
             0.35f);
  map.recompute_normals ();
  map::Surface surface (map);

  mov::Glider glider (surface);
  glider.launch (
    position (Vec3 (40, 70, 40)), velocity (Vec3 (12, -1, 15)), Vec3 (0, 0, 1));
  glider.set_turn (-0.7f);
  glider.set_speed_control (0.8f);
  glider.set_flare (true);
  glider.update (seconds (0.25f));
  const mov::Glider::State saved = glider.state ();

  glider.set_turn (1.0f);
  glider.set_flare (false);
  glider.update (seconds (1.0f));
  glider.restore (saved);
  const mov::Glider::State restored = glider.state ();

  check_position (restored.position, saved.position);
  check_velocity (restored.velocity, saved.velocity);
  check_vector (restored.heading, saved.heading);
  MOPPE_CHECK_NEAR (
    radians_value (restored.bank), radians_value (saved.bank), 1e-6f);
  MOPPE_CHECK_NEAR (restored.airspeed.numerical_value_in (u::m / u::s),
                    saved.airspeed.numerical_value_in (u::m / u::s),
                    1e-6f);
  MOPPE_CHECK (restored.flare == saved.flare);
  MOPPE_CHECK (restored.landed == saved.landed);
}

MOPPE_TEST (star_state_restores_attraction_and_respawn_state) {
  using namespace moppe;
  map::RandomHeightMap map (
    17, 17, Vec3 (100, 20, 100), 1, terrain::Topology::Torus);
  std::fill (map.raw_heights (),
             map.raw_heights () + map.width () * map.height (),
             0.5f);
  map.recompute_normals ();
  game::WorldParams world;
  world.map_size = spatial_extent_in_metres (map.size ());
  world.water_level = 0 * u::m;
  game::Stars stars;
  stars.generate (map, world, 8);
  const game::Stars::State initial = stars.state ();

  MOPPE_CHECK (stars.update (initial.stars[0].position, 0.0f, 1.0f / 60.0f) ==
               1);
  MOPPE_CHECK (stars.collected () == 1);
  stars.restore (initial);
  const game::Stars::State restored = stars.state ();

  MOPPE_CHECK (restored.count == 8);
  MOPPE_CHECK (restored.collected == 0);
  check_vector (restored.stars[0].position, initial.stars[0].position);
  MOPPE_CHECK_NEAR (restored.stars[0].phase, initial.stars[0].phase, 1e-6f);
  MOPPE_CHECK_NEAR (restored.stars[0].respawn, initial.stars[0].respawn, 1e-6f);
}

MOPPE_TEST (dust_state_is_a_bounded_deterministic_emission_log) {
  using namespace moppe;
  game::Dust dust;
  game::Dust::Style style;
  style.lifetime = 2.0f * u::s;
  style.spread = 1.5f * one;
  style.additive = true;
  dust.emit (position (Vec3 (1, 2, 3)),
             velocity (Vec3 (4, 5, 6)),
             90,
             DisplayColor (0.8f, 0.6f, 0.2f),
             style);
  const game::Dust::State saved = dust.state ();
  MOPPE_CHECK (saved.emissions.size () == 2);
  MOPPE_CHECK (saved.emissions[0].particle_count == 64);
  MOPPE_CHECK (saved.emissions[1].particle_count == 26);
  MOPPE_CHECK (saved.emissions[0].id != saved.emissions[1].id);

  dust.update (2.0f * u::s);
  MOPPE_CHECK (dust.state ().emissions.empty ());
  dust.restore (saved);
  const game::Dust::State restored = dust.state ();
  MOPPE_CHECK (restored.emissions.size () == 2);
  MOPPE_CHECK (restored.next_id == saved.next_id);
  MOPPE_CHECK_NEAR (seconds_value (restored.logical_time),
                    seconds_value (saved.logical_time),
                    1e-6f);
  check_position (restored.emissions[0].position, saved.emissions[0].position);
}

MOPPE_TEST (game_state_is_an_independent_value) {
  static_assert (std::is_copy_constructible_v<moppe::game::GameState>);
  static_assert (std::is_copy_assignable_v<moppe::game::GameState>);

  moppe::game::GameState first {};
  first.logic.m_total_time = 12.5;
  first.logic.m_score = 400;
  moppe::game::GameState second = first;
  second.logic.m_total_time = 20.0;
  second.logic.m_score = 900;
  MOPPE_CHECK (first.logic.m_total_time == 12.5);
  MOPPE_CHECK (first.logic.m_score == 400);
}

MOPPE_TEST (game_session_restores_a_same_world_checkpoint) {
  using namespace moppe;
  map::RandomHeightMap map (
    17, 17, Vec3 (200, 20, 200), 1, terrain::Topology::Torus);
  std::fill (map.raw_heights (),
             map.raw_heights () + map.width () * map.height (),
             0.5f);
  map.recompute_normals ();
  map::Surface surface (map);
  game::WorldParams world;
  world.map_size = spatial_extent_in_metres (map.size ());
  world.water_level = 0 * u::m;

  static_assert (!std::is_copy_constructible_v<game::GameSession>);
  static_assert (!std::is_copy_assignable_v<game::GameSession>);
  static_assert (!std::is_move_constructible_v<game::GameSession>);
  static_assert (!std::is_move_assignable_v<game::GameSession>);

  game::GameSession session (world, map, surface);
  session.logic ().m_total_time = 12.5;
  session.logic ().m_score = 400;
  session.bike ().set_thrust (0.8f);
  session.car ().set_thrust (-0.4f);
  session.glider ().launch (
    position (Vec3 (80, 70, 80)), velocity (Vec3 (8, 1, 14)), Vec3 (0, 0, 1));
  session.glider ().set_turn (0.6f);
  session.walker ().spawn (position (Vec3 (3, 4, 5)), Vec3 (1, 0, 0));
  session.camera ().place (Vec3 (8, 9, 10), Vec3 (9, 9, 10));
  session.stars ().generate (map, world, 4);
  session.dust ().emit (position (Vec3 (1, 2, 3)),
                        velocity (Vec3 (4, 5, 6)),
                        3,
                        DisplayColor (0.8f, 0.6f, 0.2f));
  const game::GameSession::State saved = session.state ();

  session.logic ().m_total_time = 20.0;
  session.logic ().m_score = 900;
  session.bike ().set_thrust (-1.0f);
  session.car ().set_thrust (1.0f);
  session.glider ().set_turn (-1.0f);
  session.walker ().spawn (position (Vec3 (30, 40, 50)), Vec3 (0, 0, -1));
  session.camera ().place (Vec3 (100, 100, 100), Vec3 ());
  session.stars ().update (saved.stars.stars[0].position, 0.0f, 1.0f / 60.0f);
  session.dust ().update (1.0f * u::s);
  session.restore (saved);
  const game::GameSession::State restored = session.state ();

  // A checkpoint is a value, not a view into this particular live session.
  // The replacement session is prepared against the same world before its
  // mutable state is restored.
  game::GameSession replacement (world, map, surface);
  replacement.stars ().generate (map, world, 4);
  replacement.restore (saved);
  const game::GameSession::State replayed = replacement.state ();

  MOPPE_CHECK (restored.logic.m_total_time == saved.logic.m_total_time);
  MOPPE_CHECK (restored.logic.m_score == saved.logic.m_score);
  MOPPE_CHECK_NEAR (scalar_value (restored.vehicle.thrust),
                    scalar_value (saved.vehicle.thrust),
                    1e-6f);
  MOPPE_CHECK_NEAR (
    scalar_value (restored.car.thrust), scalar_value (saved.car.thrust), 1e-6f);
  check_position (restored.glider.position, saved.glider.position);
  check_vector (restored.glider.heading, saved.glider.heading);
  check_position (restored.walker.position, saved.walker.position);
  check_position (restored.camera.position, saved.camera.position);
  MOPPE_CHECK (restored.stars.count == saved.stars.count);
  MOPPE_CHECK (restored.stars.collected == saved.stars.collected);
  MOPPE_CHECK (restored.dust.emissions.size () == saved.dust.emissions.size ());
  MOPPE_CHECK (replayed.logic.m_total_time == saved.logic.m_total_time);
  MOPPE_CHECK (replayed.logic.m_score == saved.logic.m_score);
  MOPPE_CHECK_NEAR (scalar_value (replayed.vehicle.thrust),
                    scalar_value (saved.vehicle.thrust),
                    1e-6f);
  check_position (replayed.glider.position, saved.glider.position);
  check_position (replayed.walker.position, saved.walker.position);
  check_position (replayed.camera.position, saved.camera.position);
  MOPPE_CHECK (replayed.stars.count == saved.stars.count);
  MOPPE_CHECK (replayed.dust.emissions.size () == saved.dust.emissions.size ());
}

MOPPE_TEST (game_session_advance_replays_an_input_tape_on_the_same_world) {
  using namespace moppe;

  using AdvanceGameSession =
    game::GameSessionAdvanceResult (*) (const game::GameSessionAdvanceContext&,
                                        game::GameSession&,
                                        const game::InputFrame&,
                                        seconds_t);
  static_assert (
    std::is_same_v<decltype (&game::advance_game_session), AdvanceGameSession>);

  map::RandomHeightMap map (
    17, 17, Vec3 (200, 20, 200), 1, terrain::Topology::Torus);
  std::fill (map.raw_heights (),
             map.raw_heights () + map.width () * map.height (),
             0.5f);
  map.recompute_normals ();
  map::Surface surface (map);
  game::WorldParams world;
  world.map_size = spatial_extent_in_metres (map.size ());
  world.resolution = map.width ();
  world.water_level = 0 * u::m;
  world.terrain_topology = terrain::Topology::Torus;
  std::vector<mov::Box> obstacles;
  const game::GameSessionAdvanceContext context { world, map, obstacles };

  game::InputFrameAdapter recorder;
  recorder.key (platform::Key::D, true);
  recorder.key (platform::Key::W, true);
  recorder.key (platform::Key::Space, true);
  recorder.key (platform::Key::Tab, true);
  std::vector<game::InputFrame> tape;
  tape.push_back (recorder.take_frame ());
  for (int i = 0; i < 179; ++i)
    tape.push_back (recorder.take_frame ());

  const seconds_t step = seconds (1.0f / 60.0f);
  game::GameSession live (world, map, surface);
  const game::GameSession::State checkpoint = live.state ();
  for (const game::InputFrame& frame : tape)
    game::advance_game_session (context, live, frame, step);
  const game::GameSession::State live_state = live.state ();

  // This must be more than a checkpoint round trip: the recorded keyboard
  // tape drove and steered the bike, while its Tab edge changed the camera.
  MOPPE_CHECK (length2 (position_value (live_state.vehicle.position) -
                        position_value (checkpoint.vehicle.position)) > 1.0f);
  MOPPE_CHECK (live_state.logic.m_mode == game::M_BIKE);
  MOPPE_CHECK (live_state.logic.m_cam_mode == game::CAM_FRONT);
  MOPPE_CHECK (live_state.logic.m_fuel < checkpoint.logic.m_fuel);

  game::GameSession replay (world, map, surface);
  replay.restore (checkpoint);
  for (const game::InputFrame& frame : tape)
    game::advance_game_session (context, replay, frame, step);
  const game::GameSession::State replayed = replay.state ();

  MOPPE_CHECK (replayed.logic.m_mode == live_state.logic.m_mode);
  MOPPE_CHECK (replayed.logic.m_cam_mode == live_state.logic.m_cam_mode);
  MOPPE_CHECK_NEAR (replayed.logic.m_fuel, live_state.logic.m_fuel, 1e-6f);
  MOPPE_CHECK_NEAR (
    replayed.logic.m_odometer, live_state.logic.m_odometer, 1e-6f);
  MOPPE_CHECK_NEAR (replayed.logic.m_fov_k, live_state.logic.m_fov_k, 1e-6f);
  check_position (replayed.vehicle.position, live_state.vehicle.position);
  check_velocity (replayed.vehicle.velocity, live_state.vehicle.velocity);
  check_vector (replayed.vehicle.heading, live_state.vehicle.heading);
  check_position (replayed.camera.position, live_state.camera.position);
  check_position (replayed.camera.target, live_state.camera.target);
  check_velocity (replayed.camera.position_velocity,
                  live_state.camera.position_velocity);
  MOPPE_CHECK (replayed.stars.count == live_state.stars.count);
  MOPPE_CHECK (replayed.stars.collected == live_state.stars.collected);
  MOPPE_CHECK (replayed.dust.next_id == live_state.dust.next_id);
  MOPPE_CHECK (replayed.dust.emissions.size () ==
               live_state.dust.emissions.size ());
  MOPPE_CHECK_NEAR (seconds_value (replayed.dust.logical_time),
                    seconds_value (live_state.dust.logical_time),
                    1e-6f);
}
