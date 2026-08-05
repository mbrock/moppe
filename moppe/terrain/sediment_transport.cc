#include <moppe/terrain/sediment_transport.hh>

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <limits>
#include <stdexcept>
#include <vector>

#include <mp-units/math.h>

namespace moppe::terrain {
  namespace {
    constexpr auto cubic_metre =
      mp_units::si::metre * mp_units::si::metre * mp_units::si::metre;

    double sediment_volume_value (SedimentVolume volume) {
      return volume.numerical_value_in (cubic_metre);
    }

    SedimentVolume sediment_volume_from (double cubic_metres) {
      return cubic_metres * sediment_volume[cubic_metre];
    }

    int hillslope_sweep_count (const TerrainDomain& domain,
                               julian_years_f64_t duration,
                               square_meters_per_julian_year_t diffusivity) {
      if (duration == 0 || diffusivity == 0)
        return 0;
      const auto stable_duration =
        1.0 / (2.0 * diffusivity *
               (1.0 / (domain.spacing_x () * domain.spacing_x ()) +
                1.0 / (domain.spacing_z () * domain.spacing_z ())));
      const auto requested =
        mp_units::ceil<mp_units::one> (duration / stable_duration);
      const double requested_count =
        requested.numerical_value_in (mp_units::one);
      if (!mp_units::isfinite (requested) ||
          requested_count > std::numeric_limits<int>::max ())
        throw std::invalid_argument (
          "hillslope transport requests too many sweeps");
      return std::max (1, static_cast<int> (requested_count));
    }

    void
    validate_hillslope_transport (const TerrainDomain& domain,
                                  std::span<const SurfaceElevation> elevations,
                                  std::span<const SedimentThickness> sediment,
                                  std::span<const std::uint8_t> fixed,
                                  julian_years_f64_t duration,
                                  square_meters_per_julian_year_t diffusivity) {
      if (elevations.size () != domain.size () ||
          sediment.size () != domain.size () || fixed.size () != domain.size ())
        throw std::invalid_argument (
          "hillslope transport inputs do not match terrain domain");
      if (!mp_units::isfinite (duration) || duration < 0 ||
          !mp_units::isfinite (diffusivity) || diffusivity < 0)
        throw std::invalid_argument (
          "hillslope transport parameters must be finite and non-negative");
      for (std::size_t cell = 0; cell < domain.size (); ++cell) {
        if (!mp_units::isfinite (elevations[cell].quantity_from_zero ()))
          throw std::invalid_argument ("hillslope elevations must be finite");
        if (!mp_units::isfinite (sediment[cell]) ||
            sediment[cell] < SedimentThickness::zero ())
          throw std::invalid_argument (
            "hillslope sediment must be finite and non-negative");
      }
    }

    void validate_sediment_routing (
      const FractionalFlowDomain& flow,
      std::span<const SedimentVolume> potential_detachment,
      std::span<const SedimentVolume> transport_capacity,
      std::span<const SedimentVolume> maximum_deposition,
      std::span<const std::uint8_t> ocean) {
      const std::size_t count = flow.size ();
      if (potential_detachment.size () != count ||
          transport_capacity.size () != count ||
          maximum_deposition.size () != count || ocean.size () != count)
        throw std::invalid_argument (
          "sediment routing inputs do not match drainage domain");

      std::vector<std::size_t> position (count);
      std::vector<std::uint8_t> seen (count);
      const auto order = flow.topological_order ();
      for (std::size_t i = 0; i < order.size (); ++i) {
        const std::size_t cell = order[i].value;
        if (cell >= count || seen[cell])
          throw std::invalid_argument (
            "sediment routing requires a complete topological order");
        seen[cell] = 1;
        position[cell] = i;
      }

      for (std::size_t cell = 0; cell < count; ++cell) {
        const double potential =
          sediment_volume_value (potential_detachment[cell]);
        const double capacity =
          sediment_volume_value (transport_capacity[cell]);
        const double deposition_limit =
          sediment_volume_value (maximum_deposition[cell]);
        if (!std::isfinite (potential) || potential < 0.0 ||
            !std::isfinite (capacity) || capacity < 0.0 ||
            !std::isfinite (deposition_limit) || deposition_limit < 0.0)
          throw std::invalid_argument (
            "sediment volumes must be finite and non-negative");

        const FractionalFlowRoute& route =
          flow.route (CellIndex { static_cast<std::uint32_t> (cell) });
        if (route.arc_count > route.arcs.size ())
          throw std::invalid_argument (
            "sediment route has too many receiver arcs");

        double fraction_total = 0.0;
        for (std::uint8_t arc = 0; arc < route.arc_count; ++arc) {
          const std::size_t receiver = route.arcs[arc].receiver.value;
          const double fraction =
            route.arcs[arc].fraction.numerical_value_in (mp_units::one);
          if (receiver >= count || position[receiver] <= position[cell] ||
              !std::isfinite (fraction) || fraction < 0.0)
            throw std::invalid_argument (
              "sediment route is not a valid drainage DAG");
          fraction_total += fraction;
        }
        if (route.arc_count != 0 && std::abs (fraction_total - 1.0) > 1e-5)
          throw std::invalid_argument (
            "sediment receiver fractions must sum to one");
      }
    }
  }

  SedimentRoutingResult
  route_sediment (const FractionalFlowDomain& flow,
                  std::span<const SedimentVolume> potential_detachment,
                  std::span<const SedimentVolume> transport_capacity,
                  std::span<const SedimentVolume> maximum_deposition,
                  std::span<const std::uint8_t> ocean) {
    validate_sediment_routing (flow,
                               potential_detachment,
                               transport_capacity,
                               maximum_deposition,
                               ocean);

    const std::size_t count = flow.size ();
    std::vector<double> incoming (count);
    SedimentRoutingResult result {
      .detached = std::vector<SedimentVolume> (count, SedimentVolume::zero ()),
      .deposited = std::vector<SedimentVolume> (count, SedimentVolume::zero ()),
      .outgoing = std::vector<SedimentVolume> (count, SedimentVolume::zero ())
    };

    double detached_total = 0.0;
    double deposited_total = 0.0;
    double exported_total = 0.0;

    for (const CellIndex index : flow.topological_order ()) {
      const std::size_t cell = index.value;
      const FractionalFlowRoute& route = flow.route (index);

      if (ocean[cell]) {
        exported_total += incoming[cell];
        continue;
      }

      if (route.empty ()) {
        result.deposited[cell] = sediment_volume_from (incoming[cell]);
        deposited_total += incoming[cell];
        continue;
      }

      const double capacity = sediment_volume_value (transport_capacity[cell]);
      const double local_detachment =
        std::min (sediment_volume_value (potential_detachment[cell]),
                  std::max (0.0, capacity - incoming[cell]));
      const double available = incoming[cell] + local_detachment;
      const double deposited =
        std::min (std::max (0.0, available - capacity),
                  sediment_volume_value (maximum_deposition[cell]));
      const double outgoing = available - deposited;

      result.detached[cell] = sediment_volume_from (local_detachment);
      result.deposited[cell] = sediment_volume_from (deposited);
      result.outgoing[cell] = sediment_volume_from (outgoing);
      detached_total += local_detachment;
      deposited_total += deposited;

      double posted = 0.0;
      for (std::uint8_t arc = 0; arc < route.arc_count; ++arc) {
        // Make the final posting the exact remainder. Independently rounding
        // every arc would slowly manufacture or destroy sediment at splits.
        const double share =
          arc + 1 == route.arc_count
            ? outgoing - posted
            : outgoing *
                route.arcs[arc].fraction.numerical_value_in (mp_units::one);
        incoming[route.arcs[arc].receiver.value] += share;
        posted += share;
      }
    }

    result.detached_total = sediment_volume_from (detached_total);
    result.deposited_total = sediment_volume_from (deposited_total);
    result.exported_to_ocean = sediment_volume_from (exported_total);
    result.balance_residual =
      (detached_total - deposited_total - exported_total) * cubic_metre;
    return result;
  }

  HillslopeTransportResult
  route_hillslope_sediment (const TerrainDomain& domain,
                            std::span<const SurfaceElevation> elevations,
                            std::span<const SedimentThickness> sediment,
                            std::span<const std::uint8_t> fixed,
                            julian_years_f64_t duration,
                            square_meters_per_julian_year_t diffusivity) {
    validate_hillslope_transport (
      domain, elevations, sediment, fixed, duration, diffusivity);

    const std::size_t count = domain.size ();
    HillslopeTransportResult result {
      .heights = { elevations.begin (), elevations.end () },
      .sediment_thickness = { sediment.begin (), sediment.end () },
      .eroded_thickness =
        std::vector<SedimentThickness> (count, SedimentThickness::zero ()),
      .deposited_thickness =
        std::vector<SedimentThickness> (count, SedimentThickness::zero ()),
    };
    const int sweep_count =
      hillslope_sweep_count (domain, duration, diffusivity);
    result.sweeps = iteration_count (sweep_count);
    if (sweep_count == 0)
      return result;

    const double cell_area_m2 = domain.cell_area ().numerical_value_in (
      mp_units::si::metre * mp_units::si::metre);
    const julian_years_f64_t sweep_duration = duration / sweep_count;
    std::vector<double> height_m (count);
    std::vector<double> sediment_m3 (count);
    std::vector<double> net_m3 (count);
    std::vector<double> outgoing_m3 (count);
    std::vector<double> incoming_m3 (count);
    for (std::size_t cell = 0; cell < count; ++cell) {
      height_m[cell] = surface_elevation_value (elevations[cell]);
      sediment_m3[cell] =
        sediment[cell].numerical_value_in (mp_units::si::metre) * cell_area_m2;
    }

    double transferred_m3 = 0.0;
    double bedrock_detached_m3 = 0.0;
    double residual_m3 = 0.0;
    for (int sweep = 0; sweep < sweep_count; ++sweep) {
      std::fill (net_m3.begin (), net_m3.end (), 0.0);
      std::fill (outgoing_m3.begin (), outgoing_m3.end (), 0.0);
      std::fill (incoming_m3.begin (), incoming_m3.end (), 0.0);

      const auto post_face = [&] (std::size_t first,
                                  std::size_t second,
                                  meters_t face_width,
                                  meters_t run) {
        if (fixed[first] || fixed[second])
          return;
        const double difference_m = height_m[first] - height_m[second];
        if (difference_m == 0.0)
          return;
        const double conductance_m2 =
          (diffusivity * sweep_duration * face_width / run)
            .numerical_value_in (mp_units::si::metre * mp_units::si::metre);
        const double volume_m3 = std::abs (difference_m) * conductance_m2;
        const std::size_t source = difference_m > 0.0 ? first : second;
        const std::size_t destination = difference_m > 0.0 ? second : first;
        net_m3[source] -= volume_m3;
        net_m3[destination] += volume_m3;
        outgoing_m3[source] += volume_m3;
        incoming_m3[destination] += volume_m3;
      };

      for (std::size_t cell = 0; cell < count; ++cell) {
        const TerrainIndex index = domain.index (cell);
        post_face (cell,
                   domain.offset (domain.shifted (index, 1, 0)),
                   domain.spacing_z (),
                   domain.spacing_x ());
        post_face (cell,
                   domain.offset (domain.shifted (index, 0, 1)),
                   domain.spacing_x (),
                   domain.spacing_z ());
      }

      for (std::size_t cell = 0; cell < count; ++cell) {
        const double cover_removed =
          std::min (sediment_m3[cell], outgoing_m3[cell]);
        bedrock_detached_m3 += outgoing_m3[cell] - cover_removed;
        sediment_m3[cell] =
          std::max (0.0, sediment_m3[cell] - cover_removed + incoming_m3[cell]);
        height_m[cell] += net_m3[cell] / cell_area_m2;
        result.eroded_thickness[cell] +=
          static_cast<float> (outgoing_m3[cell] / cell_area_m2) *
          sediment_thickness[mp_units::si::metre];
        result.deposited_thickness[cell] +=
          static_cast<float> (incoming_m3[cell] / cell_area_m2) *
          sediment_thickness[mp_units::si::metre];
        residual_m3 += net_m3[cell];
        transferred_m3 += outgoing_m3[cell];
      }
    }

    for (std::size_t cell = 0; cell < count; ++cell) {
      result.heights[cell] = surface_elevation_point (
        static_cast<float> (height_m[cell]) * mp_units::si::metre);
      result.sediment_thickness[cell] =
        static_cast<float> (sediment_m3[cell] / cell_area_m2) *
        sediment_thickness[mp_units::si::metre];
    }
    result.transferred = sediment_volume_from (transferred_m3);
    result.bedrock_detached = sediment_volume_from (bedrock_detached_m3);
    result.balance_residual = residual_m3 * cubic_metre;
    return result;
  }
}
