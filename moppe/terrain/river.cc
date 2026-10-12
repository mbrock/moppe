#include <moppe/terrain/river.hh>

#include <algorithm>
#include <cmath>

namespace moppe::terrain {
  namespace {
    // Cubic metres a second from each square metre of catchment: 2.5 from a
    // square kilometre.
    constexpr float discharge_per_m2 = 2.5e-6f;
    // W = 7.59 sqrt(Q), which is 0.012 sqrt(A): a wide gravel bed.
    constexpr float width_per_sqrt_m2 = 0.012f;
    constexpr float depth_per_sqrt_m2 = 0.0015f;
    constexpr float visible_river_width_m = 5.0f;
  }

  cubic_meters_per_second_t
  river_discharge (square_meters_t contributing_area) noexcept {
    const float area = std::max (
      0.0f, (contributing_area).numerical_value_in (moppe::u::m * moppe::u::m));
    return discharge_per_m2 * area * mp_units::si::metre * mp_units::si::metre *
           mp_units::si::metre / mp_units::si::second;
  }

  meters_t river_width (square_meters_t contributing_area) noexcept {
    const float area = std::max (
      0.0f, (contributing_area).numerical_value_in (moppe::u::m * moppe::u::m));
    return std::clamp (width_per_sqrt_m2 * std::sqrt (area), 1.5f, 60.0f) *
           mp_units::si::metre;
  }

  meters_t river_depth (square_meters_t contributing_area) noexcept {
    const float area = std::max (
      0.0f, (contributing_area).numerical_value_in (moppe::u::m * moppe::u::m));
    return std::clamp (depth_per_sqrt_m2 * std::sqrt (area), 0.4f, 2.5f) *
           mp_units::si::metre;
  }

  square_meters_t visible_river_minimum_area () noexcept {
    constexpr float square_root_area_m =
      visible_river_width_m / width_per_sqrt_m2;
    return square_root_area_m * square_root_area_m * mp_units::si::metre *
           mp_units::si::metre;
  }
}
