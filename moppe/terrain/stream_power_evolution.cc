#include <moppe/terrain/stream_power_evolution.hh>

#include <moppe/gfx/signal.hh>
#include <moppe/profile.hh>
#include <moppe/quantities.hh>
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
    using spatial::get;

    // The backward-Euler solve mixes heights of the whole world with small
    // per-step changes, so it carries its points at double precision and
    // narrows once when a solved cell is stored.
    using ElevationF64 =
      quantity_point<surface_elevation[u::m],
                     default_point_origin (surface_elevation[u::m]),
                     double>;

    constexpr auto stream_cubic_metre = u::m * u::m * u::m;

    SedimentVolume stream_sediment_volume_from (double cubic_metres) {
      return cubic_metres * sediment_volume[stream_cubic_metre];
    }

    double stream_sediment_volume_value (SedimentVolume volume) {
      return volume.numerical_value_in (stream_cubic_metre);
    }

    std::vector<SedimentThickness>
    validated_sediment (std::span<const SedimentThickness> sediment,
                        std::size_t count) {
      if (sediment.empty ())
        return std::vector<SedimentThickness> (count,
                                               0.0f * sediment_thickness[u::m]);
      if (sediment.size () != count)
        throw std::invalid_argument ("initial sediment does not match terrain");
      for (const SedimentThickness value : sediment)
        if (!isfinite (value) || value < SedimentThickness::zero ())
          throw std::invalid_argument (
            "initial sediment must be finite and non-negative");
      return { sediment.begin (), sediment.end () };
    }

    void
    check_stream_power_evolution_parameters (const StreamPowerEvolution& p) {
      if (!isfinite (p.duration) || p.duration < 0 || !isfinite (p.time_step) ||
          p.time_step <= 0 || !isfinite (p.uplift_duration) ||
          p.uplift_duration < 0 || !isfinite (p.reference_incision_rate) ||
          p.reference_incision_rate < 0 ||
          !isfinite (p.maximum_deposition_rate) ||
          p.maximum_deposition_rate < 0 || !isfinite (p.reference_area) ||
          p.reference_area <= 0 || !std::isfinite (p.area_exponent) ||
          p.area_exponent < 0 || !isfinite (p.diffusivity) ||
          p.diffusivity < 0 || !std::isfinite (p.sea_level) ||
          !isfinite (p.fluvial_transport.runoff_rate) ||
          p.fluvial_transport.runoff_rate < 0 ||
          !isfinite (p.fluvial_transport.concentration_at_unit_slope) ||
          p.fluvial_transport.concentration_at_unit_slope < 0 ||
          !isfinite (p.valley_deposition.minimum_width) ||
          p.valley_deposition.minimum_width < 0 ||
          !isfinite (p.valley_deposition.maximum_width) ||
          p.valley_deposition.maximum_width <
            p.valley_deposition.minimum_width ||
          !isfinite (p.valley_deposition.width_per_sqrt_area) ||
          p.valley_deposition.width_per_sqrt_area < 0 ||
          !isfinite (p.valley_deposition.minimum_wall_relief) ||
          p.valley_deposition.minimum_wall_relief < 0 ||
          !isfinite (p.valley_deposition.wall_relief_per_width) ||
          p.valley_deposition.wall_relief_per_width < 0 ||
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

  }

  StreamPowerEvolutionResult detail::evolve_stream_power (
    const TerrainDomain& grid,
    std::span<const SurfaceElevation> elevations,
    std::span<const meters_per_julian_year_t> uplift_rate,
    const StreamPowerEvolution& parameters,
    const StreamPowerProgress& progress,
    std::span<const ChannelTangent> initial_channel_tangents,
    std::span<const SedimentThickness> initial_sediment) {

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
    std::vector<SedimentThickness> mobile_sediment =
      validated_sediment (initial_sediment, count);
    std::vector<SedimentThickness> eroded_thickness (
      count, 0.0f * sediment_thickness[u::m]);
    std::vector<SedimentThickness> deposited_thickness (
      count, 0.0f * sediment_thickness[u::m]);

    StreamPowerEvolutionReport report { .cells = cell_count (count) };

    // this copies the elevation column into a scratch bundle
    ElevationMap current (grid, { elevations.begin (), elevations.end () });
    auto& current_heights = get<surface_elevation> (current);

    if (duration == 0)
      return { .heights = std::move (current_heights),
               .sediment_thickness = std::move (mobile_sediment),
               .eroded_thickness = std::move (eroded_thickness),
               .deposited_thickness = std::move (deposited_thickness),
               .channel_tangents = std::move (channel_memory),
               .report = report };

    // The recipe's time step means "so many years, each step": a pace on
    // the discrete axis of geological steps.
    const IterationPace step_pace = time_step / one_iteration;
    const IterationCount steps = whole_step_count (
      duration, step_pace, "stream-power evolution requests too many steps");

    report.steps = steps;

    IterationCount hillslope_sweeps = 0 * one;

    cubic_meters_f64_t tectonic_uplift_volume = 0.0 * u::m * u::m * u::m;
    cubic_meters_f64_t eroded_volume = 0.0 * u::m * u::m * u::m;
    cubic_meters_f64_t deposited_volume = 0.0 * stream_cubic_metre;
    cubic_meters_f64_t exported_sediment_volume = 0.0 * stream_cubic_metre;
    cubic_meters_f64_t fluvial_entrained_cover_volume =
      0.0 * stream_cubic_metre;
    cubic_meters_f64_t fluvial_bedrock_detached_volume =
      0.0 * stream_cubic_metre;
    cubic_meters_f64_t lake_sediment_storage_volume = 0.0 * stream_cubic_metre;
    cubic_meters_f64_t ocean_mouth_deposition_volume = 0.0 * stream_cubic_metre;
    cubic_meters_f64_t sediment_balance_residual = 0.0 * stream_cubic_metre;
    cubic_meters_f64_t hillslope_transferred_volume = 0.0 * stream_cubic_metre;
    cubic_meters_f64_t hillslope_bedrock_detached_volume =
      0.0 * stream_cubic_metre;

    ElevationMap next (grid);

    auto& next_heights = get<surface_elevation> (next);
    std::vector<std::uint8_t> boundary (count);
    std::vector<ElevationF64> uplifted_heights (count);
    std::vector<SedimentVolume> potential_detachment (count,
                                                      SedimentVolume::zero ());
    std::vector<SedimentVolume> transport_capacity (count,
                                                    SedimentVolume::zero ());
    std::vector<SedimentVolume> maximum_deposition (count,
                                                    SedimentVolume::zero ());
    std::vector<SedimentVolume> available_cover (count,
                                                 SedimentVolume::zero ());

    for (IterationCount step = 0 * one; step < steps; step += one_iteration) {
      MOPPE_PROFILE_NAMED_ZONE (geological_step, "orogeny.geological_step");

      const julian_years_f64_t elapsed = step * step_pace;
      const julian_years_f64_t dt = std::min (time_step, duration - elapsed);
      const julian_years_f64_t uplift_remaining =
        julian_years_f64_t (parameters.uplift_duration) - elapsed;
      const julian_years_f64_t uplift_dt =
        std::clamp (uplift_remaining, julian_years_f64_t::zero (), dt);

      const FloodField flood =
        analyze_standing_water (current, parameters.sea_level);

      const LakeCensus census = census_lakes (flood);

      std::copy (current_heights.begin (),
                 current_heights.end (),
                 next_heights.begin ());
      std::fill (boundary.begin (), boundary.end (), 0);
      std::fill (potential_detachment.begin (),
                 potential_detachment.end (),
                 SedimentVolume::zero ());
      std::fill (transport_capacity.begin (),
                 transport_capacity.end (),
                 SedimentVolume::zero ());
      const double maximum_deposition_m3 =
        (dt * parameters.maximum_deposition_rate * cell_area)
          .numerical_value_in (stream_cubic_metre);
      std::fill (maximum_deposition.begin (),
                 maximum_deposition.end (),
                 stream_sediment_volume_from (maximum_deposition_m3));
      std::size_t fixed_boundaries = 0;

      const FractionalDrainage drainage = analyze_fractional_drainage (
        flood, census, channel_memory, parameters.channel_persistence);
      channel_memory = spatial::get<channel_tangent> (drainage);

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
            uplifted_heights[cell] = current_heights[cell];
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

          const meters_f64_t uplift = uplift_dt * uplift_rate[cell];
          const ElevationF64 uplifted = current_heights[cell] + uplift;

          // Backward Euler in affine form: the receiver, plus the cell's
          // surplus above it shrunk by the implicit weight. Depression
          // routing may cross a raw uphill bed edge. This solve is only the
          // potential detachment: the forward sediment pass below decides
          // how much can move and where transported material is laid down.
          const ElevationF64 solved =
            receiver + (uplifted - receiver) / (1.0 + weight);
          const ElevationF64 evolved = std::min (solved, uplifted);

          next_heights[cell] = mp_units::value_cast<float> (evolved);
          uplifted_heights[cell] = uplifted;
          tectonic_uplift_volume += uplift * cell_area;
          const auto potential_height =
            mp_units::isq::height (uplifted - evolved);
          const double potential_m3 =
            (potential_height * cell_area)
              .numerical_value_in (stream_cubic_metre);
          potential_detachment[cell] =
            stream_sediment_volume_from (potential_m3);

          // Capacity is a water-discharge law, not a multiple of whatever
          // bedrock the implicit incision solve happened to remove. Area
          // times runoff is discharge; solid concentration and local slope
          // say how much material that water can carry during this step.
          const auto receiver_drop =
            mp_units::isq::height (uplifted - receiver);
          const auto downhill_height =
            std::max (receiver_drop, decltype (receiver_drop)::zero ());
          const slope_t slope =
            static_cast<float> (
              (downhill_height / run).numerical_value_in (mp_units::one)) *
            terrain_slope[mp_units::one];
          transport_capacity[cell] =
            sediment_transport_capacity (dt,
                                         area,
                                         slope,
                                         static_cast<float> (channel_share),
                                         parameters.fluvial_transport);
        };

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

      {
        MOPPE_PROFILE_ZONE ("orogeny.route_sediment");
        const double cell_area_m2 = cell_area.numerical_value_in (u::m * u::m);
        for (std::size_t cell = 0; cell < count; ++cell) {
          const double cover_m3 =
            mobile_sediment[cell].numerical_value_in (u::m) * cell_area_m2;
          available_cover[cell] = stream_sediment_volume_from (cover_m3);
        }
        const StandingWaterStorage standing_storage =
          standing_water_storage_capacity (
            flood,
            census,
            spatial::get<fractional_contributing_area> (drainage),
            maximum_deposition,
            parameters.valley_deposition);
        const SedimentRoutingResult routed =
          route_sediment (drainage.domain (),
                          potential_detachment,
                          transport_capacity,
                          maximum_deposition,
                          flood.ocean,
                          available_cover,
                          census.membership ().values (),
                          standing_storage.body_capacity,
                          standing_storage.ocean_mouth_capacity);

        for (std::size_t cell = 0; cell < count; ++cell) {
          const double detached_m =
            stream_sediment_volume_value (routed.detached[cell]) / cell_area_m2;
          const double entrained_cover_m =
            stream_sediment_volume_value (routed.entrained_cover[cell]) /
            cell_area_m2;
          next_heights[cell] = mp_units::value_cast<float> (
            uplifted_heights[cell] - detached_m * u::m);

          const double prior_sediment_m =
            mobile_sediment[cell].numerical_value_in (u::m);
          mobile_sediment[cell] =
            static_cast<float> (
              std::max (0.0, prior_sediment_m - entrained_cover_m)) *
            sediment_thickness[u::m];
          eroded_thickness[cell] +=
            static_cast<float> (detached_m) * sediment_thickness[u::m];
        }

        const StandingWaterDepositionResult standing =
          spread_standing_water_deposition (
            flood,
            census,
            drainage.domain (),
            spatial::get<fractional_contributing_area> (drainage),
            spatial::get<channel_tangent> (drainage),
            routed.deposited,
            maximum_deposition,
            parameters.valley_deposition);
        sediment_balance_residual += standing.balance_residual;
        lake_sediment_storage_volume +=
          stream_sediment_volume_value (standing.lake_storage) *
          stream_cubic_metre;
        ocean_mouth_deposition_volume +=
          stream_sediment_volume_value (standing.ocean_mouth_storage) *
          stream_cubic_metre;
        const double storage_overflow_m3 =
          stream_sediment_volume_value (standing.exported);

        const LateralDepositionResult lateral = spread_valley_deposition (
          drainage.domain (),
          next_heights,
          spatial::get<fractional_contributing_area> (drainage),
          spatial::get<channel_tangent> (drainage),
          standing.dry_centerline,
          flood.ocean,
          parameters.valley_deposition);
        sediment_balance_residual += lateral.balance_residual;

        for (std::size_t cell = 0; cell < count; ++cell) {
          const double deposited_m =
            (stream_sediment_volume_value (lateral.deposited[cell]) +
             stream_sediment_volume_value (standing.deposited[cell])) /
            cell_area_m2;
          next_heights[cell] = mp_units::value_cast<float> (next_heights[cell] +
                                                            deposited_m * u::m);
          mobile_sediment[cell] +=
            static_cast<float> (deposited_m) * sediment_thickness[u::m];
          deposited_thickness[cell] +=
            static_cast<float> (deposited_m) * sediment_thickness[u::m];
        }

        eroded_volume += stream_sediment_volume_value (routed.detached_total) *
                         stream_cubic_metre;
        deposited_volume +=
          (stream_sediment_volume_value (routed.deposited_total) -
           storage_overflow_m3) *
          stream_cubic_metre;
        exported_sediment_volume +=
          (stream_sediment_volume_value (routed.exported_to_ocean) +
           storage_overflow_m3) *
          stream_cubic_metre;
        fluvial_entrained_cover_volume +=
          stream_sediment_volume_value (routed.entrained_cover_total) *
          stream_cubic_metre;
        fluvial_bedrock_detached_volume +=
          stream_sediment_volume_value (routed.bedrock_detached_total) *
          stream_cubic_metre;
        sediment_balance_residual += routed.balance_residual;
      }

      IterationCount step_sweeps = 0 * one;
      {
        MOPPE_PROFILE_ZONE ("orogeny.route_hillslope_sediment");
        HillslopeTransportResult hillslope =
          route_hillslope_sediment (grid,
                                    next_heights,
                                    mobile_sediment,
                                    boundary,
                                    dt,
                                    parameters.diffusivity);
        step_sweeps = hillslope.sweeps;
        next_heights = std::move (hillslope.heights);
        mobile_sediment = std::move (hillslope.sediment_thickness);
        for (std::size_t cell = 0; cell < count; ++cell) {
          eroded_thickness[cell] += hillslope.eroded_thickness[cell];
          deposited_thickness[cell] += hillslope.deposited_thickness[cell];
        }
        const double transferred_m3 =
          stream_sediment_volume_value (hillslope.transferred);
        eroded_volume += transferred_m3 * stream_cubic_metre;
        deposited_volume += transferred_m3 * stream_cubic_metre;
        sediment_balance_residual += hillslope.balance_residual;
        hillslope_transferred_volume += transferred_m3 * stream_cubic_metre;
        hillslope_bedrock_detached_volume +=
          stream_sediment_volume_value (hillslope.bedrock_detached) *
          stream_cubic_metre;
      }

      if (step_sweeps >
          std::numeric_limits<IterationCount>::max () - hillslope_sweeps)
        throw std::overflow_error (
          "stream-power hillslope sweep count overflow");

      hillslope_sweeps += step_sweeps;
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

    report.hillslope_sweeps = hillslope_sweeps;
    report.tectonic_uplift_volume = tectonic_uplift_volume;
    report.eroded_volume = eroded_volume;
    report.deposited_volume = deposited_volume;
    report.exported_sediment_volume = exported_sediment_volume;
    report.fluvial_entrained_cover_volume = fluvial_entrained_cover_volume;
    report.fluvial_bedrock_detached_volume = fluvial_bedrock_detached_volume;
    report.lake_sediment_storage_volume = lake_sediment_storage_volume;
    report.ocean_mouth_deposition_volume = ocean_mouth_deposition_volume;
    report.sediment_balance_residual = sediment_balance_residual;
    report.hillslope_transferred_volume = hillslope_transferred_volume;
    report.hillslope_bedrock_detached_volume =
      hillslope_bedrock_detached_volume;

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
             .sediment_thickness = std::move (mobile_sediment),
             .eroded_thickness = std::move (eroded_thickness),
             .deposited_thickness = std::move (deposited_thickness),
             .channel_tangents = std::move (channel_memory),
             .report = report };
  }
}
