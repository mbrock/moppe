#include <moppe/game/chase_camera.hh>
#include <moppe/game/simulation_clock.hh>
#include <moppe/map/surface.hh>
#include <moppe/mov/glider.hh>

#include <tests/test.hh>

#include <algorithm>
#include <cmath>
#include <limits>

namespace {
  struct FlightReading {
    moppe::mov::Glider::State glider;
    moppe::game::ChaseCamera::State camera;
  };

  FlightReading run_fixed_flight (float callback_interval, int callback_count) {
    using namespace moppe;
    map::SurfaceGeometry surface =
      map::SurfaceGeometry (terrain::TerrainDomain (
        17, 17, spatial_extent_in_metres (Vec3 (200, 0, 200))));
    std::ranges::fill (spatial::get<terrain::surface_elevation> (surface),
                       terrain::surface_elevation_point (10.0f * u::m));
    map::rebuild_geometry (surface);

    mov::Glider glider (surface);
    glider.launch (
      position (Vec3 (80, 70, 80)), velocity (Vec3 (0, 0, 18)), Vec3 (0, 0, 1));
    glider.set_turn (0.6f);
    game::ChaseCamera camera (18 * u::deg, 6.5f * u::m);
    game::SimulationClock clock;
    for (int callback = 0; callback < callback_count; ++callback) {
      const int steps = clock.consume (callback_interval);
      for (int step = 0; step < steps; ++step) {
        const seconds_t dt =
          seconds (static_cast<float> (game::FIXED_SIMULATION_STEP_SECONDS));
        glider.update (dt);
        camera.update (glider.physical_position (),
                       glider.heading (),
                       glider.physical_velocity (),
                       dt);
        camera.limit (surface);
      }
    }
    return { glider.state (), camera.state () };
  }

  void check_vec_near (const moppe::Vec3& left,
                       const moppe::Vec3& right,
                       float tolerance) {
    MOPPE_CHECK_NEAR (left[0], right[0], tolerance);
    MOPPE_CHECK_NEAR (left[1], right[1], tolerance);
    MOPPE_CHECK_NEAR (left[2], right[2], tolerance);
  }
}

MOPPE_TEST (simulation_clock_matches_common_display_cadences) {
  using namespace moppe::game;
  SimulationClock clock;

  MOPPE_CHECK (clock.consume (1.0f / 120.0f) == 1);
  MOPPE_CHECK (clock.consume (1.0f / 60.0f) == 2);
  MOPPE_CHECK (clock.consume (1.0f / 40.0f) == 3);
  MOPPE_CHECK (std::abs (clock.phase_seconds ()) < 1e-8);
}

MOPPE_TEST (simulation_clock_phase_locks_jitter_around_120_hz) {
  using namespace moppe::game;
  SimulationClock clock;

  MOPPE_CHECK (clock.consume (0.0076f) == 1);
  MOPPE_CHECK (clock.consume (1.0f / 60.0f - 0.0076f) == 1);
  MOPPE_CHECK (std::abs (clock.phase_seconds ()) < 1e-7);
}

MOPPE_TEST (simulation_clock_bounds_delayed_and_invalid_callbacks) {
  using namespace moppe::game;
  SimulationClock clock;

  MOPPE_CHECK (clock.consume (0.5f) == MAX_SIMULATION_CATCH_UP_STEPS);
  clock.reset ();
  MOPPE_CHECK (clock.consume (0.0f) == 0);
  MOPPE_CHECK (clock.consume (-1.0f) == 0);
  MOPPE_CHECK (clock.consume (std::numeric_limits<float>::quiet_NaN ()) == 0);
  MOPPE_CHECK (clock.phase_seconds () == 0.0);
}

MOPPE_TEST (fixed_glider_and_camera_motion_matches_at_60_and_120_callbacks) {
  const FlightReading at_60 = run_fixed_flight (1.0f / 60.0f, 60);
  const FlightReading at_120 = run_fixed_flight (1.0f / 120.0f, 120);

  check_vec_near (moppe::position_value (at_60.glider.position),
                  moppe::position_value (at_120.glider.position),
                  1e-5f);
  check_vec_near (at_60.glider.heading, at_120.glider.heading, 1e-6f);
  check_vec_near (moppe::position_value (at_60.camera.position),
                  moppe::position_value (at_120.camera.position),
                  1e-5f);
  check_vec_near (moppe::position_value (at_60.camera.target),
                  moppe::position_value (at_120.camera.target),
                  1e-5f);
}
