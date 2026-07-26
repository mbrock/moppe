#pragma once

#include "lavoir/buffer.hh"
#include "lavoir/bundle.hh"
#include "lavoir/units.hh"

#include <cmath>
#include <cstdint>
#include <stdexcept>

/// The workshop's first field doing physics: a one-dimensional water
/// surface as displacement columns of metres over a periodic interval,
/// stepped by the leapfrog wave equation. Leapfrog keeps three storage
/// levels -- next is computed from current and previous -- and the
/// frame pacing keeps three frames in flight, so one ring of three
/// columns serves both disciplines at once: the level being written is
/// never a level the GPU may still be reading, by construction rather
/// than by fence.
///
/// The water runs on its own discrete axis. Each tick advances one
/// step; the pace (seconds per step) is fixed at construction, and
/// stability is a typed fact checked there: the Courant number
/// c · pace / spacing is dimensionless, and leapfrog demands it stay
/// below one.

namespace lavoir {
  using metres_q = quantity<si::metre, float>;
  using wave_speed_q = quantity<si::metre / si::second, float>;

  /// A lattice spacing is honestly a rate: metres of world per unit of
  /// cardinality, the spatial sibling of seconds per step.
  using site_spacing_q = quantity<si::metre / unit, float>;

  inline constexpr std::size_t wave_levels = 3;

  class water_line {
  public:
    water_line (interval sites,
                site_spacing_q spacing,
                wave_speed_q speed,
                step_pace_t pace)
        : m_sites (sites), m_spacing (spacing), m_pace (pace) {
      const auto courant = speed * (pace * (1 * step)) / (spacing * (1 * unit));
      m_courant = courant.numerical_value_in (one);
      if (!(m_courant > 0.0 && m_courant < 1.0))
        throw std::invalid_argument (
          "The water line's Courant number must sit inside (0, 1)");

      for (auto& level : m_levels)
        level = column<metres_q>::with_count (sites.size () * unit);
    }

    const interval& sites () const {
      return m_sites;
    }

    steps_t age () const {
      return m_age;
    }

    step_pace_t pace () const {
      return m_pace;
    }

    std::size_t level () const {
      return m_level;
    }

    column<metres_q>& level_column (std::size_t index) {
      return m_levels[index];
    }

    /// The displacement field at the current level.
    field<interval, const metres_q> surface () const {
      return { m_sites, m_levels[m_level].lease () };
    }

    /// Advance one step along the water's own axis: leapfrog from the
    /// current and previous levels into the eldest, which becomes
    /// current. Now and then, rain: a splash at a site derived from
    /// the step count, so the history is the seed of its own weather.
    void tick () {
      const std::size_t previous = (m_level + wave_levels - 1) % wave_levels;
      const std::size_t next = (m_level + 1) % wave_levels;

      const auto u = m_levels[m_level].lease ();
      const auto u_prev = m_levels[previous].lease ();
      const auto u_next = m_levels[next].lease ();

      const std::size_t count = m_sites.size ();
      const float c2 = static_cast<float> (m_courant * m_courant);
      constexpr float keel = 0.9975f;
      constexpr float syrup = 0.06f;

      for (std::size_t i = 0; i < count; ++i) {
        const std::size_t left = (i + count - 1) % count;
        const std::size_t right = (i + 1) % count;
        const metres_q bend = u[left] - 2.0f * u[i] + u[right];
        u_next[i] = keel * (2.0f * u[i] - u_prev[i] + c2 * bend) + syrup * bend;
      }

      m_level = next;
      m_age += 1 * step;
      sprinkle ();
    }

  private:
    /// Deterministic rain: every so many steps, a raindrop lands at a
    /// site and with a strength derived by hashing the step count.
    void sprinkle () {
      const auto ticks =
        static_cast<std::uint64_t> (m_age.numerical_value_in (step));
      if (ticks % 131 != 0)
        return;

      std::uint64_t h = ticks * 0x9e3779b97f4a7c15ull;
      h ^= h >> 32;
      const std::size_t centre = h % m_sites.size ();
      const metres_q strength =
        (0.004f + 0.007f * static_cast<float> ((h >> 8) % 997) / 997.0f) *
        si::metre;

      const auto u = m_levels[m_level].lease ();
      const std::size_t count = m_sites.size ();
      constexpr int reach = 24;
      for (int offset = -reach; offset <= reach; ++offset) {
        const std::size_t site =
          (centre + count + static_cast<std::size_t> (offset + reach) - reach) %
          count;
        const float shape =
          std::exp (-0.5f * static_cast<float> (offset * offset) / 64.0f);
        u[site] -= strength * shape;
      }
    }

    interval m_sites;
    site_spacing_q m_spacing;
    step_pace_t m_pace;
    double m_courant = 0.0;
    std::array<column<metres_q>, wave_levels> m_levels;
    std::size_t m_level = 0;
    steps_t m_age = steps_t::zero ();
  };

  static_assert (sizeof (metres_q) == sizeof (float),
                 "The GPU reads a metres column as raw floats: the "
                 "quantity must be bitwise its representation");
}
