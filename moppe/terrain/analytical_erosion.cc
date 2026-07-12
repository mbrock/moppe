#include <moppe/terrain/analytical_erosion.hh>

#include <moppe/terrain/drainage.hh>
#include <moppe/terrain/flood.hh>

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <limits>
#include <stdexcept>
#include <vector>

namespace moppe::terrain {
  namespace {
    constexpr std::uint32_t no_donor =
      std::numeric_limits<std::uint32_t>::max ();

    std::vector<float> expand_samples (const TerrainGrid& grid,
                                       const std::vector<float>& unique) {
      const std::size_t width = grid.unique_width ();
      const std::size_t height = grid.unique_height ();
      std::vector<float> expanded (grid.width * grid.height);
      for (std::size_t y = 0; y < grid.height; ++y)
        for (std::size_t x = 0; x < grid.width; ++x)
          expanded[y * grid.width + x] =
            unique[(y % height) * width + x % width];
      return expanded;
    }

    float edge_distance (std::uint32_t cell,
                         std::uint32_t receiver,
                         const TerrainGrid& grid) {
      const int width = static_cast<int> (grid.unique_width ());
      const int height = static_cast<int> (grid.unique_height ());
      int dx =
        static_cast<int> (cell % width) - static_cast<int> (receiver % width);
      int dy =
        static_cast<int> (cell / width) - static_cast<int> (receiver / width);
      if (grid.topology == Topology::Torus) {
        if (dx > width / 2)
          dx -= width;
        if (dx < -width / 2)
          dx += width;
        if (dy > height / 2)
          dy -= height;
        if (dy < -height / 2)
          dy += height;
      }
      return std::hypot (static_cast<float> (dx) * grid.spacing_x_m (),
                         static_cast<float> (dy) * grid.spacing_y_m ());
    }

    void validate (const AnalyticalErosion& p) {
      if (!std::isfinite (julian_years_value (p.duration)) ||
          p.duration < 0.0f * mp_units::astronomy::Julian_year ||
          !std::isfinite (meters_per_julian_year_value (p.uplift_rate)) ||
          p.uplift_rate <
            0.0f * mp_units::si::metre / mp_units::astronomy::Julian_year ||
          !std::isfinite (p.erodibility) || p.erodibility <= 0.0f ||
          !std::isfinite (p.area_exponent) || p.area_exponent < 0.0f ||
          !std::isfinite (p.sea_level) || p.fixed_point_iterations <= 0 ||
          !std::isfinite (p.relaxation) || p.relaxation <= 0.0f ||
          p.relaxation > 1.0f)
        throw std::invalid_argument ("invalid analytical erosion parameters");
    }
  }

  AnalyticalErosionResult
  erode_analytically (const TerrainView& terrain,
                      const AnalyticalErosion& parameters) {
    validate (parameters);
    const TerrainGrid& grid = terrain.grid ();
    const std::size_t width = grid.unique_width ();
    const std::size_t height = grid.unique_height ();
    const std::size_t count = width * height;
    const float cell_area = square_meters_value (grid.cell_area ());
    const float height_scale = grid.height_scale_m ();
    const float duration_years = julian_years_value (parameters.duration);
    const float uplift_rate =
      meters_per_julian_year_value (parameters.uplift_rate);

    std::vector<float> initial (count);
    for (std::size_t y = 0; y < height; ++y)
      for (std::size_t x = 0; x < width; ++x)
        initial[y * width + x] = terrain.at (x, y);
    std::vector<float> current = initial;

    AnalyticalErosionReport report { .cells = cell_count (count),
                                     .fixed_point_iterations =
                                       parameters.fixed_point_iterations };
    for (int iteration = 0;
         iteration < count_value (parameters.fixed_point_iterations);
         ++iteration) {
      const std::vector<float> expanded = expand_samples (grid, current);
      const TerrainView routed_terrain (grid, expanded);
      const FloodField flood =
        analyze_standing_water (routed_terrain, parameters.sea_level);
      const LakeCensus census = census_lakes (flood);
      const DrainageGraph drainage =
        analyze_wet_drainage (routed_terrain, flood, census);

      std::vector<CellIndex> receiver = drainage.receiver;
      std::vector<std::uint8_t> boundary (count, 0);
      report.fixed_boundaries = cell_count (0);
      for (std::uint32_t cell = 0; cell < count; ++cell) {
        if (flood.ocean[cell] || receiver[cell] == cell) {
          receiver[cell] = cell;
          boundary[cell] = 1;
          ++report.fixed_boundaries;
        }
      }

      std::vector<std::uint32_t> first_donor (count, no_donor);
      std::vector<std::uint32_t> next_donor (count, no_donor);
      for (std::uint32_t cell = static_cast<std::uint32_t> (count);
           cell-- > 0;) {
        const std::uint32_t next = receiver[cell];
        if (next == cell)
          continue;
        next_donor[cell] = first_donor[next];
        first_donor[next] = cell;
      }

      std::vector<double> travel (count, 0.0);
      std::vector<double> uplift_integral (count, 0.0);
      std::vector<float> predicted (count, 0.0f);
      std::vector<std::uint8_t> visited (count, 0);
      std::vector<std::uint32_t> path;
      struct Event {
        std::uint32_t cell;
        bool exit;
      };
      std::vector<Event> stack;

      for (std::uint32_t root = 0; root < count; ++root) {
        if (receiver[root] != root)
          continue;
        stack.push_back ({ root, false });
        while (!stack.empty ()) {
          const Event event = stack.back ();
          stack.pop_back ();
          const std::uint32_t cell = event.cell;
          if (event.exit) {
            path.pop_back ();
            continue;
          }

          visited[cell] = 1;
          if (cell != root) {
            const std::uint32_t next = receiver[cell];
            const double distance = edge_distance (cell, next, grid);
            const double area = std::max (
              static_cast<double> (cell_area),
              static_cast<double> (drainage.contributing_area.values ()[cell]));
            const double speed = parameters.erodibility *
                                 std::pow (area, parameters.area_exponent);
            travel[cell] = travel[next] + distance / speed;
            uplift_integral[cell] =
              uplift_integral[next] + distance * uplift_rate / speed;
          }
          path.push_back (cell);

          const double target = travel[cell] - duration_years;
          double advected_height = 0.0;
          double advected_uplift = 0.0;
          if (target <= 0.0 || path.size () == 1) {
            const std::uint32_t origin = path.front ();
            advected_height = initial[origin] * height_scale;
            advected_uplift = uplift_integral[origin];
          } else {
            const auto high =
              std::lower_bound (path.begin (),
                                path.end (),
                                target,
                                [&] (std::uint32_t candidate, double value) {
                                  return travel[candidate] < value;
                                });
            if (high == path.end () || high == path.begin ())
              throw std::logic_error (
                "analytical erosion characteristic left its river path");
            const std::uint32_t high_cell = *high;
            const std::uint32_t low_cell = *(high - 1);
            const double span = travel[high_cell] - travel[low_cell];
            const double alpha = (target - travel[low_cell]) / span;
            advected_height = std::lerp (initial[low_cell] * height_scale,
                                         initial[high_cell] * height_scale,
                                         alpha);
            advected_uplift = std::lerp (
              uplift_integral[low_cell], uplift_integral[high_cell], alpha);
          }
          const double physical_height =
            advected_height + uplift_integral[cell] - advected_uplift;
          // The stream-power erosion term is never positive: even when
          // depression routing crosses a dry saddle, elevation cannot rise
          // faster than the prescribed tectonic uplift.
          predicted[cell] = std::min (
            static_cast<float> (physical_height / height_scale),
            initial[cell] + uplift_rate * duration_years / height_scale);
          if (boundary[cell])
            predicted[cell] = initial[cell];

          stack.push_back ({ cell, true });
          for (std::uint32_t donor = first_donor[cell]; donor != no_donor;
               donor = next_donor[donor])
            stack.push_back ({ donor, false });
        }
      }

      if (std::find (visited.begin (), visited.end (), 0) != visited.end ())
        throw std::logic_error (
          "analytical erosion drainage does not reach a fixed boundary");
      for (std::size_t cell = 0; cell < count; ++cell)
        current[cell] =
          std::lerp (current[cell], predicted[cell], parameters.relaxation);
    }

    double lowered_volume_m3 = 0.0;
    double raised_volume_m3 = 0.0;
    double absolute_change_m = 0.0;
    double maximum_absolute_change_m = 0.0;
    for (std::size_t cell = 0; cell < count; ++cell) {
      const double change =
        static_cast<double> (current[cell] - initial[cell]) * height_scale;
      if (change < 0.0)
        lowered_volume_m3 -= change * cell_area;
      else
        raised_volume_m3 += change * cell_area;
      absolute_change_m += std::fabs (change);
      maximum_absolute_change_m =
        std::max (maximum_absolute_change_m, std::fabs (change));
    }
    report.lowered_volume = lowered_volume_m3 * mp_units::si::metre *
                            mp_units::si::metre * mp_units::si::metre;
    report.raised_volume = raised_volume_m3 * mp_units::si::metre *
                           mp_units::si::metre * mp_units::si::metre;
    report.mean_absolute_change =
      absolute_change_m / count * mp_units::si::metre;
    report.maximum_absolute_change =
      maximum_absolute_change_m * mp_units::si::metre;
    return { .heights = std::move (current), .report = report };
  }
}
