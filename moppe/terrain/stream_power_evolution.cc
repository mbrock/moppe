#include <moppe/terrain/stream_power_evolution.hh>

#include <moppe/terrain/drainage.hh>
#include <moppe/terrain/flood.hh>

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <limits>
#include <queue>
#include <stdexcept>
#include <vector>

namespace moppe::terrain {
  namespace {
    std::vector<float>
    expand_evolution_samples (const TerrainGrid& grid,
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

    double edge_distance_m (std::uint32_t cell,
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
      return std::hypot (static_cast<double> (dx) * grid.spacing_x_m (),
                         static_cast<double> (dy) * grid.spacing_y_m ());
    }

    void validate_stream_power_evolution (
      const TerrainView& terrain,
      std::span<const meters_per_julian_year_t> uplift_rate,
      const StreamPowerEvolution& parameters) {
      const TerrainGrid& grid = terrain.grid ();
      const std::size_t count = grid.unique_width () * grid.unique_height ();
      if (uplift_rate.size () != count)
        throw std::invalid_argument (
          "stream-power uplift field does not match terrain");
      if (!std::isfinite (julian_years_value (parameters.duration)) ||
          parameters.duration < 0.0f * mp_units::astronomy::Julian_year ||
          !std::isfinite (julian_years_value (parameters.time_step)) ||
          parameters.time_step <= 0.0f * mp_units::astronomy::Julian_year ||
          !std::isfinite (parameters.erodibility) ||
          parameters.erodibility < 0.0f ||
          !std::isfinite (parameters.area_exponent) ||
          parameters.area_exponent < 0.0f ||
          !std::isfinite (
            square_meters_per_julian_year_value (parameters.diffusivity)) ||
          parameters.diffusivity < 0.0f * mp_units::si::metre *
                                     mp_units::si::metre /
                                     mp_units::astronomy::Julian_year ||
          !std::isfinite (parameters.sea_level))
        throw std::invalid_argument (
          "invalid stream-power evolution parameters");
      for (std::size_t cell = 0; cell < count; ++cell) {
        const float height =
          terrain.at (cell % grid.unique_width (), cell / grid.unique_width ());
        const float uplift = meters_per_julian_year_value (uplift_rate[cell]);
        if (!std::isfinite (height) || !std::isfinite (uplift) || uplift < 0.0f)
          throw std::invalid_argument (
            "stream-power heights and uplift rates must be finite and uplift "
            "must be non-negative");
      }
    }

    std::vector<std::uint32_t>
    downstream_first_order (const std::vector<CellIndex>& receiver) {
      const std::size_t count = receiver.size ();
      std::vector<std::uint32_t> donors (count, 0);
      for (std::uint32_t cell = 0; cell < count; ++cell) {
        const std::uint32_t next = receiver[cell];
        if (next >= count)
          throw std::logic_error (
            "stream-power drainage receiver is outside terrain");
        if (next != cell)
          ++donors[next];
      }

      std::priority_queue<std::uint32_t,
                          std::vector<std::uint32_t>,
                          std::greater<std::uint32_t>>
        ready;
      for (std::uint32_t cell = 0; cell < count; ++cell)
        if (donors[cell] == 0)
          ready.push (cell);

      std::vector<std::uint32_t> order;
      order.reserve (count);
      while (!ready.empty ()) {
        const std::uint32_t cell = ready.top ();
        ready.pop ();
        order.push_back (cell);
        const std::uint32_t next = receiver[cell];
        if (next != cell && --donors[next] == 0)
          ready.push (next);
      }
      if (order.size () != count)
        throw std::logic_error (
          "stream-power drainage does not form an acyclic forest");
      std::reverse (order.begin (), order.end ());
      return order;
    }

    int diffuse_evolution_step (std::vector<float>& heights,
                                const std::vector<std::uint8_t>& boundary,
                                const TerrainGrid& grid,
                                double duration_years,
                                double diffusivity_m2_per_year) {
      if (duration_years == 0.0 || diffusivity_m2_per_year == 0.0)
        return 0;
      const std::size_t width = grid.unique_width ();
      const std::size_t height = grid.unique_height ();
      const double hx = grid.spacing_x_m ();
      const double hy = grid.spacing_y_m ();
      const double stable_dt = 1.0 / (2.0 * diffusivity_m2_per_year *
                                      (1.0 / (hx * hx) + 1.0 / (hy * hy)));
      const double requested = std::ceil (duration_years / stable_dt);
      if (!std::isfinite (requested) ||
          requested > std::numeric_limits<int>::max ())
        throw std::invalid_argument (
          "stream-power diffusion requests too many sweeps");
      const int sweeps = std::max (1, static_cast<int> (requested));
      const double dt = duration_years / sweeps;
      const float cx =
        static_cast<float> (diffusivity_m2_per_year * dt / (hx * hx));
      const float cy =
        static_cast<float> (diffusivity_m2_per_year * dt / (hy * hy));
      const bool periodic = grid.topology == Topology::Torus;
      const auto neighbor =
        [] (std::ptrdiff_t value, std::size_t extent, bool wrap) {
          if (wrap) {
            const std::ptrdiff_t n = static_cast<std::ptrdiff_t> (extent);
            const std::ptrdiff_t result = value % n;
            return static_cast<std::size_t> (result < 0 ? result + n : result);
          }
          return static_cast<std::size_t> (
            std::clamp<std::ptrdiff_t> (value, 0, extent - 1));
        };

      std::vector<float> next (heights.size ());
      for (int sweep = 0; sweep < sweeps; ++sweep) {
        for (std::size_t y = 0; y < height; ++y) {
          const std::size_t up =
            neighbor (static_cast<std::ptrdiff_t> (y) - 1, height, periodic);
          const std::size_t down =
            neighbor (static_cast<std::ptrdiff_t> (y) + 1, height, periodic);
          for (std::size_t x = 0; x < width; ++x) {
            const std::size_t cell = y * width + x;
            if (boundary[cell]) {
              next[cell] = heights[cell];
              continue;
            }
            const std::size_t left =
              neighbor (static_cast<std::ptrdiff_t> (x) - 1, width, periodic);
            const std::size_t right =
              neighbor (static_cast<std::ptrdiff_t> (x) + 1, width, periodic);
            const float center = heights[cell];
            next[cell] = center +
                         cx * (heights[y * width + left] +
                               heights[y * width + right] - 2.0f * center) +
                         cy * (heights[up * width + x] +
                               heights[down * width + x] - 2.0f * center);
          }
        }
        heights.swap (next);
      }
      return sweeps;
    }
  }

  StreamPowerEvolutionResult
  evolve_stream_power (const TerrainView& terrain,
                       std::span<const meters_per_julian_year_t> uplift_rate,
                       const StreamPowerEvolution& parameters,
                       const StreamPowerProgress& progress) {
    validate_stream_power_evolution (terrain, uplift_rate, parameters);
    const TerrainGrid& grid = terrain.grid ();
    const std::size_t width = grid.unique_width ();
    const std::size_t height = grid.unique_height ();
    const std::size_t count = width * height;
    const double height_scale_m = grid.height_scale_m ();
    const double cell_area_m2 = square_meters_value (grid.cell_area ());
    const double duration_years = julian_years_value (parameters.duration);
    const double time_step_years = julian_years_value (parameters.time_step);
    const double diffusivity_m2_per_year =
      square_meters_per_julian_year_value (parameters.diffusivity);

    std::vector<float> initial (count);
    for (std::size_t y = 0; y < height; ++y)
      for (std::size_t x = 0; x < width; ++x)
        initial[y * width + x] = terrain.at (x, y);
    std::vector<float> current = initial;

    StreamPowerEvolutionReport report { .cells = cell_count (count) };
    if (duration_years == 0.0)
      return { .heights = std::move (current), .report = report };

    const double requested_steps = std::ceil (duration_years / time_step_years);
    if (!std::isfinite (requested_steps) ||
        requested_steps > std::numeric_limits<int>::max ())
      throw std::invalid_argument (
        "stream-power evolution requests too many steps");
    const int steps = static_cast<int> (requested_steps);
    report.steps = iteration_count (steps);
    int diffusion_sweeps = 0;
    double tectonic_uplift_volume_m3 = 0.0;
    double incised_volume_m3 = 0.0;

    for (int step = 0; step < steps; ++step) {
      const double elapsed = static_cast<double> (step) * time_step_years;
      const double dt = std::min (time_step_years, duration_years - elapsed);
      const std::vector<float> expanded =
        expand_evolution_samples (grid, current);
      const TerrainView routed_terrain (grid, expanded);
      const FloodField flood =
        analyze_standing_water (routed_terrain, parameters.sea_level);
      const LakeCensus census = census_lakes (flood);
      const DrainageGraph drainage =
        analyze_wet_drainage (routed_terrain, flood, census);
      const std::vector<std::uint32_t> order =
        downstream_first_order (drainage.receiver);

      std::vector<float> next = current;
      std::vector<std::uint8_t> boundary (count, 0);
      std::size_t fixed_boundaries = 0;
      for (const std::uint32_t cell : order) {
        const std::uint32_t receiver = drainage.receiver[cell];
        const bool ocean = flood.ocean[cell] != 0;
        if (ocean || receiver == cell) {
          boundary[cell] = 1;
          // Ocean cells are fixed base-level outlets, but their bed is not
          // the water surface. Preserve submerged relief rather than lifting
          // the entire ocean floor to sea level at every geological step.
          next[cell] = current[cell];
          ++fixed_boundaries;
        } else {
          const double distance_m = edge_distance_m (cell, receiver, grid);
          const double area_m2 = std::max (
            cell_area_m2,
            static_cast<double> (drainage.contributing_area.values ()[cell]));
          const double incision_rate =
            parameters.erodibility *
            std::pow (area_m2, parameters.area_exponent) / distance_m;
          const double weight = dt * incision_rate;
          if (!std::isfinite (weight))
            throw std::overflow_error (
              "stream-power implicit weight is not finite");
          const double old_height_m = current[cell] * height_scale_m;
          const double uplift_m =
            dt * meters_per_julian_year_value (uplift_rate[cell]);
          const double uplift_only_m = old_height_m + uplift_m;
          const double receiver_height_m = next[receiver] * height_scale_m;
          const double solved_m =
            (uplift_only_m + weight * receiver_height_m) / (1.0 + weight);
          // Depression routing may cross a raw uphill bed edge. Incision is
          // not deposition: it may never raise a cell above uplift alone.
          const double evolved_m = std::min (solved_m, uplift_only_m);
          next[cell] = static_cast<float> (evolved_m / height_scale_m);
          tectonic_uplift_volume_m3 += uplift_m * cell_area_m2;
          incised_volume_m3 += (uplift_only_m - evolved_m) * cell_area_m2;
        }
      }
      const int step_sweeps = diffuse_evolution_step (
        next, boundary, grid, dt, diffusivity_m2_per_year);
      if (step_sweeps > std::numeric_limits<int>::max () - diffusion_sweeps)
        throw std::overflow_error (
          "stream-power diffusion sweep count overflow");
      diffusion_sweeps += step_sweeps;
      double total_step_change_m = 0.0;
      double maximum_step_change_m = 0.0;
      for (std::size_t cell = 0; cell < count; ++cell) {
        const double change_m =
          std::fabs (static_cast<double> (next[cell] - current[cell])) *
          height_scale_m;
        total_step_change_m += change_m;
        maximum_step_change_m = std::max (maximum_step_change_m, change_m);
      }
      report.fixed_boundaries = cell_count (fixed_boundaries);
      report.final_step_mean_change =
        total_step_change_m / count * mp_units::si::metre;
      report.final_step_maximum_change =
        maximum_step_change_m * mp_units::si::metre;
      current.swap (next);
      if (progress)
        progress (step + 1, steps);
    }
    report.diffusion_sweeps = iteration_count (diffusion_sweeps);
    report.tectonic_uplift_volume = tectonic_uplift_volume_m3 *
                                    mp_units::si::metre * mp_units::si::metre *
                                    mp_units::si::metre;
    report.incised_volume = incised_volume_m3 * mp_units::si::metre *
                            mp_units::si::metre * mp_units::si::metre;

    double lowered_volume_m3 = 0.0;
    double raised_volume_m3 = 0.0;
    double absolute_change_m = 0.0;
    double maximum_absolute_change_m = 0.0;
    for (std::size_t cell = 0; cell < count; ++cell) {
      const double change_m =
        static_cast<double> (current[cell] - initial[cell]) * height_scale_m;
      if (change_m < 0.0)
        lowered_volume_m3 -= change_m * cell_area_m2;
      else
        raised_volume_m3 += change_m * cell_area_m2;
      absolute_change_m += std::fabs (change_m);
      maximum_absolute_change_m =
        std::max (maximum_absolute_change_m, std::fabs (change_m));
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
