#ifndef MOPPE_GAME_SIMULATION_CLOCK_HH
#define MOPPE_GAME_SIMULATION_CLOCK_HH

#include <moppe/game/game_clock.hh>

#include <algorithm>
#include <cmath>

namespace moppe::game {
  inline constexpr duration_t FIXED_SIMULATION_STEP =
    (1.0 / 120.0) * mp_units::si::second;
  inline constexpr int MAX_SIMULATION_CATCH_UP_STEPS = 6;

  // Converts irregular presentation intervals into phase-locked 120 Hz
  // simulation steps. The residual stays within half a step during ordinary
  // pacing, so a slightly early 120 Hz callback does not produce a visible
  // zero-step/two-step pair on the following frame.
  class SimulationClock {
  public:
    int consume (seconds_t elapsed) {
      if (!std::isfinite (seconds_value (elapsed)) ||
          elapsed <= 0.0f * mp_units::si::second)
        return 0;

      const duration_t maximum_elapsed =
        FIXED_SIMULATION_STEP * MAX_SIMULATION_CATCH_UP_STEPS;
      m_phase += std::min (value_cast<double> (elapsed), maximum_elapsed);
      const int requested = static_cast<int> (std::floor (
        ((m_phase + 0.5 * FIXED_SIMULATION_STEP) / FIXED_SIMULATION_STEP)
          .numerical_value_in (mp_units::one)));
      const int steps =
        std::clamp (requested, 0, MAX_SIMULATION_CATCH_UP_STEPS);
      m_phase -= steps * FIXED_SIMULATION_STEP;
      return steps;
    }

    void reset () {
      m_phase = duration_t {};
    }

    duration_t phase () const {
      return m_phase;
    }

  private:
    duration_t m_phase {};
  };
}

#endif
