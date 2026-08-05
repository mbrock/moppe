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
      std::span<const std::uint8_t> ocean,
      std::span<const SedimentVolume> available_cover) {
      const std::size_t count = flow.size ();
      if (potential_detachment.size () != count ||
          transport_capacity.size () != count ||
          maximum_deposition.size () != count || ocean.size () != count)
        throw std::invalid_argument (
          "sediment routing inputs do not match drainage domain");
      if (!available_cover.empty () && available_cover.size () != count)
        throw std::invalid_argument (
          "sediment cover does not match drainage domain");

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
        const double cover = available_cover.empty ()
                               ? 0.0
                               : sediment_volume_value (available_cover[cell]);
        if (!std::isfinite (potential) || potential < 0.0 ||
            !std::isfinite (capacity) || capacity < 0.0 ||
            !std::isfinite (deposition_limit) || deposition_limit < 0.0 ||
            !std::isfinite (cover) || cover < 0.0)
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

  SedimentVolume
  sediment_transport_capacity (julian_years_f64_t duration,
                               square_meters_t contributing_area,
                               slope_t slope,
                               float channel_share,
                               const FluvialTransport& parameters) {
    if (!mp_units::isfinite (duration) || duration < 0 ||
        !mp_units::isfinite (contributing_area) || contributing_area < 0 ||
        !mp_units::isfinite (slope) || slope < 0 ||
        !std::isfinite (channel_share) || channel_share < 0.0f ||
        channel_share > 1.0f || !mp_units::isfinite (parameters.runoff_rate) ||
        parameters.runoff_rate < 0 ||
        !mp_units::isfinite (parameters.concentration_at_unit_slope) ||
        parameters.concentration_at_unit_slope < 0)
      throw std::invalid_argument (
        "fluvial transport parameters must be finite and non-negative");

    const auto capacity =
      duration * contributing_area * parameters.runoff_rate *
      parameters.concentration_at_unit_slope * slope * channel_share;
    return sediment_volume_from (capacity.numerical_value_in (cubic_metre));
  }

  meters_t alluvial_valley_width (square_meters_t contributing_area,
                                  const ValleyDeposition& parameters) noexcept {
    const float area_m2 =
      std::max (0.0f,
                contributing_area.numerical_value_in (mp_units::si::metre *
                                                      mp_units::si::metre));
    const float minimum_m = std::max (
      0.0f, parameters.minimum_width.numerical_value_in (mp_units::si::metre));
    const float maximum_m = std::max (
      minimum_m,
      parameters.maximum_width.numerical_value_in (mp_units::si::metre));
    const float width_ratio = std::max (
      0.0f, parameters.width_per_sqrt_area.numerical_value_in (mp_units::one));
    return std::clamp (minimum_m + width_ratio * std::sqrt (area_m2),
                       minimum_m,
                       maximum_m) *
           mp_units::si::metre;
  }

  SedimentRoutingResult
  route_sediment (const FractionalFlowDomain& flow,
                  std::span<const SedimentVolume> potential_detachment,
                  std::span<const SedimentVolume> transport_capacity,
                  std::span<const SedimentVolume> maximum_deposition,
                  std::span<const std::uint8_t> ocean,
                  std::span<const SedimentVolume> available_cover) {
    validate_sediment_routing (flow,
                               potential_detachment,
                               transport_capacity,
                               maximum_deposition,
                               ocean,
                               available_cover);

    const std::size_t count = flow.size ();
    std::vector<double> incoming (count);
    SedimentRoutingResult result {
      .detached = std::vector<SedimentVolume> (count, SedimentVolume::zero ()),
      .entrained_cover =
        std::vector<SedimentVolume> (count, SedimentVolume::zero ()),
      .bedrock_detached =
        std::vector<SedimentVolume> (count, SedimentVolume::zero ()),
      .deposited = std::vector<SedimentVolume> (count, SedimentVolume::zero ()),
      .outgoing = std::vector<SedimentVolume> (count, SedimentVolume::zero ())
    };

    double detached_total = 0.0;
    double entrained_cover_total = 0.0;
    double bedrock_detached_total = 0.0;
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
      double spare_capacity = std::max (0.0, capacity - incoming[cell]);
      const double cover = available_cover.empty ()
                             ? 0.0
                             : sediment_volume_value (available_cover[cell]);
      const double entrained_cover = std::min (cover, spare_capacity);
      spare_capacity -= entrained_cover;
      const double bedrock_detachment = std::min (
        sediment_volume_value (potential_detachment[cell]), spare_capacity);
      const double local_detachment = entrained_cover + bedrock_detachment;
      const double available = incoming[cell] + local_detachment;
      const double deposited =
        std::min (std::max (0.0, available - capacity),
                  sediment_volume_value (maximum_deposition[cell]));
      const double outgoing = available - deposited;

      result.detached[cell] = sediment_volume_from (local_detachment);
      result.entrained_cover[cell] = sediment_volume_from (entrained_cover);
      result.bedrock_detached[cell] = sediment_volume_from (bedrock_detachment);
      result.deposited[cell] = sediment_volume_from (deposited);
      result.outgoing[cell] = sediment_volume_from (outgoing);
      detached_total += local_detachment;
      entrained_cover_total += entrained_cover;
      bedrock_detached_total += bedrock_detachment;
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
    result.entrained_cover_total = sediment_volume_from (entrained_cover_total);
    result.bedrock_detached_total =
      sediment_volume_from (bedrock_detached_total);
    result.deposited_total = sediment_volume_from (deposited_total);
    result.exported_to_ocean = sediment_volume_from (exported_total);
    result.balance_residual =
      (detached_total - deposited_total - exported_total) * cubic_metre;
    return result;
  }

  LateralDepositionResult spread_valley_deposition (
    const FractionalFlowDomain& flow,
    std::span<const SurfaceElevation> elevations,
    std::span<const FractionalContributingArea> contributing_areas,
    std::span<const ChannelTangent> channel_tangents,
    std::span<const SedimentVolume> centerline_deposition,
    std::span<const std::uint8_t> ocean,
    const ValleyDeposition& parameters) {
    const TerrainDomain& domain = flow.terrain_domain ();
    const std::size_t count = domain.size ();
    if (elevations.size () != count || contributing_areas.size () != count ||
        channel_tangents.size () != count ||
        centerline_deposition.size () != count || ocean.size () != count)
      throw std::invalid_argument (
        "valley deposition inputs do not match terrain domain");

    const double cell_area_m2 = domain.cell_area ().numerical_value_in (
      mp_units::si::metre * mp_units::si::metre);
    const float spacing_x_m =
      domain.spacing_x ().numerical_value_in (mp_units::si::metre);
    const float spacing_z_m =
      domain.spacing_z ().numerical_value_in (mp_units::si::metre);
    const float along_radius_m = 0.75f * std::hypot (spacing_x_m, spacing_z_m);
    const float minimum_wall_relief_m = std::max (
      0.0f,
      parameters.minimum_wall_relief.numerical_value_in (mp_units::si::metre));
    const float wall_relief_ratio = std::max (
      0.0f,
      parameters.wall_relief_per_width.numerical_value_in (mp_units::one));

    LateralDepositionResult result { .deposited = std::vector<SedimentVolume> (
                                       count, SedimentVolume::zero ()) };
    std::vector<double> distributed_m3 (count);
    std::vector<double> working_height_m (count);
    for (std::size_t cell = 0; cell < count; ++cell) {
      if (!mp_units::isfinite (elevations[cell].quantity_from_zero ()) ||
          !mp_units::isfinite (contributing_areas[cell]) ||
          contributing_areas[cell] < FractionalContributingArea::zero () ||
          !mp_units::isfinite (channel_tangents[cell].magnitude ()))
        throw std::invalid_argument (
          "valley deposition readings must be finite and non-negative");
      const double volume_m3 =
        sediment_volume_value (centerline_deposition[cell]);
      if (!std::isfinite (volume_m3) || volume_m3 < 0.0)
        throw std::invalid_argument (
          "valley deposition volumes must be finite and non-negative");
      working_height_m[cell] = surface_elevation_value (elevations[cell]);
    }

    struct Candidate {
      std::size_t cell;
      double height_m;
    };
    double input_total_m3 = 0.0;
    double output_total_m3 = 0.0;
    for (std::size_t source = 0; source < count; ++source) {
      const double source_volume_m3 =
        sediment_volume_value (centerline_deposition[source]);
      input_total_m3 += source_volume_m3;
      if (source_volume_m3 == 0.0)
        continue;
      if (ocean[source])
        throw std::invalid_argument (
          "valley deposition cannot originate in the ocean");

      Vec3 tangent =
        channel_tangents[source].numerical_value_in (mp_units::one);
      float tangent_length = std::hypot (tangent[0], tangent[2]);
      if (tangent_length <= 1e-6f) {
        const CellIndex source_cell { static_cast<std::uint32_t> (source) };
        const FractionalFlowRoute& route = flow.route (source_cell);
        if (!route.empty ()) {
          const TerrainIndex from = domain.index (source);
          const TerrainIndex to = domain.index (route.arcs[0].receiver.value);
          tangent[0] = minimum_image_delta (
            (static_cast<float> (to.column) -
             static_cast<float> (from.column)) *
              spacing_x_m,
            domain.period_x ().numerical_value_in (mp_units::si::metre));
          tangent[2] = minimum_image_delta (
            (static_cast<float> (to.row) - static_cast<float> (from.row)) *
              spacing_z_m,
            domain.period_z ().numerical_value_in (mp_units::si::metre));
          tangent_length = std::hypot (tangent[0], tangent[2]);
        }
      }
      if (tangent_length <= 1e-6f) {
        tangent = Vec3 (0.0f, 0.0f, 1.0f);
      } else {
        tangent /= tangent_length;
      }

      const square_meters_t area = square_meters_t (contributing_areas[source]);
      const float width_m = alluvial_valley_width (area, parameters)
                              .numerical_value_in (mp_units::si::metre);
      const float half_width_m = 0.5f * width_m;
      const float wall_relief_m =
        minimum_wall_relief_m + wall_relief_ratio * width_m;
      const float search_radius_m = half_width_m + along_radius_m;
      const int radius_x =
        static_cast<int> (std::ceil (search_radius_m / spacing_x_m));
      const int radius_z =
        static_cast<int> (std::ceil (search_radius_m / spacing_z_m));
      const TerrainIndex center = domain.index (source);
      const double ceiling_m = working_height_m[source] + wall_relief_m;

      std::vector<std::size_t> footprint;
      for (int dz = -radius_z; dz <= radius_z; ++dz)
        for (int dx = -radius_x; dx <= radius_x; ++dx) {
          const float offset_x_m = static_cast<float> (dx) * spacing_x_m;
          const float offset_z_m = static_cast<float> (dz) * spacing_z_m;
          const float along_m =
            std::fabs (offset_x_m * tangent[0] + offset_z_m * tangent[2]);
          const float across_m =
            std::fabs (-offset_x_m * tangent[2] + offset_z_m * tangent[0]);
          if (along_m > along_radius_m || across_m > half_width_m)
            continue;
          const TerrainIndex site = domain.shifted (center, dx, dz);
          const std::size_t cell = domain.offset (site);
          if (!ocean[cell] && working_height_m[cell] <= ceiling_m)
            footprint.push_back (cell);
        }
      std::ranges::sort (footprint);
      const auto unique = std::ranges::unique (footprint);
      footprint.erase (unique.begin (), unique.end ());
      if (footprint.empty ())
        footprint.push_back (source);

      std::vector<Candidate> candidates;
      candidates.reserve (footprint.size ());
      for (const std::size_t cell : footprint)
        candidates.push_back ({ cell, working_height_m[cell] });
      std::ranges::sort (candidates, {}, &Candidate::height_m);

      double remaining_m3 = source_volume_m3;
      std::size_t active = 1;
      double floor_level_m = candidates.front ().height_m;
      while (active < candidates.size ()) {
        const double next_level_m = candidates[active].height_m;
        const double needed_m3 = (next_level_m - floor_level_m) * cell_area_m2 *
                                 static_cast<double> (active);
        if (remaining_m3 < needed_m3)
          break;
        remaining_m3 -= needed_m3;
        floor_level_m = next_level_m;
        ++active;
      }
      floor_level_m +=
        remaining_m3 / (cell_area_m2 * static_cast<double> (active));

      double posted_m3 = 0.0;
      for (std::size_t position = 0; position < active; ++position) {
        const std::size_t cell = candidates[position].cell;
        const double share_m3 =
          position + 1 == active
            ? source_volume_m3 - posted_m3
            : (floor_level_m - candidates[position].height_m) * cell_area_m2;
        distributed_m3[cell] += share_m3;
        working_height_m[cell] += share_m3 / cell_area_m2;
        posted_m3 += share_m3;
      }
      output_total_m3 += posted_m3;
    }

    for (std::size_t cell = 0; cell < count; ++cell)
      result.deposited[cell] = sediment_volume_from (distributed_m3[cell]);
    result.balance_residual = (input_total_m3 - output_total_m3) * cubic_metre;
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
