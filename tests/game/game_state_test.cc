#include <moppe/game/game_session.hh>
#include <moppe/game/game_state.hh>
#include <moppe/game/input_frame_adapter.hh>
#include <moppe/map/surface.hh>

#include <tests/test.hh>

#include <algorithm>
#include <cmath>
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
  map::SurfaceGeometry surface = map::SurfaceGeometry (terrain::TerrainDomain (
    9, 9, spatial_extent_in_metres (Vec3 (100, 0, 100))));
  for (int y = 0; y < static_cast<int> (surface.domain ().height ()); ++y)
    for (int x = 0; x < static_cast<int> (surface.domain ().width ()); ++x)
      spatial::get<terrain::surface_elevation> (surface[terrain::TerrainIndex {
        static_cast<std::size_t> (x), static_cast<std::size_t> (y) }]) =
        moppe::terrain::surface_elevation_point (
          (0.05f * static_cast<float> (x + y)) * 20.0f * mp_units::si::metre);
  map::rebuild_geometry (surface);
  mov::Vehicle vehicle (position (Vec3 (20, 0, 20)),
                        15 * u::deg,
                        surface,
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
  MOPPE_CHECK_NEAR ((restored.fall_top).numerical_value_in (moppe::u::m),
                    (saved.fall_top).numerical_value_in (moppe::u::m),
                    1e-6f);
  MOPPE_CHECK_NEAR ((restored.fall_drop).numerical_value_in (moppe::u::m),
                    (saved.fall_drop).numerical_value_in (moppe::u::m),
                    1e-6f);
  MOPPE_CHECK (restored.body_kind == saved.body_kind);
}

MOPPE_TEST (airborne_vehicle_prepares_for_expected_landing_plane) {
  using namespace moppe;
  map::SurfaceGeometry surface = map::SurfaceGeometry (terrain::TerrainDomain (
    17, 17, spatial_extent_in_metres (Vec3 (160, 0, 160))));
  for (int z = 0; z < static_cast<int> (surface.domain ().height ()); ++z)
    for (int x = 0; x < static_cast<int> (surface.domain ().width ()); ++x)
      spatial::get<terrain::surface_elevation> (surface[terrain::TerrainIndex {
        static_cast<std::size_t> (x), static_cast<std::size_t> (z) }]) =
        moppe::terrain::surface_elevation_point ((0.10f + 0.02f * x) * 80.0f *
                                                 mp_units::si::metre);
  map::rebuild_geometry (surface);

  mov::Vehicle vehicle (position (Vec3 (50, 0, 50)),
                        90 * u::deg,
                        surface,
                        1000 * u::N,
                        10 * u::kW,
                        100 * u::kg);
  mov::Vehicle::State flight = vehicle.state ();
  flight.position = position (Vec3 (50, 30, 50));
  flight.velocity = velocity (Vec3 (12, -4, 0));
  flight.heading = Vec3 (1, 0, 0);
  flight.thrust_orientation = flight.heading;
  flight.render_heading = flight.heading;
  flight.render_normal = Vec3 (0, 1, 0);
  flight.airborne_time = seconds (0.3f);
  flight.fall_top = 30 * u::m;
  vehicle.restore (flight);

  vehicle.update (seconds (1.0f / 60.0f));

  // The flight velocity points down, but the expected uphill landing tangent
  // points up. The surface normal also banks toward the downhill side.
  MOPPE_CHECK (vehicle.render_orientation ()[1] > 0.0f);
  MOPPE_CHECK (vehicle.render_normal ()[0] < 0.0f);
}

MOPPE_TEST (air_steering_visibly_whips_the_motocross) {
  using namespace moppe;
  map::SurfaceGeometry surface = map::SurfaceGeometry (terrain::TerrainDomain (
    17, 17, spatial_extent_in_metres (Vec3 (160, 0, 160))));
  std::ranges::fill (
    spatial::get<terrain::surface_elevation> (surface),
    moppe::terrain::surface_elevation_point (10.0f * mp_units::si::metre));
  map::rebuild_geometry (surface);

  mov::Vehicle vehicle (position (Vec3 (50, 30, 50)),
                        0 * u::deg,
                        surface,
                        1000 * u::N,
                        10 * u::kW,
                        100 * u::kg);
  mov::Vehicle::State flight = vehicle.state ();
  flight.position = position (Vec3 (50, 30, 50));
  flight.velocity = velocity (Vec3 (0, 0, 18));
  flight.heading = Vec3 (0, 0, 1);
  flight.thrust_orientation = flight.heading;
  flight.render_heading = flight.heading;
  flight.render_normal = Vec3 (0, 1, 0);
  flight.airborne_time = seconds (1.0f);
  flight.fall_top = 30 * u::m;
  vehicle.restore (flight);
  vehicle.set_yaw (90 * u::deg);

  for (int i = 0; i < 30; ++i)
    vehicle.update (seconds (1.0f / 60.0f));

  MOPPE_CHECK (std::abs (vehicle.orientation ()[0]) > 0.35f);
  MOPPE_CHECK (std::abs (vehicle.render_orientation ()[0]) > 0.25f);
}

MOPPE_TEST (clean_air_whip_banks_points_and_recharges_jump_jets) {
  using namespace moppe;
  map::SurfaceGeometry surface = map::SurfaceGeometry (terrain::TerrainDomain (
    17, 17, spatial_extent_in_metres (Vec3 (160, 0, 160))));
  std::ranges::fill (
    spatial::get<terrain::surface_elevation> (surface),
    moppe::terrain::surface_elevation_point (10.0f * mp_units::si::metre));
  map::rebuild_geometry (surface);
  game::WorldParams world;
  world.map_size = spatial_extent_in_metres (Vec3 (160, 20, 160));
  world.resolution = static_cast<int> (surface.domain ().width ());
  world.water_level = 0 * u::m;
  std::vector<mov::Box> obstacles;
  game::GameSession session (world, surface);

  mov::Vehicle::State flight = session.bike ().state ();
  flight.position = position (Vec3 (50, 18, 50));
  flight.velocity = velocity (Vec3 (0, 0, 15));
  flight.heading = Vec3 (0, 0, 1);
  flight.thrust_orientation = flight.heading;
  flight.render_heading = flight.heading;
  flight.airborne_time = seconds (1.5f);
  flight.fall_top = 18 * u::m;
  flight.boost_charge = 0.2f;
  flight.boost_recharge_delay = seconds (1.0f);
  session.bike ().restore (flight);

  const seconds_t step = seconds (1.0f / 60.0f);
  game::advance_game_session (
    world, surface, obstacles, session, game::InputFrame {}, step);

  flight = session.bike ().state ();
  flight.velocity = velocity (Vec3 (15, 0, 0));
  flight.heading = Vec3 (1, 0, 0);
  flight.thrust_orientation = flight.heading;
  flight.render_heading = flight.heading;
  session.bike ().restore (flight);
  game::advance_game_session (
    world, surface, obstacles, session, game::InputFrame {}, step);
  MOPPE_CHECK_NEAR (
    std::abs (session.logic ().m_jump_spin_radians), 1.5707963f, 1e-4f);
  MOPPE_CHECK_NEAR (
    session.logic ().m_jump_peak_spin_radians, 1.5707963f, 1e-4f);

  flight = session.bike ().state ();
  flight.velocity = velocity (Vec3 (0, 0, 15));
  flight.heading = Vec3 (0, 0, 1);
  flight.thrust_orientation = flight.heading;
  flight.render_heading = flight.heading;
  session.bike ().restore (flight);
  game::advance_game_session (
    world, surface, obstacles, session, game::InputFrame {}, step);
  MOPPE_CHECK_NEAR (
    std::abs (session.logic ().m_jump_spin_radians), 0.0f, 1e-4f);
  MOPPE_CHECK_NEAR (
    session.logic ().m_jump_peak_spin_radians, 1.5707963f, 1e-4f);

  flight = session.bike ().state ();
  flight.position = position (Vec3 (52, 11.05f, 50));
  flight.velocity = velocity (Vec3 (0, -1, 15));
  flight.airborne_time = seconds (1.7f);
  flight.fall_top = 18 * u::m;
  session.bike ().restore (flight);
  game::advance_game_session (
    world, surface, obstacles, session, game::InputFrame {}, seconds (0.1f));

  MOPPE_CHECK (session.logic ().m_landed_clean);
  MOPPE_CHECK_NEAR (session.logic ().m_landed_spin_degrees, 90.0f, 1e-3f);
  MOPPE_CHECK (session.logic ().m_landed_points > 350);
  MOPPE_CHECK (session.logic ().m_score == session.logic ().m_landed_points);
  MOPPE_CHECK (session.bike ().boost_charge () > 0.3f);
  MOPPE_CHECK (!session.dust ().state ().emissions.empty ());
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
  map::SurfaceGeometry surface = map::SurfaceGeometry (terrain::TerrainDomain (
    17, 17, spatial_extent_in_metres (Vec3 (200, 0, 200))));
  std::ranges::fill (spatial::get<terrain::surface_elevation> (surface),
                     moppe::terrain::surface_elevation_point (
                       (0.5f) * 20.0f * mp_units::si::metre));
  map::rebuild_geometry (surface);

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
  map::SurfaceGeometry surface = map::SurfaceGeometry (terrain::TerrainDomain (
    17, 17, spatial_extent_in_metres (Vec3 (200, 0, 200))));
  std::ranges::fill (spatial::get<terrain::surface_elevation> (surface),
                     moppe::terrain::surface_elevation_point (
                       (0.35f) * 20.0f * mp_units::si::metre));
  map::rebuild_geometry (surface);

  mov::Glider glider (surface);
  glider.launch (position (Vec3 (40, 70, 40)),
                 velocity (Vec3 (12, -1, 15)),
                 Vec3 (0, 0, 1),
                 true);
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
  MOPPE_CHECK (restored.bike_attached == saved.bike_attached);
  MOPPE_CHECK (restored.landed == saved.landed);
}

MOPPE_TEST (dropping_bike_reduces_glider_wing_loading) {
  using namespace moppe;
  map::SurfaceGeometry surface = map::SurfaceGeometry (terrain::TerrainDomain (
    17, 17, spatial_extent_in_metres (Vec3 (200, 0, 200))));
  std::ranges::fill (spatial::get<terrain::surface_elevation> (surface),
                     moppe::terrain::surface_elevation_point (
                       (0.5f) * 20.0f * mp_units::si::metre));
  map::rebuild_geometry (surface);

  mov::Glider loaded (surface);
  loaded.launch (position (Vec3 (80, 100, 80)),
                 velocity (Vec3 (0, 0, 22)),
                 Vec3 (0, 0, 1),
                 true);
  mov::Glider light (surface);
  light.restore (loaded.state ());
  MOPPE_CHECK (light.drop_bike ());

  for (int i = 0; i < 480; ++i) {
    loaded.update (seconds (1.0f / 60.0f));
    light.update (seconds (1.0f / 60.0f));
  }

  MOPPE_CHECK (loaded.bike_attached ());
  MOPPE_CHECK (!light.bike_attached ());
  MOPPE_CHECK (loaded.airspeed () > light.airspeed ());
  MOPPE_CHECK (loaded.vertical_speed () < light.vertical_speed ());
  MOPPE_CHECK (loaded.position ()[1] < light.position ()[1]);
}

MOPPE_TEST (deploying_glider_carries_then_drops_motocross) {
  using namespace moppe;
  map::SurfaceGeometry surface = map::SurfaceGeometry (terrain::TerrainDomain (
    17, 17, spatial_extent_in_metres (Vec3 (200, 0, 200))));
  std::ranges::fill (spatial::get<terrain::surface_elevation> (surface),
                     moppe::terrain::surface_elevation_point (
                       (0.5f) * 20.0f * mp_units::si::metre));
  map::rebuild_geometry (surface);
  game::WorldParams world;
  world.map_size = spatial_extent_in_metres (Vec3 (200, 20, 200));
  world.resolution = static_cast<int> (surface.domain ().width ());
  world.water_level = 0 * u::m;
  std::vector<mov::Box> obstacles;
  game::GameSession session (world, surface);

  game::InputFrame held;
  held.deploy_glider_held = true;
  game::advance_game_session (
    world, surface, obstacles, session, held, seconds (1.0f / 60.0f));
  MOPPE_CHECK (session.logic ().m_mode == game::M_BIKE);

  mov::Vehicle::State airborne = session.bike ().state ();
  airborne.position = position (Vec3 (80, 40, 80));
  airborne.velocity = velocity (Vec3 (0, 1, 20));
  airborne.heading = Vec3 (0, 0, 1);
  airborne.thrust_orientation = airborne.heading;
  airborne.airborne_time = seconds (0.3f);
  airborne.fall_top = 40 * u::m;
  session.bike ().restore (airborne);

  game::advance_game_session (
    world, surface, obstacles, session, held, seconds (1.0f / 60.0f));

  MOPPE_CHECK (session.logic ().m_mode == game::M_GLIDER);
  MOPPE_CHECK (session.glider ().bike_attached ());
  MOPPE_CHECK (session.can_drop_bike ());
  MOPPE_CHECK_NEAR (
    length (session.glider ().position () - session.bike ().position ()),
    2.4f,
    1e-4f);

  game::advance_game_session (
    world, surface, obstacles, session, held, seconds (1.0f / 60.0f));
  MOPPE_CHECK (session.glider ().bike_attached ());

  game::InputFrame drop;
  drop.deploy_glider = true;
  game::advance_game_session (
    world, surface, obstacles, session, drop, seconds (1.0f / 60.0f));
  const Vec3 dropped_position = session.bike ().position ();

  MOPPE_CHECK (session.logic ().m_mode == game::M_GLIDER);
  MOPPE_CHECK (!session.glider ().bike_attached ());
  MOPPE_CHECK (!session.can_drop_bike ());

  for (int i = 0; i < 60; ++i)
    game::advance_game_session (world,
                                surface,
                                obstacles,
                                session,
                                game::InputFrame {},
                                seconds (1.0f / 60.0f));
  MOPPE_CHECK (session.bike ().position ()[1] < dropped_position[1]);
  MOPPE_CHECK (length (session.glider ().position () -
                       session.bike ().position ()) > 2.0f);
}

MOPPE_TEST (star_state_restores_attraction_and_respawn_state) {
  using namespace moppe;
  map::SurfaceGeometry surface = map::SurfaceGeometry (terrain::TerrainDomain (
    17, 17, spatial_extent_in_metres (Vec3 (100, 0, 100))));
  std::ranges::fill (spatial::get<terrain::surface_elevation> (surface),
                     moppe::terrain::surface_elevation_point (
                       (0.5f) * 20.0f * mp_units::si::metre));
  map::rebuild_geometry (surface);
  game::WorldParams world;
  world.map_size = spatial_extent_in_metres (Vec3 (100, 20, 100));
  world.water_level = 0 * u::m;
  game::Stars stars;
  stars.generate (surface, world, 8);
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
  map::SurfaceGeometry surface = map::SurfaceGeometry (terrain::TerrainDomain (
    17, 17, spatial_extent_in_metres (Vec3 (200, 0, 200))));
  std::ranges::fill (spatial::get<terrain::surface_elevation> (surface),
                     moppe::terrain::surface_elevation_point (
                       (0.5f) * 20.0f * mp_units::si::metre));
  map::rebuild_geometry (surface);
  game::WorldParams world;
  world.map_size = spatial_extent_in_metres (Vec3 (200, 20, 200));
  world.water_level = 0 * u::m;

  static_assert (!std::is_copy_constructible_v<game::GameSession>);
  static_assert (!std::is_copy_assignable_v<game::GameSession>);
  static_assert (!std::is_move_constructible_v<game::GameSession>);
  static_assert (!std::is_move_assignable_v<game::GameSession>);

  game::GameSession session (world, surface);
  session.logic ().m_total_time = 12.5;
  session.logic ().m_score = 400;
  session.bike ().set_thrust (0.8f);
  session.car ().set_thrust (-0.4f);
  session.glider ().launch (
    position (Vec3 (80, 70, 80)), velocity (Vec3 (8, 1, 14)), Vec3 (0, 0, 1));
  session.glider ().set_turn (0.6f);
  session.walker ().spawn (position (Vec3 (3, 4, 5)), Vec3 (1, 0, 0));
  session.camera ().place (Vec3 (8, 9, 10), Vec3 (9, 9, 10));
  session.stars ().generate (surface, world, 4);
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
  game::GameSession replacement (world, surface);
  replacement.stars ().generate (surface, world, 4);
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
    game::GameSessionAdvanceResult (*) (const game::WorldParams&,
                                        const map::SurfaceGeometry&,
                                        const std::vector<mov::Box>&,
                                        game::GameSession&,
                                        const game::InputFrame&,
                                        seconds_t);
  static_assert (
    std::is_same_v<decltype (&game::advance_game_session), AdvanceGameSession>);

  map::SurfaceGeometry surface = map::SurfaceGeometry (terrain::TerrainDomain (
    17, 17, spatial_extent_in_metres (Vec3 (200, 0, 200))));
  std::ranges::fill (spatial::get<terrain::surface_elevation> (surface),
                     moppe::terrain::surface_elevation_point (
                       (0.5f) * 20.0f * mp_units::si::metre));
  map::rebuild_geometry (surface);
  game::WorldParams world;
  world.map_size = spatial_extent_in_metres (Vec3 (200, 20, 200));
  world.resolution = static_cast<int> (surface.domain ().width ());
  world.water_level = 0 * u::m;
  std::vector<mov::Box> obstacles;

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
  game::GameSession live (world, surface);
  const game::GameSession::State checkpoint = live.state ();
  for (const game::InputFrame& frame : tape)
    game::advance_game_session (world, surface, obstacles, live, frame, step);
  const game::GameSession::State live_state = live.state ();

  // This must be more than a checkpoint round trip: the recorded keyboard
  // tape drove and steered the bike, while its Tab edge changed the camera.
  MOPPE_CHECK (length2 (position_value (live_state.vehicle.position) -
                        position_value (checkpoint.vehicle.position)) > 1.0f);
  MOPPE_CHECK (live_state.logic.m_mode == game::M_BIKE);
  MOPPE_CHECK (live_state.logic.m_cam_mode == game::CAM_FRONT);
  MOPPE_CHECK (live_state.logic.m_odometer > checkpoint.logic.m_odometer);

  game::GameSession replay (world, surface);
  replay.restore (checkpoint);
  for (const game::InputFrame& frame : tape)
    game::advance_game_session (world, surface, obstacles, replay, frame, step);
  const game::GameSession::State replayed = replay.state ();

  MOPPE_CHECK (replayed.logic.m_mode == live_state.logic.m_mode);
  MOPPE_CHECK (replayed.logic.m_cam_mode == live_state.logic.m_cam_mode);
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
