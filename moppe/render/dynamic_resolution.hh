#ifndef MOPPE_RENDER_DYNAMIC_RESOLUTION_HH
#define MOPPE_RENDER_DYNAMIC_RESOLUTION_HH

#include <moppe/gfx/math.hh>

#include <algorithm>
#include <cmath>

namespace moppe::render {
  // Holds a frame rate by shedding the 3D scene's resolution: told each
  // frame's GPU time and how often frames are due, it yields the share of
  // its ordinary width and height the scene should be drawn at, from three
  // eighths to the whole in thirty-seconds.
  //
  // A frame's cost follows the scene's pixels only in part: the passes at
  // the drawable's size, the shadows, and the vertex work stay. So nothing
  // is modelled. Every few frames the mean GPU time is compared with the
  // budget, a little under the frame's period; over it, or under a quiet
  // band below it, the pixel count is scaled by the ratio of where the
  // time should rest to where it is -- sharply down, gently up -- and so
  // on until the time comes to rest in the band. The band is wider than
  // what one step of share changes, so a steady view settles on a step
  // rather than hunting between two. A changed share takes a few frames
  // to show in the timings, which are passed over.
  class DynamicResolution {
  public:
    // Thirty-seconds of the scene's ordinary size, and the fewest drawn.
    static constexpr int steps = 32, fewest = 12;
    // Frames whose GPU time is averaged before the share may change, and
    // frames already in flight at the old share when it does.
    static constexpr int window = 8, in_flight = 4;
    // The GPU's part of a frame's period; the bottom of the quiet band
    // and where the time is steered to, as parts of that budget.
    static constexpr float headroom = 0.90f, quiet = 0.85f, rest = 0.925f;
    // The most the pixel count changes at once, down and up.
    static constexpr float fall = 0.70f, rise = 1.10f;

    float share () const {
      return float (m_step) / float (steps);
    }

    // One frame's GPU time, where frames are due every `period`.
    void observe (seconds_t gpu_time, seconds_t period) {
      if (!(gpu_time > seconds (0.0f)) || !(period > seconds (0.0f)))
        return;
      if (m_passing > 0) {
        --m_passing;
        return;
      }
      m_sum += gpu_time;
      if (++m_count < window)
        return;
      const seconds_t mean = m_sum / float (m_count);
      m_sum = seconds (0.0f);
      m_count = 0;

      const seconds_t budget = period * headroom;
      const float toward = seconds_value (budget * rest) / seconds_value (mean);
      if (mean > budget)
        m_pixels *= std::max (toward, fall);
      else if (mean < budget * quiet)
        m_pixels *= std::min (toward, rise);
      else
        return;
      const float least = float (fewest * fewest) / float (steps * steps);
      m_pixels = std::clamp (m_pixels, least, 1.0f);
      const int step =
        std::clamp (int (std::lround (std::sqrt (m_pixels) * float (steps))),
                    fewest,
                    steps);
      if (step != m_step) {
        m_step = step;
        m_passing = in_flight;
      }
    }

  private:
    int m_step = steps;
    // The share of the scene's pixels the steps follow.
    float m_pixels = 1.0f;
    seconds_t m_sum = seconds (0.0f);
    int m_count = 0, m_passing = 0;
  };
}

#endif
