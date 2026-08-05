#include <moppe/terrain/sediment_transport.hh>

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <limits>
#include <numeric>
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

    double hillslope_diffusivity_multiplier (
      double face_gradient,
      proportion_t critical_gradient,
      proportion_t maximum_diffusivity_multiplier) {
      const double critical =
        critical_gradient.numerical_value_in (mp_units::one);
      const double maximum =
        maximum_diffusivity_multiplier.numerical_value_in (mp_units::one);
      const double activation =
        std::clamp (2.0 * face_gradient / critical - 1.0, 0.0, 1.0);
      const double smooth_activation =
        activation * activation * (3.0 - 2.0 * activation);
      return 1.0 + (maximum - 1.0) * smooth_activation;
    }

    int hillslope_sweep_count (const TerrainDomain& domain,
                               julian_years_f64_t duration,
                               square_meters_per_julian_year_t diffusivity,
                               double maximum_diffusivity_multiplier) {
      if (duration == 0 || diffusivity == 0)
        return 0;
      const auto maximum_diffusivity =
        maximum_diffusivity_multiplier * diffusivity;
      const auto stable_duration =
        1.0 / (2.0 * maximum_diffusivity *
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
                                  square_meters_per_julian_year_t diffusivity,
                                  proportion_t critical_gradient,
                                  proportion_t maximum_diffusivity_multiplier) {
      if (elevations.size () != domain.size () ||
          sediment.size () != domain.size () || fixed.size () != domain.size ())
        throw std::invalid_argument (
          "hillslope transport inputs do not match terrain domain");
      if (!mp_units::isfinite (duration) || duration < 0 ||
          !mp_units::isfinite (diffusivity) || diffusivity < 0 ||
          !mp_units::isfinite (critical_gradient) || critical_gradient <= 0 ||
          !mp_units::isfinite (maximum_diffusivity_multiplier) ||
          maximum_diffusivity_multiplier < 1 * mp_units::one)
        throw std::invalid_argument (
          "hillslope transport parameters are outside their physical range");
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
      std::span<const SedimentVolume> available_cover,
      std::span<const WaterBodyId> water_body,
      std::span<const SedimentVolume> body_storage_capacity,
      std::span<const SedimentVolume> ocean_mouth_capacity) {
      const std::size_t count = flow.size ();
      if (potential_detachment.size () != count ||
          transport_capacity.size () != count ||
          maximum_deposition.size () != count || ocean.size () != count)
        throw std::invalid_argument (
          "sediment routing inputs do not match drainage domain");
      if (!available_cover.empty () && available_cover.size () != count)
        throw std::invalid_argument (
          "sediment cover does not match drainage domain");
      if ((!water_body.empty () && water_body.size () != count) ||
          (!ocean_mouth_capacity.empty () &&
           ocean_mouth_capacity.size () != count))
        throw std::invalid_argument (
          "standing-water storage does not match drainage domain");
      if (water_body.empty () != ocean_mouth_capacity.empty ())
        throw std::invalid_argument (
          "standing-water storage inputs must be supplied together");
      if (water_body.empty () && !body_storage_capacity.empty ())
        throw std::invalid_argument (
          "water-body storage requires standing-water membership");
      for (const SedimentVolume capacity : body_storage_capacity) {
        const double value = sediment_volume_value (capacity);
        if (!std::isfinite (value) || value < 0.0)
          throw std::invalid_argument (
            "water-body storage must be finite and non-negative");
      }

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
        const double ocean_storage =
          ocean_mouth_capacity.empty ()
            ? 0.0
            : sediment_volume_value (ocean_mouth_capacity[cell]);
        if (!std::isfinite (potential) || potential < 0.0 ||
            !std::isfinite (capacity) || capacity < 0.0 ||
            !std::isfinite (deposition_limit) || deposition_limit < 0.0 ||
            !std::isfinite (cover) || cover < 0.0 ||
            !std::isfinite (ocean_storage) || ocean_storage < 0.0)
          throw std::invalid_argument (
            "sediment volumes must be finite and non-negative");
        if (!water_body.empty () && water_body[cell] != no_water_body &&
            !body_storage_capacity.empty () &&
            water_body[cell].value >= body_storage_capacity.size ())
          throw std::invalid_argument (
            "sediment water-body identifier is outside storage domain");

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

  StandingWaterStorage standing_water_storage_capacity (
    const FloodField& flood,
    const LakeCensus& census,
    std::span<const FractionalContributingArea> contributing_areas,
    std::span<const SedimentVolume> maximum_deposition,
    const ValleyDeposition& parameters) {
    const std::size_t count = flood.domain ().size ();
    if (census.cell_count () != count || contributing_areas.size () != count ||
        maximum_deposition.size () != count)
      throw std::invalid_argument (
        "standing-water capacity inputs do not share a domain");

    std::vector<double> body_capacity_m3 (census.domain ().size ());
    std::vector<double> ocean_capacity_m3 (count);
    const double cell_area_m2 =
      flood.domain ().cell_area ().numerical_value_in (mp_units::si::metre *
                                                       mp_units::si::metre);
    for (std::size_t cell = 0; cell < count; ++cell) {
      const double maximum_m3 =
        sediment_volume_value (maximum_deposition[cell]);
      if (!std::isfinite (maximum_m3) || maximum_m3 < 0.0 ||
          !mp_units::isfinite (contributing_areas[cell]) ||
          contributing_areas[cell] < FractionalContributingArea::zero ())
        throw std::invalid_argument (
          "standing-water capacity readings must be finite and non-negative");
      const WaterBodyId body =
        census.body_at (CellIndex { static_cast<std::uint32_t> (cell) });
      if (body == LakeCensus::dry)
        continue;
      const double accommodation_m3 =
        static_cast<double> (flood.water_depth_m (cell)) * cell_area_m2;
      if (flood.ocean[cell]) {
        const float width_m =
          alluvial_valley_width (square_meters_t (contributing_areas[cell]),
                                 parameters)
            .numerical_value_in (mp_units::si::metre);
        const double footprint_cells =
          std::max (1.0, 0.25 * width_m * width_m / cell_area_m2);
        ocean_capacity_m3[cell] =
          std::min (accommodation_m3, maximum_m3 * footprint_cells);
      } else {
        body_capacity_m3[body.value] += std::min (accommodation_m3, maximum_m3);
      }
    }

    StandingWaterStorage result;
    result.body_capacity.reserve (body_capacity_m3.size ());
    for (const double capacity_m3 : body_capacity_m3)
      result.body_capacity.push_back (sediment_volume_from (capacity_m3));
    result.ocean_mouth_capacity.reserve (count);
    for (const double capacity_m3 : ocean_capacity_m3)
      result.ocean_mouth_capacity.push_back (
        sediment_volume_from (capacity_m3));
    return result;
  }

  SedimentRoutingResult
  route_sediment (const FractionalFlowDomain& flow,
                  std::span<const SedimentVolume> potential_detachment,
                  std::span<const SedimentVolume> transport_capacity,
                  std::span<const SedimentVolume> maximum_deposition,
                  std::span<const std::uint8_t> ocean,
                  std::span<const SedimentVolume> available_cover,
                  std::span<const WaterBodyId> water_body,
                  std::span<const SedimentVolume> body_storage_capacity,
                  std::span<const SedimentVolume> ocean_mouth_capacity) {
    validate_sediment_routing (flow,
                               potential_detachment,
                               transport_capacity,
                               maximum_deposition,
                               ocean,
                               available_cover,
                               water_body,
                               body_storage_capacity,
                               ocean_mouth_capacity);

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
    std::vector<double> remaining_body_storage;
    remaining_body_storage.reserve (body_storage_capacity.size ());
    for (const SedimentVolume capacity : body_storage_capacity)
      remaining_body_storage.push_back (sediment_volume_value (capacity));

    for (const CellIndex index : flow.topological_order ()) {
      const std::size_t cell = index.value;
      const FractionalFlowRoute& route = flow.route (index);

      if (ocean[cell]) {
        const double deposited =
          ocean_mouth_capacity.empty ()
            ? 0.0
            : std::min (incoming[cell],
                        sediment_volume_value (ocean_mouth_capacity[cell]));
        result.deposited[cell] = sediment_volume_from (deposited);
        deposited_total += deposited;
        exported_total += incoming[cell] - deposited;
        continue;
      }

      const WaterBodyId body =
        water_body.empty () ? no_water_body : water_body[cell];
      if (body != no_water_body && !route.empty ()) {
        const double deposited =
          std::min (incoming[cell], remaining_body_storage[body.value]);
        remaining_body_storage[body.value] -= deposited;
        const double outgoing = incoming[cell] - deposited;
        result.deposited[cell] = sediment_volume_from (deposited);
        result.outgoing[cell] = sediment_volume_from (outgoing);
        deposited_total += deposited;

        double posted = 0.0;
        for (std::uint8_t arc = 0; arc < route.arc_count; ++arc) {
          const double share =
            arc + 1 == route.arc_count
              ? outgoing - posted
              : outgoing *
                  route.arcs[arc].fraction.numerical_value_in (mp_units::one);
          incoming[route.arcs[arc].receiver.value] += share;
          posted += share;
        }
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

  StandingWaterDepositionResult spread_standing_water_deposition (
    const FloodField& flood,
    const LakeCensus& census,
    const FractionalFlowDomain& flow,
    std::span<const FractionalContributingArea> contributing_areas,
    std::span<const ChannelTangent> channel_tangents,
    std::span<const SedimentVolume> centerline_deposition,
    std::span<const SedimentVolume> maximum_deposition,
    const ValleyDeposition& parameters) {
    const TerrainDomain& domain = flood.domain ();
    const std::size_t count = domain.size ();
    if (flow.terrain_domain () != domain || census.cell_count () != count ||
        contributing_areas.size () != count ||
        channel_tangents.size () != count ||
        centerline_deposition.size () != count ||
        maximum_deposition.size () != count)
      throw std::invalid_argument (
        "standing-water deposition inputs do not share a domain");

    StandingWaterDepositionResult result {
      .dry_centerline =
        std::vector<SedimentVolume> (count, SedimentVolume::zero ()),
      .deposited = std::vector<SedimentVolume> (count, SedimentVolume::zero ())
    };
    std::vector<double> deposited_m3 (count);
    std::vector<double> remaining_capacity_m3 (count);
    std::vector<double> body_load_m3 (census.domain ().size ());
    std::vector<double> body_capacity_m3 (census.domain ().size ());
    std::vector<std::vector<std::size_t>> body_cells (census.domain ().size ());
    const double cell_area_m2 = domain.cell_area ().numerical_value_in (
      mp_units::si::metre * mp_units::si::metre);
    const float spacing_x_m =
      domain.spacing_x ().numerical_value_in (mp_units::si::metre);
    const float spacing_z_m =
      domain.spacing_z ().numerical_value_in (mp_units::si::metre);
    const float cell_diagonal_m = std::hypot (spacing_x_m, spacing_z_m);
    const std::size_t no_footprint_position =
      std::numeric_limits<std::size_t>::max ();
    std::vector<std::size_t> footprint_position (count, no_footprint_position);

    double input_m3 = 0.0;
    double dry_m3 = 0.0;
    double lake_m3 = 0.0;
    double ocean_m3 = 0.0;
    double exported_m3 = 0.0;
    for (std::size_t cell = 0; cell < count; ++cell) {
      const double load_m3 =
        sediment_volume_value (centerline_deposition[cell]);
      const double maximum_m3 =
        sediment_volume_value (maximum_deposition[cell]);
      if (!std::isfinite (load_m3) || load_m3 < 0.0 ||
          !std::isfinite (maximum_m3) || maximum_m3 < 0.0)
        throw std::invalid_argument (
          "standing-water deposition volumes must be finite and non-negative");
      input_m3 += load_m3;
      const WaterBodyId body =
        census.body_at (CellIndex { static_cast<std::uint32_t> (cell) });
      if (body != LakeCensus::dry) {
        const double accommodation_m3 =
          static_cast<double> (flood.water_depth_m (cell)) * cell_area_m2;
        remaining_capacity_m3[cell] = std::min (accommodation_m3, maximum_m3);
      }
      if (body == LakeCensus::dry) {
        result.dry_centerline[cell] = centerline_deposition[cell];
        dry_m3 += load_m3;
        continue;
      }
      if (!flood.ocean[cell]) {
        body_load_m3[body.value] += load_m3;
        body_capacity_m3[body.value] += remaining_capacity_m3[cell];
        body_cells[body.value].push_back (cell);
        continue;
      }
      if (load_m3 == 0.0)
        continue;

      Vec3 tangent = channel_tangents[cell].numerical_value_in (mp_units::one);
      const float length = std::hypot (tangent[0], tangent[2]);
      tangent = length > 1e-6f ? tangent / length : Vec3 (0.0f, 0.0f, 1.0f);
      const float width_m =
        alluvial_valley_width (square_meters_t (contributing_areas[cell]),
                               parameters)
          .numerical_value_in (mp_units::si::metre);
      const float search_m = width_m + cell_diagonal_m;
      const int radius_x =
        static_cast<int> (std::ceil (search_m / spacing_x_m));
      const int radius_z =
        static_cast<int> (std::ceil (search_m / spacing_z_m));
      const TerrainIndex center = domain.index (cell);
      struct MouthCell {
        std::size_t cell;
        double weight;
      };
      std::vector<MouthCell> footprint;
      for (int dz = -radius_z; dz <= radius_z; ++dz)
        for (int dx = -radius_x; dx <= radius_x; ++dx) {
          const float offset_x_m = static_cast<float> (dx) * spacing_x_m;
          const float offset_z_m = static_cast<float> (dz) * spacing_z_m;
          const float along_m =
            offset_x_m * tangent[0] + offset_z_m * tangent[2];
          if (along_m < -cell_diagonal_m || along_m > width_m)
            continue;
          const float progress =
            std::clamp (along_m / std::max (width_m, 1e-6f), 0.0f, 1.0f);
          const float half_width_m =
            0.5f * width_m * std::lerp (0.25f, 1.0f, progress);
          const float across_m =
            std::fabs (-offset_x_m * tangent[2] + offset_z_m * tangent[0]);
          if (across_m > half_width_m)
            continue;
          const std::size_t destination =
            domain.offset (domain.shifted (center, dx, dz));
          if (!flood.ocean[destination])
            continue;
          if (remaining_capacity_m3[destination] <= 0.0)
            continue;
          const double distance_m = std::hypot (offset_x_m, offset_z_m);
          const double weight = remaining_capacity_m3[destination] *
                                (1.0 - 0.5 * distance_m / search_m);
          std::size_t& position = footprint_position[destination];
          if (position == no_footprint_position) {
            position = footprint.size ();
            footprint.push_back ({ destination, weight });
          } else {
            footprint[position].weight =
              std::max (footprint[position].weight, weight);
          }
        }
      for (const MouthCell& destination : footprint)
        footprint_position[destination.cell] = no_footprint_position;

      double remaining_m3 = load_m3;
      while (remaining_m3 > 1e-12 && !footprint.empty ()) {
        const double weight_total =
          std::accumulate (footprint.begin (),
                           footprint.end (),
                           0.0,
                           [] (double sum, const MouthCell& destination) {
                             return sum + destination.weight;
                           });
        if (weight_total <= 0.0)
          break;
        const double pass_load_m3 = remaining_m3;
        double posted_m3 = 0.0;
        std::vector<MouthCell> still_open;
        for (const MouthCell& destination : footprint) {
          const double requested_m3 =
            pass_load_m3 * destination.weight / weight_total;
          const double share_m3 =
            std::min (requested_m3, remaining_capacity_m3[destination.cell]);
          deposited_m3[destination.cell] += share_m3;
          remaining_capacity_m3[destination.cell] -= share_m3;
          posted_m3 += share_m3;
          if (remaining_capacity_m3[destination.cell] > 1e-12)
            still_open.push_back (destination);
        }
        if (posted_m3 <= 1e-12)
          break;
        remaining_m3 -= posted_m3;
        footprint = std::move (still_open);
      }
      ocean_m3 += load_m3 - remaining_m3;
      exported_m3 += remaining_m3;
    }

    for (std::size_t body = 0; body < body_cells.size (); ++body) {
      const double load_m3 = body_load_m3[body];
      if (load_m3 == 0.0)
        continue;
      const double capacity_m3 = body_capacity_m3[body];
      if (capacity_m3 <= 0.0) {
        exported_m3 += load_m3;
        continue;
      }
      const double stored_m3 = std::min (load_m3, capacity_m3);
      double posted_m3 = 0.0;
      for (std::size_t position = 0; position < body_cells[body].size ();
           ++position) {
        const std::size_t cell = body_cells[body][position];
        const double local_capacity_m3 = remaining_capacity_m3[cell];
        const double share_m3 = position + 1 == body_cells[body].size ()
                                  ? stored_m3 - posted_m3
                                  : stored_m3 * local_capacity_m3 / capacity_m3;
        deposited_m3[cell] += share_m3;
        remaining_capacity_m3[cell] =
          std::max (0.0, remaining_capacity_m3[cell] - share_m3);
        posted_m3 += share_m3;
      }
      lake_m3 += posted_m3;
      exported_m3 += load_m3 - posted_m3;
    }

    double output_m3 = dry_m3 + exported_m3;
    for (std::size_t cell = 0; cell < count; ++cell) {
      result.deposited[cell] = sediment_volume_from (deposited_m3[cell]);
      output_m3 += deposited_m3[cell];
    }
    result.lake_storage = sediment_volume_from (lake_m3);
    result.ocean_mouth_storage = sediment_volume_from (ocean_m3);
    result.exported = sediment_volume_from (exported_m3);
    result.balance_residual = (input_m3 - output_m3) * cubic_metre;
    return result;
  }

  HillslopeTransportResult
  route_hillslope_sediment (const TerrainDomain& domain,
                            std::span<const SurfaceElevation> elevations,
                            std::span<const SedimentThickness> sediment,
                            std::span<const std::uint8_t> fixed,
                            julian_years_f64_t duration,
                            square_meters_per_julian_year_t diffusivity,
                            proportion_t critical_gradient,
                            proportion_t maximum_diffusivity_multiplier) {
    validate_hillslope_transport (domain,
                                  elevations,
                                  sediment,
                                  fixed,
                                  duration,
                                  diffusivity,
                                  critical_gradient,
                                  maximum_diffusivity_multiplier);

    const std::size_t count = domain.size ();
    HillslopeTransportResult result {
      .heights = { elevations.begin (), elevations.end () },
      .sediment_thickness = { sediment.begin (), sediment.end () },
      .eroded_thickness =
        std::vector<SedimentThickness> (count, SedimentThickness::zero ()),
      .deposited_thickness =
        std::vector<SedimentThickness> (count, SedimentThickness::zero ()),
    };
    bool nonlinear_flux_active = false;
    for (std::size_t cell = 0; cell < count; ++cell) {
      if (fixed[cell])
        continue;
      const TerrainIndex index = domain.index (cell);
      const auto inspect_face = [&] (TerrainIndex neighbor, meters_t run) {
        const std::size_t other = domain.offset (neighbor);
        if (fixed[other])
          return;
        const double difference_m =
          std::abs (surface_elevation_value (elevations[cell]) -
                    surface_elevation_value (elevations[other]));
        const double gradient =
          difference_m / run.numerical_value_in (mp_units::si::metre);
        nonlinear_flux_active |=
          hillslope_diffusivity_multiplier (
            gradient, critical_gradient, maximum_diffusivity_multiplier) > 1.0;
      };
      inspect_face (domain.shifted (index, 1, 0), domain.spacing_x ());
      inspect_face (domain.shifted (index, 0, 1), domain.spacing_z ());
    }
    const double stability_multiplier =
      nonlinear_flux_active
        ? maximum_diffusivity_multiplier.numerical_value_in (mp_units::one)
        : 1.0;
    const int sweep_count = hillslope_sweep_count (
      domain, duration, diffusivity, stability_multiplier);
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
        const double face_gradient =
          std::abs (difference_m) /
          run.numerical_value_in (mp_units::si::metre);
        const double local_multiplier = hillslope_diffusivity_multiplier (
          face_gradient, critical_gradient, maximum_diffusivity_multiplier);
        const double conductance_m2 =
          (local_multiplier * diffusivity * sweep_duration * face_width / run)
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
