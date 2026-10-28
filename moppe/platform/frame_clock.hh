#ifndef MOPPE_PLATFORM_FRAME_CLOCK_HH
#define MOPPE_PLATFORM_FRAME_CLOCK_HH

#include <moppe/nhal/nhal.hh>

#include <algorithm>

namespace moppe::platform {
  struct FrameStep {
    double elapsed_seconds = 0;
    bool by_display = false;
  };

  // Display predictions can retreat as their cadence or feedback changes.
  // Never spend wall time on such a correction or consume the same display
  // interval twice. Wall time bridges startup and prediction loss instead.
  class FrameClock {
  public:
    FrameStep select (double wall_seconds, const nhal::FrameTiming& timing) {
      const bool by_display = timing.predicted && m_consumed_display > 0;
      const double elapsed =
        by_display ? std::max (0.0, timing.display_seconds - m_consumed_display)
                   : wall_seconds;
      // The host still caps elapsed time at 50 ms. Consume the full forward
      // timestamp so discarded time does not become a catch-up backlog.
      m_consumed_display =
        timing.predicted ? std::max (m_consumed_display, timing.display_seconds)
                         : 0;
      return { elapsed, by_display };
    }

  private:
    double m_consumed_display = 0;
  };
}

#endif
