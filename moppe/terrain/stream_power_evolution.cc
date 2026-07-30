#include <moppe/terrain/stream_power_evolution.hh>

#include <moppe/gfx/signal.hh>
#include <moppe/profile.hh>
#include <moppe/quantities.hh>
#include <moppe/spatial/bundle_operations.hh>
#include <moppe/terrain/domain.hh>
#include <moppe/terrain/flood.hh>
#include <moppe/terrain/fractional_drainage.hh>

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <limits>
#include <stdexcept>
#include <vector>

#include <mp-units/framework.h>
#include <mp-units/math.h>
#include <mp-units/systems/astronomy.h>
#include <mp-units/systems/isq.h>
#include <mp-units/systems/si.h>

namespace moppe::terrain {
  namespace {
    using mp_units::abs;
    using mp_units::default_point_origin;
    using mp_units::isfinite;
    using mp_units::one;
    using mp_units::quantity_point;
    using mp_units::astronomy::Julian_year;
    using mp_units::si::metre;
    using spatial::for_each_site;
    using spatial::get;
    using spatial::laplacian;

    // The backward-Euler solve mixes heights of the whole world with small
    // per-step changes, so it carries its points at double precision and
    // narrows once when a solved cell is stored.
    using ElevationF64 =
      quantity_point<surface_elevation[u::m],
                     default_point_origin (surface_elevation[u::m]),
                     double>;

    void
    check_stream_power_evolution_parameters (const StreamPowerEvolution& p) {
      if (!isfinite (p.duration) || p.duration < 0 || !isfinite (p.time_step) ||
          p.time_step <= 0 || !isfinite (p.reference_incision_rate) ||
          p.reference_incision_rate < 0 || !isfinite (p.reference_area) ||
          p.reference_area <= 0 || !std::isfinite (p.area_exponent) ||
          p.area_exponent < 0 || !isfinite (p.diffusivity) ||
          p.diffusivity < 0 || !std::isfinite (p.sea_level) ||
          !isfinite (p.channel_persistence) || p.channel_persistence < 0 ||
          p.channel_persistence >= 1 * one)
        throw std::invalid_argument (
          "invalid stream-power evolution parameters");
    }

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

      check_stream_power_evolution_parameters (parameters);

      for (const auto& elevation : elevations)
        if (!isfinite (elevation.quantity_from_zero ()))
          throw std::invalid_argument ("elevation must be finite");

      for (const auto& uplift : uplift_rate)
        if (!isfinite (uplift) || uplift < 0)
          throw std::invalid_argument (
            "uplift rate must be finite and non-negative");
    }

    // The unit of the discrete time axis. Dividing a step size by it turns
    // "so many years, each step" into a pace, and paces are what the count
    // arithmetic below wants.
    inline constexpr IterationCount one_iteration = 1 * one;

    // How much time one iteration of a discrete pass covers.
    using IterationPace = decltype (julian_years_f64_t {} / IterationCount {});

    // How many iterations at a pace cover a duration; the last covering
    // iteration may be partial, so this rounds up. No number is extracted
    // anywhere: a duration over a pace already is a count.
    IterationCount whole_step_count (julian_years_f64_t duration,
                                     IterationPace pace,
                                     const char* excess) {
      const auto requested = mp_units::ceil<one> (duration / pace);
      if (!isfinite (requested) ||
          requested > std::numeric_limits<int>::max () * one)
        throw std::invalid_argument (excess);
      return value_cast<int> (requested);
    }

    // Remembered channel directions must arrive as finite horizontal unit
    // vectors, and both readings stay typed: the squared magnitude answers
    // finite and unit-length at once, and the dot with the vertical answers
    // horizontal without naming which lane of the vector is up. A missing
    // memory starts every channel from rest.
    std::vector<ChannelTangent>
    validated_channel_memory (std::span<const ChannelTangent> tangents,
                              std::size_t count) {
      if (tangents.empty ())
        return std::vector<ChannelTangent> (count,
                                            Vec3 () * channel_tangent[one]);
      if (tangents.size () != count)
        throw std::invalid_argument (
          "initial channel tangents do not match terrain");

      const auto vertical = Vec3 (0.0f, 1.0f, 0.0f) * one;

      for (const ChannelTangent value : tangents) {
        const auto length = value.magnitude ();
        const auto lean = dot (value, vertical);
        if (!isfinite (length) || length * length > 1.0001f * one ||
            !(abs (lean) <= 1e-6f * decltype (lean)::reference))
          throw std::invalid_argument (
            "initial channel tangents must be finite horizontal unit vectors");
      }

      return { tangents.begin (), tangents.end () };
    }

    IterationCount
    diffuse_evolution_step (ElevationMap& elevation,
                            const std::vector<std::uint8_t>& boundary,
                            julian_years_f64_t duration,
                            square_meters_per_julian_year_t diffusivity) {
      MOPPE_PROFILE_ZONE ("orogeny.diffuse_step");

      if (duration == 0 || diffusivity == 0)
        return 0 * one;

      const TerrainDomain& grid = elevation.domain ();
      const meters_t spacing_x = grid.spacing_x ();
      const meters_t spacing_z = grid.spacing_z ();

      // The longest time one sweep may cover and stay stable: a pace, not a
      // duration, because it is a property of each iteration of the scheme.
      const IterationPace stable_pace =
        1.0 /
        (2.0 * diffusivity *
         (1.0 / (spacing_x * spacing_x) + 1.0 / (spacing_z * spacing_z))) /
        one_iteration;

      const IterationCount sweeps = std::max (
        one_iteration,
        whole_step_count (duration,
                          stable_pace,
                          "stream-power diffusion requests too many sweeps"));

      // The pace actually swept: the duration shared evenly across the
      // count. Diffusivity times a pace is then an area per iteration --
      // the square of the distance each sweep spreads material across.
      const IterationPace pace = duration / sweeps;
      const auto sweep_spread =
        mp_units::value_cast<float> (diffusivity * pace);

      // Each sweep advances the discrete time axis by one iteration,
      // displacing every free cell along the Laplacian of the elevation
      // around it, which the metric neighbourhood serves as an elevation
      // per area; fixed base-level cells keep their height but still
      // support their neighbours.

      ElevationMap scratch (grid);

      auto& heights = get<surface_elevation> (elevation);
      auto& smoothed = get<surface_elevation> (scratch);

      for (IterationCount sweep = 0 * one; sweep < sweeps;
           sweep += one_iteration) {
        for_each_site (elevation, [&] (const auto& focus) {
          const std::size_t cell = grid.offset (focus.index ());
          const SurfaceElevation centre = get<surface_elevation> (focus);
          smoothed[cell] = boundary[cell]
                             ? centre
                             : centre + sweep_spread * one_iteration *
                                          laplacian<surface_elevation> (focus);
        });

        heights.swap (smoothed);
      }

      return sweeps;
    }
  }

  StreamPowerEvolutionResult detail::evolve_stream_power (
    const TerrainDomain& grid,
    std::span<const SurfaceElevation> elevations,
    std::span<const meters_per_julian_year_t> uplift_rate,
    const StreamPowerEvolution& parameters,
    const StreamPowerProgress& progress,
    std::span<const ChannelTangent> initial_channel_tangents) {

    MOPPE_PROFILE_ZONE ("evolve_stream_power");
    validate_stream_power_evolution (grid, elevations, uplift_rate, parameters);

    const std::size_t count = grid.width () * grid.height ();
    const square_meters_t cell_area = grid.cell_area ();
    const julian_years_f64_t duration = parameters.duration;
    const julian_years_f64_t time_step = parameters.time_step;

    // The input span is a column over this same lattice, so it already lies
    // in the domain's own storage order and seeds the working state whole.
    const std::vector<SurfaceElevation> initial (elevations.begin (),
                                                 elevations.end ());

    std::vector<ChannelTangent> channel_memory =
      validated_channel_memory (initial_channel_tangents, count);

    StreamPowerEvolutionReport report { .cells = cell_count (count) };

    // this copies the elevation column into a scratch bundle
    ElevationMap current (grid, { elevations.begin (), elevations.end () });
    auto& current_heights = get<surface_elevation> (current);

    if (duration == 0)
      return { .heights = std::move (current_heights),
               .channel_tangents = std::move (channel_memory),
               .report = report };

    // The recipe's time step means "so many years, each step": a pace on
    // the discrete axis of geological steps.
    const IterationPace step_pace = time_step / one_iteration;
    const IterationCount steps = whole_step_count (
      duration, step_pace, "stream-power evolution requests too many steps");

    report.steps = steps;

    IterationCount diffusion_sweeps = 0 * one;

    cubic_meters_f64_t tectonic_uplift_volume = 0.0 * u::m * u::m * u::m;
    cubic_meters_f64_t incised_volume = 0.0 * u::m * u::m * u::m;

    ElevationMap next (grid);

    auto& next_heights = get<surface_elevation> (next);
    std::vector<std::uint8_t> boundary (count);

    for (IterationCount step = 0 * one; step < steps; step += one_iteration) {
      MOPPE_PROFILE_NAMED_ZONE (geological_step, "orogeny.geological_step");

      const julian_years_f64_t elapsed = step * step_pace;
      const julian_years_f64_t dt = std::min (time_step, duration - elapsed);

      const FloodField flood =
        analyze_standing_water (current, parameters.sea_level);

      const LakeCensus census = census_lakes (flood);

      std::copy (current_heights.begin (),
                 current_heights.end (),
                 next_heights.begin ());
      std::fill (boundary.begin (), boundary.end (), 0);
      std::size_t fixed_boundaries = 0;

      {
        MOPPE_PROFILE_ZONE ("orogeny.solve_uplift_and_incision");
        const auto solve = [&] (std::uint32_t cell,
                                bool fixed,
                                square_meters_t area,
                                meters_t run,
                                ElevationF64 receiver) {
          if (fixed) {
            boundary[cell] = 1;
            // Ocean cells are fixed base-level outlets, but their bed is not
            // the water surface. Preserve submerged relief rather than lifting
            // the entire ocean floor to sea level at every geological step.
            next_heights[cell] = current_heights[cell];
            ++fixed_boundaries;
            return;
          }

          const double area_growth = (area / parameters.reference_area)
                                       .numerical_value_in (mp_units::one);
          // Soft country gives way faster under the same discharge, so its
          // channels cut down while the hard ground beside them holds up.
          // That difference is where a landscape gets more than one valley
          // spacing, and with it a hierarchy instead of a comb.
          // A channel head is not a sharp place on the ground, so it is not
          // a sharp threshold here either: the fluvial share rises across a
          // factor of four in catchment around the initiation area.
          const double channel_share = static_cast<double> (
            smoothstep (-1.0f,
                        1.0f,
                        std::log2 (static_cast<float> (
                          (area / parameters.channel_initiation_area)
                            .numerical_value_in (mp_units::one)))));
          const auto incision_velocity =
            channel_share * std::pow (area_growth, parameters.area_exponent) *
            parameters.reference_incision_rate;
          const auto coupling = incision_velocity / run;
          const auto weight = dt * coupling;

          if (!mp_units::isfinite (weight))
            throw std::overflow_error (
              "stream-power implicit weight is not finite");

          const meters_f64_t uplift = dt * uplift_rate[cell];
          const ElevationF64 uplifted = current_heights[cell] + uplift;

          // Backward Euler in affine form: the receiver, plus the cell's
          // surplus above it shrunk by the implicit weight. Depression
          // routing may cross a raw uphill bed edge, and incision is not
          // deposition: the solve may never raise a cell above uplift alone.
          const ElevationF64 solved =
            receiver + (uplifted - receiver) / (1.0 + weight);
          const ElevationF64 evolved = std::min (solved, uplifted);

          next_heights[cell] = mp_units::value_cast<float> (evolved);
          tectonic_uplift_volume += uplift * cell_area;
          incised_volume +=
            mp_units::isq::height (uplifted - evolved) * cell_area;
        };

        const FractionalDrainage drainage = analyze_fractional_drainage (
          flood, census, channel_memory, parameters.channel_persistence);
        channel_memory = spatial::get<channel_tangent> (drainage);
        const auto& areas =
          spatial::get<fractional_contributing_area> (drainage);

        const auto order = drainage.domain ().topological_order ();
        for (auto position = order.rbegin (); position != order.rend ();
             ++position) {
          const CellIndex cell = *position;
          const FractionalFlowRoute& route = drainage.domain ().route (cell);
          ElevationF64 receiver = next_heights[cell.value];

          if (route.arc_count == 1) {
            receiver = next_heights[route.arcs[0].receiver.value];
          } else if (route.arc_count == 2) {
            const SurfaceElevation primary =
              next_heights[route.arcs[0].receiver.value];
            const SurfaceElevation secondary =
              next_heights[route.arcs[1].receiver.value];
            receiver =
              primary + (secondary - primary) * route.receiver_interpolation;
          }

          const square_meters_t contributing_area =
            std::max (grid.cell_area (), square_meters_t (areas[cell.value]));

          solve (cell.value,
                 flood.ocean[cell.value] || route.empty (),
                 contributing_area,
                 route.empty () ? 1.0f * mp_units::si::metre : route.run,
                 receiver);
        }
      }

      IterationCount step_sweeps = 0 * one;
      {
        MOPPE_PROFILE_ZONE ("orogeny.apply_hillslope_diffusion");
        step_sweeps =
          diffuse_evolution_step (next, boundary, dt, parameters.diffusivity);
      }

      if (step_sweeps >
          std::numeric_limits<IterationCount>::max () - diffusion_sweeps)
        throw std::overflow_error (
          "stream-power diffusion sweep count overflow");

      diffusion_sweeps += step_sweeps;
      meters_f64_t total_step_change = 0.0 * u::m;
      meters_f64_t maximum_step_change = 0.0 * u::m;

      {
        MOPPE_PROFILE_ZONE ("orogeny.measure_step_change");
        for (std::size_t cell = 0; cell < count; ++cell) {
          const meters_f64_t change = mp_units::abs (
            mp_units::isq::height (next_heights[cell] - current_heights[cell]));
          total_step_change += change;
          maximum_step_change = std::max (maximum_step_change, change);
        }
      }

      report.fixed_boundaries = cell_count (fixed_boundaries);
      report.final_step_mean_change =
        total_step_change / static_cast<double> (count);
      report.final_step_maximum_change = maximum_step_change;

      current_heights.swap (next_heights);

      if (progress)
        progress (step + one_iteration, steps, current_heights);
    }

    report.diffusion_sweeps = diffusion_sweeps;
    report.tectonic_uplift_volume = tectonic_uplift_volume;
    report.incised_volume = incised_volume;

    cubic_meters_f64_t lowered_volume = 0.0 * u::m * u::m * u::m;
    cubic_meters_f64_t raised_volume = 0.0 * u::m * u::m * u::m;
    meters_f64_t total_absolute_change = 0.0 * u::m;
    meters_f64_t maximum_absolute_change = 0.0 * u::m;

    for (std::size_t cell = 0; cell < count; ++cell) {
      const meters_f64_t change =
        mp_units::isq::height (current_heights[cell] - initial[cell]);

      if (change < meters_f64_t::zero ())
        lowered_volume -= change * cell_area;
      else
        raised_volume += change * cell_area;

      total_absolute_change += mp_units::abs (change);
      maximum_absolute_change =
        std::max (maximum_absolute_change, mp_units::abs (change));
    }

    report.lowered_volume = lowered_volume;
    report.raised_volume = raised_volume;
    report.mean_absolute_change =
      total_absolute_change / static_cast<double> (count);
    report.maximum_absolute_change = maximum_absolute_change;

    return { .heights = std::move (current_heights),
             .channel_tangents = std::move (channel_memory),
             .report = report };
  }
}
