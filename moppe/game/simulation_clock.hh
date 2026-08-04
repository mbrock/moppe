#ifndef MOPPE_GAME_SIMULATION_CLOCK_HH
#define MOPPE_GAME_SIMULATION_CLOCK_HH

#include <algorithm>
#include <cmath>

namespace moppe::game {
  inline constexpr double FIXED_SIMULATION_STEP_SECONDS = 1.0 / 120.0;
  inline constexpr int MAX_SIMULATION_CATCH_UP_STEPS = 6;

  // Converts irregular presentation intervals into phase-locked 120 Hz
  // simulation steps. The residual stays within half a step during ordinary
  // pacing, so a slightly early 120 Hz callback does not produce a visible
  // zero-step/two-step pair on the following frame.
  class SimulationClock {
  public:
    int consume (float elapsed_seconds) {
      if (!std::isfinite (elapsed_seconds) || elapsed_seconds <= 0.0f)
        return 0;

      const double maximum_elapsed =
        FIXED_SIMULATION_STEP_SECONDS * MAX_SIMULATION_CATCH_UP_STEPS;
      m_phase_seconds +=
        std::min (static_cast<double> (elapsed_seconds), maximum_elapsed);
      const int requested = static_cast<int> (
        std::floor ((m_phase_seconds + 0.5 * FIXED_SIMULATION_STEP_SECONDS) /
                    FIXED_SIMULATION_STEP_SECONDS));
      const int steps =
        std::clamp (requested, 0, MAX_SIMULATION_CATCH_UP_STEPS);
      m_phase_seconds -= steps * FIXED_SIMULATION_STEP_SECONDS;
      return steps;
    }

    void reset () {
      m_phase_seconds = 0.0;
    }

    double phase_seconds () const {
      return m_phase_seconds;
    }

  private:
    double m_phase_seconds = 0.0;
  };
}

#endif
