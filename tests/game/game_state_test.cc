#include <moppe/game/game_state.hh>
#include <moppe/map/generate.hh>

#include <tests/test.hh>

#include <algorithm>
#include <type_traits>

namespace {
  void check_vector (const moppe::Vector3D& actual,
                     const moppe::Vector3D& expected) {
    MOPPE_CHECK_NEAR (actual.x, expected.x, 1e-6f);
    MOPPE_CHECK_NEAR (actual.y, expected.y, 1e-6f);
    MOPPE_CHECK_NEAR (actual.z, expected.z, 1e-6f);
  }
}

MOPPE_TEST (vehicle_state_restores_hidden_simulation_state) {
  using namespace moppe;
  map::RandomHeightMap map (
    9, 9, Vector3D (100, 20, 100), 1, terrain::Topology::Torus);
  map.randomize_geologically ();
  map.recompute_normals ();
  mov::Vehicle vehicle (Vector3D (20, 0, 20), 15, map, 1000, 10000, 100);
  vehicle.set_thrust (0.8f);
  vehicle.set_yaw (25.0f);
  vehicle.set_boost (0.7f, 0.5f);
  vehicle.update (1.0f / 60.0f);
  const mov::Vehicle::State saved = vehicle.state ();

  vehicle.set_thrust (-1.0f);
  vehicle.set_yaw (-70.0f);
  vehicle.update (0.5f);
  vehicle.restore (saved);
  const mov::Vehicle::State restored = vehicle.state ();

  check_vector (restored.position, saved.position);
  check_vector (restored.velocity, saved.velocity);
  check_vector (restored.heading, saved.heading);
  check_vector (restored.thrust_orientation, saved.thrust_orientation);
  check_vector (restored.render_heading, saved.render_heading);
  check_vector (restored.render_normal, saved.render_normal);
  check_vector (restored.body_color, saved.body_color);
  MOPPE_CHECK_NEAR (restored.yaw, saved.yaw, 1e-6f);
  MOPPE_CHECK_NEAR (restored.yaw_target, saved.yaw_target, 1e-6f);
  MOPPE_CHECK_NEAR (restored.lean, saved.lean, 1e-6f);
  MOPPE_CHECK_NEAR (restored.susp, saved.susp, 1e-6f);
  MOPPE_CHECK_NEAR (restored.susp_v, saved.susp_v, 1e-6f);
  MOPPE_CHECK_NEAR (restored.wheel_spin, saved.wheel_spin, 1e-6f);
  MOPPE_CHECK (restored.boost_flight == saved.boost_flight);
  MOPPE_CHECK_NEAR (restored.thrust, saved.thrust, 1e-6f);
  MOPPE_CHECK_NEAR (restored.boost_input, saved.boost_input, 1e-6f);
  MOPPE_CHECK_NEAR (restored.boost_drive, saved.boost_drive, 1e-6f);
  MOPPE_CHECK_NEAR (restored.boost_level, saved.boost_level, 1e-6f);
  MOPPE_CHECK_NEAR (restored.boost_charge, saved.boost_charge, 1e-6f);
  MOPPE_CHECK_NEAR (
    restored.boost_recharge_delay, saved.boost_recharge_delay, 1e-6f);
  MOPPE_CHECK_NEAR (restored.airborne_time, saved.airborne_time, 1e-6f);
  MOPPE_CHECK_NEAR (restored.impact, saved.impact, 1e-6f);
  MOPPE_CHECK_NEAR (restored.fall_top, saved.fall_top, 1e-6f);
  MOPPE_CHECK_NEAR (restored.fall_drop, saved.fall_drop, 1e-6f);
  MOPPE_CHECK (restored.body_kind == saved.body_kind);
}

MOPPE_TEST (camera_and_walker_state_round_trip) {
  using namespace moppe;
  game::ChaseCamera camera (18, 6.5f);
  camera.update (
    Vector3D (10, 2, 20), Vector3D (0, 0, 1), Vector3D (4, 0, 2), 1.0f / 60.0f);
  const game::ChaseCamera::State camera_state = camera.state ();
  camera.place (Vector3D (100, 100, 100), Vector3D ());
  camera.restore (camera_state);
  check_vector (camera.state ().position, camera_state.position);
  check_vector (camera.state ().target, camera_state.target);
  check_vector (camera.state ().position_velocity,
                camera_state.position_velocity);

  game::Walker walker;
  walker.spawn (Vector3D (3, 4, 5), Vector3D (1, 0, 0));
  walker.set_turn (0.4f);
  walker.set_walk (0.8f);
  const game::Walker::State walker_state = walker.state ();
  walker.spawn (Vector3D (30, 40, 50), Vector3D (0, 0, -1));
  walker.restore (walker_state);
  check_vector (walker.state ().position, walker_state.position);
  check_vector (walker.state ().heading, walker_state.heading);
  MOPPE_CHECK_NEAR (walker.state ().turn, walker_state.turn, 1e-6f);
  MOPPE_CHECK_NEAR (walker.state ().walk, walker_state.walk, 1e-6f);
}

MOPPE_TEST (star_state_restores_attraction_and_respawn_state) {
  using namespace moppe;
  map::RandomHeightMap map (
    17, 17, Vector3D (100, 20, 100), 1, terrain::Topology::Torus);
  std::fill (map.raw_heights (),
             map.raw_heights () + map.width () * map.height (),
             0.5f);
  map.recompute_normals ();
  game::WorldParams world;
  world.map_size = map.size ();
  world.water_level = 0.0f;
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
