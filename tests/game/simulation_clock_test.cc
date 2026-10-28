#include <moppe/game/chase_camera.hh>
#include <moppe/game/simulation_clock.hh>
#include <moppe/map/surface.hh>
#include <moppe/mov/glider.hh>
#include <moppe/nhal/presentation.hh>
#include <moppe/platform/frame_clock.hh>

#include <tests/test.hh>

#include <algorithm>
#include <cmath>
#include <cstdint>
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
        camera.limit (surface, dt);
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

  int consume_frame (moppe::game::SimulationClock& simulation,
                     const moppe::platform::FrameStep& step) {
    // The SDL host caps the selected interval before passing it to the game.
    return simulation.consume (
      static_cast<float> (std::clamp (step.elapsed_seconds, 0.0, 0.05)));
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

MOPPE_TEST (frame_clock_does_not_double_count_delayed_feedback) {
  using namespace moppe;
  constexpr double h = game::FIXED_SIMULATION_STEP_SECONDS;
  nhal::PresentationClock presentation;
  presentation.set_refresh (h);
  for (std::uint64_t serial = 1; serial <= 5; ++serial)
    presentation.presented (serial, 1.0 + 2 * (serial - 1) * h);

  platform::FrameClock frame;
  game::SimulationClock simulation;
  const nhal::FrameTiming anchor = presentation.predict (8, 1.0 + 8.25 * h);
  MOPPE_CHECK (anchor.predicted);
  const platform::FrameStep first = frame.select (0.0, anchor);
  MOPPE_CHECK (!first.by_display);
  MOPPE_CHECK (consume_frame (simulation, first) == 0);

  // Presents accelerate from 60 to 120 Hz, but frame 6's feedback is late.
  const nhal::FrameTiming ahead = presentation.predict (9, 1.0 + 9.25 * h);
  MOPPE_CHECK (ahead.predicted);
  MOPPE_CHECK (std::abs (ahead.display_seconds - (1.0 + 16 * h)) < 1e-12);
  const platform::FrameStep advance = frame.select (h, ahead);
  int steps = consume_frame (simulation, advance);
  MOPPE_CHECK (advance.by_display);
  MOPPE_CHECK (steps == 2);

  presentation.presented (6, 1.0 + 9 * h);
  presentation.presented (7, 1.0 + 10 * h);
  const nhal::FrameTiming caught_up =
    presentation.predict (10, 1.0 + 10.25 * h);
  MOPPE_CHECK (caught_up.predicted);
  MOPPE_CHECK (std::abs (caught_up.display_seconds - ahead.display_seconds) <
               1e-12);
  const platform::FrameStep correction = frame.select (h, caught_up);
  steps += consume_frame (simulation, correction);
  // Two refreshes elapsed, and the prediction advanced by two refreshes.
  // Spending wall time on the duplicate would advance three instead.
  MOPPE_CHECK (steps == 2);
  MOPPE_CHECK (correction.by_display);
  MOPPE_CHECK (std::abs (correction.elapsed_seconds) < 1e-12);

  presentation.presented (8, 1.0 + 11 * h);
  const nhal::FrameTiming next = presentation.predict (11, 1.0 + 11.25 * h);
  MOPPE_CHECK (next.predicted);
  const platform::FrameStep resumed = frame.select (h, next);
  MOPPE_CHECK (resumed.by_display);
  MOPPE_CHECK (consume_frame (simulation, resumed) == 1);
}

MOPPE_TEST (frame_clock_consumes_cadence_corrections_once) {
  using namespace moppe;
  constexpr double h = game::FIXED_SIMULATION_STEP_SECONDS;
  // Two frames of lookahead produce a duplicate when the median changes;
  // three produce a backward correction, then a partial recovery step.
  for (std::uint64_t lag = 2; lag <= 3; ++lag) {
    nhal::PresentationClock presentation;
    presentation.set_refresh (h);
    for (std::uint64_t serial = 1; serial <= 32; ++serial)
      presentation.presented (serial, 1.0 + 2 * (serial - 1) * h);

    platform::FrameClock frame;
    game::SimulationClock simulation;
    const nhal::FrameTiming anchor =
      presentation.predict (32 + lag, 1.0 + 62.25 * h);
    MOPPE_CHECK (anchor.predicted);
    const platform::FrameStep first = frame.select (0.0, anchor);
    MOPPE_CHECK (!first.by_display);
    MOPPE_CHECK (consume_frame (simulation, first) == 0);

    int ticks = 62, total_steps = 0;
    double wall_total = 0, elapsed_total = 0;
    double previous_display = anchor.display_seconds;
    // Sixteen 40 Hz intervals, then sixteen 60 Hz intervals, change the
    // 31-interval median each way. One last frame finishes the recovery.
    for (std::uint64_t serial = 33; serial <= 65; ++serial) {
      const int wall_ticks = serial <= 48 ? 3 : 2;
      ticks += wall_ticks;
      presentation.presented (serial, 1.0 + ticks * h);
      const nhal::FrameTiming timing =
        presentation.predict (serial + lag, 1.0 + (ticks + 0.25) * h);
      MOPPE_CHECK (timing.predicted);
      if (serial == 64) {
        const double correction = (2.0 - double (lag)) * h;
        MOPPE_CHECK (std::abs (timing.display_seconds - previous_display -
                               correction) < 1e-12);
      }
      previous_display = timing.display_seconds;

      const platform::FrameStep step = frame.select (wall_ticks * h, timing);
      int expected_steps = wall_ticks;
      if (serial == 48)
        expected_steps += static_cast<int> (lag);
      else if (serial == 64)
        expected_steps = 0;
      else if (serial == 65)
        expected_steps = 4 - static_cast<int> (lag);
      const int steps = consume_frame (simulation, step);
      MOPPE_CHECK (steps == expected_steps);
      MOPPE_CHECK (step.by_display);
      MOPPE_CHECK (std::abs (step.elapsed_seconds - expected_steps * h) <
                   1e-12);
      total_steps += steps;
      wall_total += wall_ticks * h;
      elapsed_total += step.elapsed_seconds;
    }
    MOPPE_CHECK (total_steps == 82);
    MOPPE_CHECK (std::abs (elapsed_total - wall_total) < 1e-12);
    MOPPE_CHECK (std::abs (simulation.phase_seconds ()) < 1e-7);
  }
}

MOPPE_TEST (frame_clock_preserves_jitter_fallback_and_catch_up_policy) {
  using namespace moppe;
  constexpr double h = game::FIXED_SIMULATION_STEP_SECONDS;
  struct Sample {
    double wall, display;
    bool predicted;
    double elapsed;
    bool by_display;
    int steps;
  };
  const Sample samples[] = {
    // No prediction, then the first prediction: both spend wall time.
    { h, 0, false, h, false, 1 },
    { 2 * h, 1.0, true, 2 * h, false, 2 },
    // 8.33/25 ms loop jitter still produces two 16.67 ms display steps.
    { h, 1.0 + 2 * h, true, 2 * h, true, 2 },
    { 3 * h, 1.0 + 4 * h, true, 2 * h, true, 2 },
    // Losing the prediction resets the anchor, even if it returns earlier.
    { 3 * h, 0, false, 3 * h, false, 3 },
    { h, 0.5, true, h, false, 1 },
    { 3 * h, 0.5 + 2 * h, true, 2 * h, true, 2 },
    // Cap 58.33 ms at six steps; do not replay the discarded refresh.
    { 2 * h, 0.5 + 9 * h, true, 7 * h, true, 6 },
    { 2 * h, 0.5 + 11 * h, true, 2 * h, true, 2 },
    // The same 50 ms cap applies during wall fallback.
    { 0.5, 0, false, 0.5, false, 6 },
  };
  platform::FrameClock frame;
  game::SimulationClock simulation;
  for (const Sample& sample : samples) {
    const platform::FrameStep step =
      frame.select (sample.wall, { sample.display, h, sample.predicted });
    MOPPE_CHECK (std::abs (step.elapsed_seconds - sample.elapsed) < 1e-12);
    MOPPE_CHECK (step.by_display == sample.by_display);
    MOPPE_CHECK (consume_frame (simulation, step) == sample.steps);
  }
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
