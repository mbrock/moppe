#include <moppe/terrain/river.hh>

#include <algorithm>
#include <cmath>

namespace moppe::terrain {
  namespace {
    constexpr float width_per_sqrt_m2 = 0.012f;
    constexpr float depth_per_sqrt_m2 = 0.0015f;
  }

  meters_t river_width (square_meters_t contributing_area) noexcept {
    const float area = std::max (0.0f, square_meters_value (contributing_area));
    return std::clamp (width_per_sqrt_m2 * std::sqrt (area), 1.5f, 24.0f) *
           mp_units::si::metre;
  }

  meters_t river_depth (square_meters_t contributing_area) noexcept {
    const float area = std::max (0.0f, square_meters_value (contributing_area));
    return std::clamp (depth_per_sqrt_m2 * std::sqrt (area), 0.4f, 2.5f) *
           mp_units::si::metre;
  }

  square_meters_t
  visible_river_minimum_area (const TerrainGrid& grid) noexcept {
    constexpr float cells_across = 2.0f / width_per_sqrt_m2;
    return cells_across * cells_across * grid.cell_area ();
  }
}
