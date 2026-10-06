// When frames reach the screen: each device records the moments its
// presentations actually appeared, as its platform reports them (Vulkan's
// present timing, Metal's presented handlers, DXGI's frame statistics), and
// this predicts when the next frame will appear, on the host's steady
// clock. A host that steps its simulation by the difference between
// consecutive predictions moves the world exactly as far as the display
// shows it moving, whatever the loop's own jitter.
#ifndef MOPPE_NHAL_PRESENTATION_HH
#define MOPPE_NHAL_PRESENTATION_HH

#include <moppe/nhal/nhal.hh>

#include <algorithm>
#include <array>
#include <chrono>
#include <cmath>
#include <cstdint>
#include <mutex>

namespace moppe::nhal {
  // Seconds on std::chrono::steady_clock, the host's clock.
  inline double steady_seconds () {
    return std::chrono::duration<double> (
             std::chrono::steady_clock::now ().time_since_epoch ())
      .count ();
  }

  class PresentationClock {
  public:
    // The frame numbered `serial` appeared at `seconds` (steady clock).
    // Thread-safe: presentation reports may arrive on any thread.
    void presented (std::uint64_t serial, double seconds) {
      const std::lock_guard lock (m_mutex);
      if (m_count && serial <= m_seen[(m_next + size - 1) % size].serial)
        return;
      m_seen[m_next] = { serial, seconds };
      m_next = (m_next + 1) % size;
      m_count = std::min (m_count + 1, size);
    }

    // The display's refresh period, when the platform states it.
    void set_refresh (double seconds) {
      const std::lock_guard lock (m_mutex);
      m_refresh = seconds;
    }

    // When frame `serial`, about to be built at `now`, is expected on
    // screen: one frame period after another from the latest observed, and
    // no sooner than a period from now if the loop has fallen behind. The
    // period is the frames' own cadence (a 60 Hz game on a 120 Hz display
    // shows each frame for two refreshes), in whole refreshes when the
    // platform states the refresh.
    FrameTiming predict (std::uint64_t serial, double now) const {
      const std::lock_guard lock (m_mutex);
      if (!m_count)
        return {};
      const Seen& last = m_seen[(m_next + size - 1) % size];
      double period = observed_period ();
      if (m_refresh > 0)
        period = period > 0 ? std::max (1.0, std::round (period / m_refresh))
                                * m_refresh
                            : m_refresh;
      if (!(period > 0) || serial <= last.serial)
        return {};
      double at = last.seconds + double (serial - last.serial) * period;
      const double soonest = now + period;
      if (at < soonest)
        at += std::ceil ((soonest - at) / period) * period;
      return { at, m_refresh > 0 ? m_refresh : period, true };
    }

  private:
    static constexpr std::size_t size = 32;

    struct Seen {
      std::uint64_t serial = 0;
      double seconds = 0;
    };

    // The median interval between consecutive observed frames.
    double observed_period () const {
      std::array<double, size> intervals {};
      std::size_t n = 0;
      for (std::size_t i = 1; i < m_count; ++i) {
        const Seen& a = m_seen[(m_next + size - i - 1) % size];
        const Seen& b = m_seen[(m_next + size - i) % size];
        if (b.serial == a.serial + 1 && b.seconds > a.seconds)
          intervals[n++] = b.seconds - a.seconds;
      }
      if (n < 4)
        return 0;
      std::nth_element (intervals.begin (), intervals.begin () + n / 2,
                        intervals.begin () + n);
      return intervals[n / 2];
    }

    mutable std::mutex m_mutex;
    std::array<Seen, size> m_seen {};
    std::size_t m_next = 0, m_count = 0;
    double m_refresh = 0;
  };
}

#endif
