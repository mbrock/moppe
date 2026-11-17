#include <moppe/terrain/river.hh>

#include <algorithm>
#include <cmath>

namespace moppe::terrain {
  namespace {
    // Cubic metres a second from each square metre of catchment: 2.5 from a
    // square kilometre, a runoff of 2.5 mm a second.
    constexpr quantity<u::m / u::s, float> spate_runoff = 2.5e-6f * u::m / u::s;
    // W = 7.59 sqrt(Q), which is 0.012 sqrt(A): a wide gravel bed. The
    // depth a nickpoint's sheet falls as grows the same way.
    constexpr scale_factor_t width_per_root_area = 0.012f * scale_factor[one];
    constexpr scale_factor_t depth_per_root_area = 0.0015f * scale_factor[one];
    constexpr meters_t visible_river_width = 5.0f * u::m;

    // The catchment's side: the root of its area, never negative.
    meters_t root_area (square_meters_t contributing_area) {
      return sqrt (std::max (0.0f * u::m * u::m, contributing_area));
    }
  }

  cubic_meters_per_second_t
  river_discharge (square_meters_t contributing_area) noexcept {
    return spate_runoff * std::max (0.0f * u::m * u::m, contributing_area);
  }

  meters_t river_width (square_meters_t contributing_area) noexcept {
    return std::clamp (
      meters_t (width_per_root_area * root_area (contributing_area)),
      1.5f * u::m,
      60.0f * u::m);
  }

  meters_t river_depth (square_meters_t contributing_area) noexcept {
    return std::clamp (
      meters_t (depth_per_root_area * root_area (contributing_area)),
      0.4f * u::m,
      2.5f * u::m);
  }

  square_meters_t visible_river_minimum_area () noexcept {
    const meters_t square_root_area (visible_river_width / width_per_root_area);
    return square_root_area * square_root_area;
  }
}
