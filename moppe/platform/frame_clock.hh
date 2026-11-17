#ifndef MOPPE_PLATFORM_FRAME_CLOCK_HH
#define MOPPE_PLATFORM_FRAME_CLOCK_HH

#include <moppe/gfx/math.hh>
#include <moppe/nhal/nhal.hh>

#include <algorithm>

namespace moppe::platform {
  struct FrameStep {
    seconds_f64_t elapsed {};
    bool by_display = false;
  };

  // Display predictions can retreat as their cadence or feedback changes.
  // Never spend wall time on such a correction or consume the same display
  // interval twice. Wall time bridges startup and prediction loss instead.
  class FrameClock {
  public:
    FrameStep select (seconds_f64_t wall, const nhal::FrameTiming& timing) {
      // NHAL's timing is plain seconds on the display's clock.
      const seconds_f64_t display = timing.display_seconds * u::s;
      const bool by_display =
        timing.predicted && m_consumed_display > seconds_f64_t {};
      const seconds_f64_t elapsed =
        by_display ? std::max (seconds_f64_t {}, display - m_consumed_display)
                   : wall;
      // The host still caps elapsed time at 50 ms. Consume the full forward
      // timestamp so discarded time does not become a catch-up backlog.
      m_consumed_display = timing.predicted
                             ? std::max (m_consumed_display, display)
                             : seconds_f64_t {};
      return { elapsed, by_display };
    }

  private:
    // How far into the display's clock frames have been stepped.
    seconds_f64_t m_consumed_display {};
  };
}

#endif
