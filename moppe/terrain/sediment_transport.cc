#include <moppe/terrain/sediment_transport.hh>

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <stdexcept>
#include <vector>

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
}
