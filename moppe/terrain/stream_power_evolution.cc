#include <moppe/terrain/stream_power_evolution.hh>

#include <moppe/profile.hh>
#include <moppe/terrain/flood.hh>
#include <moppe/terrain/fractional_drainage.hh>

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <limits>
#include <stdexcept>
#include <vector>

namespace moppe::terrain {
  namespace {
    void validate_stream_power_evolution (
      const TerrainDomain& grid,
      std::span<const SurfaceElevation> elevations,
      std::span<const meters_per_julian_year_t> uplift_rate,
      const StreamPowerEvolution& parameters) {
      MOPPE_PROFILE_ZONE ("orogeny.validate");
      const std::size_t count = grid.width () * grid.height ();
      if (uplift_rate.size () != count)
        throw std::invalid_argument (
          "stream-power uplift field does not match terrain");
      if (!std::isfinite (julian_years_value (parameters.duration)) ||
          parameters.duration < 0.0f * mp_units::astronomy::Julian_year ||
          !std::isfinite (julian_years_value (parameters.time_step)) ||
          parameters.time_step <= 0.0f * mp_units::astronomy::Julian_year ||
          !std::isfinite (meters_per_julian_year_value (
            parameters.reference_incision_rate)) ||
          parameters.reference_incision_rate <
            0.0f * mp_units::si::metre / mp_units::astronomy::Julian_year ||
          !std::isfinite (square_meters_value (parameters.reference_area)) ||
          parameters.reference_area <=
            0.0f * mp_units::si::metre * mp_units::si::metre ||
          !std::isfinite (parameters.area_exponent) ||
          parameters.area_exponent < 0.0f ||
          !std::isfinite (
            square_meters_per_julian_year_value (parameters.diffusivity)) ||
          parameters.diffusivity < 0.0f * mp_units::si::metre *
                                     mp_units::si::metre /
                                     mp_units::astronomy::Julian_year ||
          !std::isfinite (parameters.sea_level) ||
          !std::isfinite (parameters.channel_persistence.numerical_value_in (
            mp_units::one)) ||
          parameters.channel_persistence <
            0.0f * channel_persistence[mp_units::one] ||
          parameters.channel_persistence >=
            1.0f * channel_persistence[mp_units::one])
        throw std::invalid_argument (
          "invalid stream-power evolution parameters");
      for (std::size_t cell = 0; cell < count; ++cell) {
        const float height = elevation_at (
          grid, elevations, cell % grid.width (), cell / grid.width ());
        const float uplift = meters_per_julian_year_value (uplift_rate[cell]);
        if (!std::isfinite (height) || !std::isfinite (uplift) || uplift < 0.0f)
          throw std::invalid_argument (
            "stream-power heights and uplift rates must be finite and uplift "
            "must be non-negative");
      }
    }

    int diffuse_evolution_step (std::vector<float>& heights,
                                const std::vector<std::uint8_t>& boundary,
                                const TerrainDomain& grid,
                                double duration_years,
                                double diffusivity_m2_per_year) {
      MOPPE_PROFILE_ZONE ("orogeny.diffuse_step");
      if (duration_years == 0.0 || diffusivity_m2_per_year == 0.0)
        return 0;
      const std::size_t width = grid.width ();
      const std::size_t height = grid.height ();
      const double hx = meters_value (grid.spacing_x ());
      const double hy = meters_value (grid.spacing_z ());
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
      const auto neighbor = [] (std::ptrdiff_t value, std::size_t extent) {
        const std::ptrdiff_t n = static_cast<std::ptrdiff_t> (extent);
        const std::ptrdiff_t result = value % n;
        return static_cast<std::size_t> (result < 0 ? result + n : result);
      };

      std::vector<float> next (heights.size ());
      for (int sweep = 0; sweep < sweeps; ++sweep) {
        for (std::size_t y = 0; y < height; ++y) {
          const std::size_t up =
            neighbor (static_cast<std::ptrdiff_t> (y) - 1, height);
          const std::size_t down =
            neighbor (static_cast<std::ptrdiff_t> (y) + 1, height);
          for (std::size_t x = 0; x < width; ++x) {
            const std::size_t cell = y * width + x;
            if (boundary[cell]) {
              next[cell] = heights[cell];
              continue;
            }
            const std::size_t left =
              neighbor (static_cast<std::ptrdiff_t> (x) - 1, width);
            const std::size_t right =
              neighbor (static_cast<std::ptrdiff_t> (x) + 1, width);
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

  FractionalDrainage CpuStreamPowerEvolutionBackend::route_fractional (
    const FloodField& flood,
    const LakeCensus& census,
    std::span<const ChannelTangent> previous_tangent,
    ChannelPersistence persistence) const {
    return analyze_fractional_drainage (
      flood, census, previous_tangent, persistence);
  }

  StreamPowerEvolutionResult detail::evolve_stream_power (
    const TerrainDomain& grid,
    std::span<const SurfaceElevation> elevations,
    std::span<const meters_per_julian_year_t> uplift_rate,
    const StreamPowerEvolution& parameters,
    const StreamPowerEvolutionBackend& backend,
    const StreamPowerProgress& progress,
    std::span<const ChannelTangent> initial_channel_tangents) {
    MOPPE_PROFILE_ZONE ("evolve_stream_power");
    validate_stream_power_evolution (grid, elevations, uplift_rate, parameters);
    const std::size_t width = grid.width ();
    const std::size_t height = grid.height ();
    const std::size_t count = width * height;
    if (!initial_channel_tangents.empty () &&
        initial_channel_tangents.size () != count)
      throw std::invalid_argument (
        "initial channel tangents do not match terrain");
    const double cell_area_m2 = square_meters_value (grid.cell_area ());
    const double duration_years = julian_years_value (parameters.duration);
    const double time_step_years = julian_years_value (parameters.time_step);
    const double diffusivity_m2_per_year =
      square_meters_per_julian_year_value (parameters.diffusivity);

    std::vector<float> initial (count);
    for (std::size_t y = 0; y < height; ++y)
      for (std::size_t x = 0; x < width; ++x)
        initial[y * width + x] = elevation_at (grid, elevations, x, y);
    std::vector<float> current = initial;
    std::vector<ChannelTangent> channel_memory;
    if (!initial_channel_tangents.empty ()) {
      channel_memory.assign (initial_channel_tangents.begin (),
                             initial_channel_tangents.end ());
      for (const ChannelTangent value : channel_memory) {
        const Vec3 tangent = value.numerical_value_in (mp_units::one);
        if (!std::isfinite (tangent[0]) || !std::isfinite (tangent[1]) ||
            !std::isfinite (tangent[2]) || std::fabs (tangent[1]) > 1e-6f ||
            length2 (tangent) > 1.0001f)
          throw std::invalid_argument (
            "initial channel tangents must be finite horizontal unit vectors");
      }
    } else {
      channel_memory.assign (count, Vec3 () * channel_tangent[mp_units::one]);
    }

    StreamPowerEvolutionReport report { .cells = cell_count (count) };
    if (duration_years == 0.0)
      return { .heights = std::move (current),
               .channel_tangents = std::move (channel_memory),
               .report = report };

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
    std::vector<float> next (count);
    std::vector<std::uint8_t> boundary (count);

    for (int step = 0; step < steps; ++step) {
      MOPPE_PROFILE_NAMED_ZONE (geological_step, "orogeny.geological_step");
      const double elapsed = static_cast<double> (step) * time_step_years;
      const double dt = std::min (time_step_years, duration_years - elapsed);
      const ElevationMap routed_terrain = make_elevation_map (grid, current);
      const FloodField flood =
        analyze_standing_water (routed_terrain, parameters.sea_level);
      const LakeCensus census = census_lakes (flood);

      std::copy (current.begin (), current.end (), next.begin ());
      std::fill (boundary.begin (), boundary.end (), 0);
      std::size_t fixed_boundaries = 0;
      {
        MOPPE_PROFILE_ZONE ("orogeny.solve_uplift_and_incision");
        const auto solve = [&] (std::uint32_t cell,
                                bool fixed,
                                square_meters_t area,
                                meters_t run,
                                double receiver_height_m) {
          if (fixed) {
            boundary[cell] = 1;
            // Ocean cells are fixed base-level outlets, but their bed is not
            // the water surface. Preserve submerged relief rather than lifting
            // the entire ocean floor to sea level at every geological step.
            next[cell] = current[cell];
            ++fixed_boundaries;
            return;
          }

          const double area_ratio =
            square_meters_value (area) /
            square_meters_value (parameters.reference_area);
          const auto incision_velocity =
            std::pow (area_ratio, parameters.area_exponent) *
            parameters.reference_incision_rate;
          const auto coupling = incision_velocity / run;
          const auto dimensionless_weight =
            (static_cast<float> (dt) * mp_units::astronomy::Julian_year) *
            coupling;
          const double weight =
            dimensionless_weight.numerical_value_in (mp_units::one);
          if (!std::isfinite (weight))
            throw std::overflow_error (
              "stream-power implicit weight is not finite");
          const double old_height_m = current[cell];
          const double uplift_m =
            dt * meters_per_julian_year_value (uplift_rate[cell]);
          const double uplift_only_m = old_height_m + uplift_m;
          const double solved_m =
            (uplift_only_m + weight * receiver_height_m) / (1.0 + weight);
          // Depression routing may cross a raw uphill bed edge. Incision is
          // not deposition: it may never raise a cell above uplift alone.
          const double evolved_m = std::min (solved_m, uplift_only_m);
          next[cell] = static_cast<float> (evolved_m);
          tectonic_uplift_volume_m3 += uplift_m * cell_area_m2;
          incised_volume_m3 += (uplift_only_m - evolved_m) * cell_area_m2;
        };

        const FractionalDrainage drainage = backend.route_fractional (
          flood, census, channel_memory, parameters.channel_persistence);
        channel_memory = spatial::get<channel_tangent> (drainage);
        const auto& area =
          spatial::get<fractional_contributing_area> (drainage);
        const auto order = drainage.domain ().topological_order ();
        for (auto position = order.rbegin (); position != order.rend ();
             ++position) {
          const CellIndex cell = *position;
          const FractionalFlowRoute& route = drainage.domain ().route (cell);
          double receiver_height_m = next[cell.value];
          if (route.arc_count == 1) {
            receiver_height_m = next[route.arcs[0].receiver.value];
          } else if (route.arc_count == 2) {
            const float interpolation =
              route.receiver_interpolation.numerical_value_in (mp_units::one);
            receiver_height_m = std::lerp (next[route.arcs[0].receiver.value],
                                           next[route.arcs[1].receiver.value],
                                           interpolation);
          }
          const square_meters_t physical_area =
            area[cell.value].numerical_value_in (mp_units::si::metre *
                                                 mp_units::si::metre) *
            mp_units::si::metre * mp_units::si::metre;
          const square_meters_t contributing_area =
            std::max (square_meters_value (grid.cell_area ()),
                      square_meters_value (physical_area)) *
            mp_units::si::metre * mp_units::si::metre;
          solve (cell.value,
                 flood.ocean[cell.value] || route.empty (),
                 contributing_area,
                 route.empty () ? 1.0f * mp_units::si::metre : route.run,
                 receiver_height_m);
        }
      }
      int step_sweeps = 0;
      {
        MOPPE_PROFILE_ZONE ("orogeny.apply_hillslope_diffusion");
        step_sweeps = diffuse_evolution_step (
          next, boundary, grid, dt, diffusivity_m2_per_year);
      }
      if (step_sweeps > std::numeric_limits<int>::max () - diffusion_sweeps)
        throw std::overflow_error (
          "stream-power diffusion sweep count overflow");
      diffusion_sweeps += step_sweeps;
      double total_step_change_m = 0.0;
      double maximum_step_change_m = 0.0;
      {
        MOPPE_PROFILE_ZONE ("orogeny.measure_step_change");
        for (std::size_t cell = 0; cell < count; ++cell) {
          const double change_m =
            std::fabs (static_cast<double> (next[cell] - current[cell]));
          total_step_change_m += change_m;
          maximum_step_change_m = std::max (maximum_step_change_m, change_m);
        }
      }
      report.fixed_boundaries = cell_count (fixed_boundaries);
      report.final_step_mean_change =
        total_step_change_m / count * mp_units::si::metre;
      report.final_step_maximum_change =
        maximum_step_change_m * mp_units::si::metre;
      current.swap (next);
      if (progress)
        progress (step + 1, steps, current);
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
        static_cast<double> (current[cell] - initial[cell]);
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
    return { .heights = std::move (current),
             .channel_tangents = std::move (channel_memory),
             .report = report };
  }

  StreamPowerEvolutionResult detail::evolve_stream_power (
    const TerrainDomain& domain,
    std::span<const SurfaceElevation> elevations,
    std::span<const meters_per_julian_year_t> uplift_rate,
    const StreamPowerEvolution& parameters,
    const StreamPowerProgress& progress,
    std::span<const ChannelTangent> initial_channel_tangents) {
    static const CpuStreamPowerEvolutionBackend backend;
    return evolve_stream_power (domain,
                                elevations,
                                uplift_rate,
                                parameters,
                                backend,
                                progress,
                                initial_channel_tangents);
  }
}
