#include <moppe/correct_math.hh>
#include <moppe/parallel.hh>
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

    // The routers carry their working values at double precision and narrow
    // once when a result is stored, so these are the double forms of the
    // header's float quantities.
    using WorkingElevation =
      quantity_point<surface_elevation[u::m],
                     default_point_origin (surface_elevation[u::m]),
                     double>;
    using WorkingElevationChange = quantity<surface_elevation[u::m], double>;
    using WorkingArea = quantity<u::m * u::m, double>;
    // One signed lane of the ground's gradient, and its magnitude.
    using SurfaceGradient = quantity<grade[one], double>;
    using GradientMagnitude = quantity<terrain_slope[one], double>;
    using DiffusivityMultiplier = quantity<scale_factor[one], double>;
    using ActivationWeight = quantity<blend_weight[one], double>;
    using FractionTotal = quantity<flow_fraction[one], double>;
    // How many cells a mouth's deposit may spread over.
    using FootprintCells = quantity<cell_count[one], double>;

    // A product of lengths in cubic metres, named as solid. The unit is
    // already cubic metres, so this changes no bits.
    template <mp_units::Quantity Q>
    SedimentVolume sediment_volume_of (Q volume) {
      return volume.numerical_value_in (cubic_metre) *
             sediment_volume[cubic_metre];
    }

    // The thickness a volume makes over an area, as a height change.
    WorkingElevationChange thickness_over (SedimentVolume volume,
                                           WorkingArea area) {
      return (volume / area).numerical_value_in (u::m) *
             surface_elevation[u::m];
    }

    DiffusivityMultiplier hillslope_diffusivity_multiplier (
      GradientMagnitude gradient_magnitude,
      proportion_t critical_gradient,
      proportion_t maximum_diffusivity_multiplier) {
      const GradientMagnitude critical =
        static_cast<double> (critical_gradient.numerical_value_in (one)) *
        terrain_slope[one];
      const DiffusivityMultiplier maximum =
        static_cast<double> (
          maximum_diffusivity_multiplier.numerical_value_in (one)) *
        scale_factor[one];
      const DiffusivityMultiplier unity = 1.0 * scale_factor[one];
      const ActivationWeight activation =
        std::clamp (
          (2.0 * gradient_magnitude / critical).numerical_value_in (one) - 1.0,
          0.0,
          1.0) *
        blend_weight[one];
      const ActivationWeight smooth_activation =
        (activation * activation * (3.0 * blend_weight[one] - 2.0 * activation))
          .numerical_value_in (one) *
        blend_weight[one];
      return unity +
             (maximum - unity) * smooth_activation.numerical_value_in (one);
    }

    enum class HillslopeFaceAxis { x, z };

    void
    reconstruct_centered_gradients (const TerrainDomain& domain,
                                    std::span<const WorkingElevation> levels,
                                    std::span<SurfaceGradient> gradient_x,
                                    std::span<SurfaceGradient> gradient_z) {
      const std::size_t width = domain.width ();
      const std::size_t height = domain.height ();
      const meters_f64_t run_x = 2.0 * domain.spacing_x ();
      const meters_f64_t run_z = 2.0 * domain.spacing_z ();
      parallel_for (height, 16, [&] (std::size_t first, std::size_t last) {
        for (std::size_t row = first; row < last; ++row) {
          const std::size_t prior_row = row == 0 ? height - 1 : row - 1;
          const std::size_t next_row = row + 1 == height ? 0 : row + 1;
          for (std::size_t column = 0; column < width; ++column) {
            const std::size_t prior_column =
              column == 0 ? width - 1 : column - 1;
            const std::size_t next_column =
              column + 1 == width ? 0 : column + 1;
            const std::size_t cell = row * width + column;
            gradient_x[cell] = ((levels[row * width + next_column] -
                                 levels[row * width + prior_column]) /
                                run_x)
                                 .numerical_value_in (one) *
                               grade[one];
            gradient_z[cell] = ((levels[next_row * width + column] -
                                 levels[prior_row * width + column]) /
                                run_z)
                                 .numerical_value_in (one) *
                               grade[one];
          }
        }
      });
    }

    GradientMagnitude
    reconstructed_face_gradient (std::span<const WorkingElevation> levels,
                                 std::span<const SurfaceGradient> gradient_x,
                                 std::span<const SurfaceGradient> gradient_z,
                                 std::size_t first,
                                 std::size_t second,
                                 HillslopeFaceAxis axis,
                                 meters_t run) {
      const SurfaceGradient normal =
        ((levels[second] - levels[first]) / run).numerical_value_in (one) *
        grade[one];
      if (axis == HillslopeFaceAxis::x) {
        const SurfaceGradient tangent =
          0.5 * (gradient_z[first] + gradient_z[second]);
        return cr::hypot (normal.numerical_value_in (one),
                          tangent.numerical_value_in (one)) *
               terrain_slope[one];
      }

      const SurfaceGradient tangent =
        0.5 * (gradient_x[first] + gradient_x[second]);
      return cr::hypot (tangent.numerical_value_in (one),
                        normal.numerical_value_in (one)) *
             terrain_slope[one];
    }

    int hillslope_sweep_count (
      const TerrainDomain& domain,
      julian_years_f64_t duration,
      square_meters_per_julian_year_t diffusivity,
      DiffusivityMultiplier maximum_diffusivity_multiplier) {
      if (duration == 0 || diffusivity == 0)
        return 0;
      const auto maximum_diffusivity =
        maximum_diffusivity_multiplier.numerical_value_in (one) * diffusivity;
      const auto stable_duration =
        1.0 / (2.0 * maximum_diffusivity *
               (1.0 / (domain.spacing_x () * domain.spacing_x ()) +
                1.0 / (domain.spacing_z () * domain.spacing_z ())));
      const auto requested =
        mp_units::ceil<mp_units::one> (duration / stable_duration);
      if (!mp_units::isfinite (requested) ||
          requested > std::numeric_limits<int>::max () * one)
        throw std::invalid_argument (
          "hillslope transport requests too many sweeps");
      return std::max (1,
                       static_cast<int> (requested.numerical_value_in (one)));
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
        if (!mp_units::isfinite (elevations[cell].delta_from_zero ()))
          throw std::invalid_argument ("hillslope elevations must be finite");
        if (!mp_units::isfinite (sediment[cell]) ||
            sediment[cell] < SedimentThickness::zero ())
          throw std::invalid_argument (
            "hillslope sediment must be finite and non-negative");
      }
    }

    bool finite_non_negative (SedimentVolume volume) {
      return mp_units::isfinite (volume) && !(volume < SedimentVolume::zero ());
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
        if (!finite_non_negative (capacity))
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
        const SedimentVolume cover = available_cover.empty ()
                                       ? SedimentVolume::zero ()
                                       : available_cover[cell];
        const SedimentVolume ocean_storage = ocean_mouth_capacity.empty ()
                                               ? SedimentVolume::zero ()
                                               : ocean_mouth_capacity[cell];
        if (!finite_non_negative (potential_detachment[cell]) ||
            !finite_non_negative (transport_capacity[cell]) ||
            !finite_non_negative (maximum_deposition[cell]) ||
            !finite_non_negative (cover) ||
            !finite_non_negative (ocean_storage))
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

        FractionTotal fraction_total = 0.0 * flow_fraction[one];
        for (std::uint8_t arc = 0; arc < route.arc_count; ++arc) {
          const std::size_t receiver = route.arcs[arc].receiver.value;
          const FractionTotal fraction = route.arcs[arc].fraction;
          if (receiver >= count || position[receiver] <= position[cell] ||
              !mp_units::isfinite (fraction) ||
              fraction < 0.0 * flow_fraction[one])
            throw std::invalid_argument (
              "sediment route is not a valid drainage DAG");
          fraction_total += fraction;
        }
        if (route.arc_count != 0 &&
            mp_units::abs (fraction_total - 1.0 * flow_fraction[one]) >
              1e-5 * flow_fraction[one])
          throw std::invalid_argument (
            "sediment receiver fractions must sum to one");
      }
    }
  }

  SedimentVolume
  sediment_transport_capacity (julian_years_f64_t duration,
                               square_meters_t contributing_area,
                               slope_t slope,
                               proportion_t channel_share,
                               const FluvialTransport& parameters) {
    if (!mp_units::isfinite (duration) || duration < 0 ||
        !mp_units::isfinite (contributing_area) || contributing_area < 0 ||
        !mp_units::isfinite (slope) || slope < 0 ||
        !mp_units::isfinite (channel_share) ||
        channel_share < 0.0f * proportion[one] ||
        channel_share > 1.0f * proportion[one] ||
        !mp_units::isfinite (parameters.runoff_rate) ||
        parameters.runoff_rate < 0 ||
        !mp_units::isfinite (parameters.concentration_at_unit_slope) ||
        parameters.concentration_at_unit_slope < 0)
      throw std::invalid_argument (
        "fluvial transport parameters must be finite and non-negative");

    const auto capacity =
      duration * contributing_area * parameters.runoff_rate *
      parameters.concentration_at_unit_slope * slope * channel_share;
    return sediment_volume_of (capacity);
  }

  meters_t alluvial_valley_width (square_meters_t contributing_area,
                                  const ValleyDeposition& parameters) noexcept {
    const square_meters_t area =
      std::max (square_meters_t::zero (), contributing_area);
    const meters_t minimum =
      std::max (meters_t::zero (), parameters.minimum_width);
    const meters_t maximum = std::max (minimum, parameters.maximum_width);
    const proportion_t width_ratio =
      std::max (proportion_t::zero (), parameters.width_per_sqrt_area);
    const meters_t width =
      minimum + width_ratio.numerical_value_in (one) * mp_units::sqrt (area);
    return std::clamp (width, minimum, maximum);
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

    StandingWaterStorage result {
      .body_capacity = std::vector<SedimentVolume> (census.domain ().size (),
                                                    SedimentVolume::zero ()),
      .ocean_mouth_capacity =
        std::vector<SedimentVolume> (count, SedimentVolume::zero ())
    };
    const WorkingArea cell_area = flood.domain ().cell_area ();
    for (std::size_t cell = 0; cell < count; ++cell) {
      const SedimentVolume maximum = maximum_deposition[cell];
      if (!finite_non_negative (maximum) ||
          !mp_units::isfinite (contributing_areas[cell]) ||
          contributing_areas[cell] < FractionalContributingArea::zero ())
        throw std::invalid_argument (
          "standing-water capacity readings must be finite and non-negative");
      const WaterBodyId body =
        census.body_at (CellIndex { static_cast<std::uint32_t> (cell) });
      if (body == LakeCensus::dry)
        continue;
      const meters_f64_t depth = flood.water_depths ()[cell];
      const SedimentVolume accommodation =
        sediment_volume_of (depth * cell_area);
      if (flood.ocean[cell]) {
        const meters_t width = alluvial_valley_width (
          square_meters_t (contributing_areas[cell]), parameters);
        const FootprintCells footprint_cells =
          std::max (
            1.0, (0.25 * width * width / cell_area).numerical_value_in (one)) *
          cell_count[one];
        result.ocean_mouth_capacity[cell] = std::min (
          accommodation, maximum * footprint_cells.numerical_value_in (one));
      } else {
        result.body_capacity[body.value] += std::min (accommodation, maximum);
      }
    }
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
    std::vector<SedimentVolume> incoming (count, SedimentVolume::zero ());
    SedimentRoutingResult result {
      .detached = std::vector<SedimentVolume> (count, SedimentVolume::zero ()),
      .entrained_cover =
        std::vector<SedimentVolume> (count, SedimentVolume::zero ()),
      .bedrock_detached =
        std::vector<SedimentVolume> (count, SedimentVolume::zero ()),
      .deposited = std::vector<SedimentVolume> (count, SedimentVolume::zero ()),
      .outgoing = std::vector<SedimentVolume> (count, SedimentVolume::zero ())
    };

    SedimentVolume detached_total = SedimentVolume::zero ();
    SedimentVolume entrained_cover_total = SedimentVolume::zero ();
    SedimentVolume bedrock_detached_total = SedimentVolume::zero ();
    SedimentVolume deposited_total = SedimentVolume::zero ();
    SedimentVolume exported_total = SedimentVolume::zero ();
    std::vector<SedimentVolume> remaining_body_storage (
      body_storage_capacity.begin (), body_storage_capacity.end ());

    for (const CellIndex index : flow.topological_order ()) {
      const std::size_t cell = index.value;
      const FractionalFlowRoute& route = flow.route (index);

      if (ocean[cell]) {
        const SedimentVolume deposited =
          ocean_mouth_capacity.empty ()
            ? SedimentVolume::zero ()
            : std::min (incoming[cell], ocean_mouth_capacity[cell]);
        result.deposited[cell] = deposited;
        deposited_total += deposited;
        exported_total += incoming[cell] - deposited;
        continue;
      }

      const WaterBodyId body =
        water_body.empty () ? no_water_body : water_body[cell];
      if (body != no_water_body && !route.empty ()) {
        const SedimentVolume deposited =
          std::min (incoming[cell], remaining_body_storage[body.value]);
        remaining_body_storage[body.value] -= deposited;
        const SedimentVolume outgoing = incoming[cell] - deposited;
        result.deposited[cell] = deposited;
        result.outgoing[cell] = outgoing;
        deposited_total += deposited;

        SedimentVolume posted = SedimentVolume::zero ();
        for (std::uint8_t arc = 0; arc < route.arc_count; ++arc) {
          const SedimentVolume share =
            arc + 1 == route.arc_count
              ? outgoing - posted
              : outgoing * route.arcs[arc].fraction.numerical_value_in (one);
          incoming[route.arcs[arc].receiver.value] += share;
          posted += share;
        }
        continue;
      }

      if (route.empty ()) {
        result.deposited[cell] = incoming[cell];
        deposited_total += incoming[cell];
        continue;
      }

      const SedimentVolume capacity = transport_capacity[cell];
      SedimentVolume spare_capacity =
        std::max (SedimentVolume::zero (), capacity - incoming[cell]);
      const SedimentVolume cover = available_cover.empty ()
                                     ? SedimentVolume::zero ()
                                     : available_cover[cell];
      const SedimentVolume entrained_cover = std::min (cover, spare_capacity);
      spare_capacity -= entrained_cover;
      const SedimentVolume bedrock_detachment =
        std::min (potential_detachment[cell], spare_capacity);
      const SedimentVolume local_detachment =
        entrained_cover + bedrock_detachment;
      const SedimentVolume available = incoming[cell] + local_detachment;
      const SedimentVolume deposited =
        std::min (std::max (SedimentVolume::zero (), available - capacity),
                  maximum_deposition[cell]);
      const SedimentVolume outgoing = available - deposited;

      result.detached[cell] = local_detachment;
      result.entrained_cover[cell] = entrained_cover;
      result.bedrock_detached[cell] = bedrock_detachment;
      result.deposited[cell] = deposited;
      result.outgoing[cell] = outgoing;
      detached_total += local_detachment;
      entrained_cover_total += entrained_cover;
      bedrock_detached_total += bedrock_detachment;
      deposited_total += deposited;

      SedimentVolume posted = SedimentVolume::zero ();
      for (std::uint8_t arc = 0; arc < route.arc_count; ++arc) {
        // Make the final posting the exact remainder. Independently rounding
        // every arc would slowly manufacture or destroy sediment at splits.
        const SedimentVolume share =
          arc + 1 == route.arc_count
            ? outgoing - posted
            : outgoing * route.arcs[arc].fraction.numerical_value_in (one);
        incoming[route.arcs[arc].receiver.value] += share;
        posted += share;
      }
    }

    result.detached_total = detached_total;
    result.entrained_cover_total = entrained_cover_total;
    result.bedrock_detached_total = bedrock_detached_total;
    result.deposited_total = deposited_total;
    result.exported_to_ocean = exported_total;
    result.balance_residual =
      (detached_total - deposited_total - exported_total)
        .numerical_value_in (cubic_metre) *
      cubic_metre;
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

    const WorkingArea cell_area = domain.cell_area ();
    const meters_t spacing_x = domain.spacing_x ();
    const meters_t spacing_z = domain.spacing_z ();
    const meters_t along_radius =
      0.75f * cr::hypot (meters_value (spacing_x), meters_value (spacing_z)) *
      u::m;
    const meters_t minimum_wall_relief =
      std::max (meters_t::zero (), parameters.minimum_wall_relief);
    const proportion_t wall_relief_ratio =
      std::max (proportion_t::zero (), parameters.wall_relief_per_width);

    LateralDepositionResult result { .deposited = std::vector<SedimentVolume> (
                                       count, SedimentVolume::zero ()) };
    std::vector<SedimentVolume> distributed (count, SedimentVolume::zero ());
    std::vector<WorkingElevation> working_heights (count);
    for (std::size_t cell = 0; cell < count; ++cell) {
      if (!mp_units::isfinite (elevations[cell].delta_from_zero ()) ||
          !mp_units::isfinite (contributing_areas[cell]) ||
          contributing_areas[cell] < FractionalContributingArea::zero () ||
          !mp_units::isfinite (channel_tangents[cell].magnitude ()))
        throw std::invalid_argument (
          "valley deposition readings must be finite and non-negative");
      if (!finite_non_negative (centerline_deposition[cell]))
        throw std::invalid_argument (
          "valley deposition volumes must be finite and non-negative");
      working_heights[cell] = elevations[cell];
    }

    struct Candidate {
      std::size_t cell;
      WorkingElevation height;
    };
    SedimentVolume input_total = SedimentVolume::zero ();
    SedimentVolume output_total = SedimentVolume::zero ();
    // One footprint and one list of candidates serve every source.
    std::vector<std::size_t> footprint;
    std::vector<Candidate> candidates;
    for (std::size_t source = 0; source < count; ++source) {
      const SedimentVolume source_volume = centerline_deposition[source];
      input_total += source_volume;
      if (source_volume == SedimentVolume::zero ())
        continue;
      if (ocean[source])
        throw std::invalid_argument (
          "valley deposition cannot originate in the ocean");

      plan_direction_t tangent =
        plan (channel_tangents[source].numerical_value_in (one)) *
        direction[one];
      magnitude_t tangent_length =
        hypot (tangent.numerical_value_ref_in (one)) * one;
      if (tangent_length <= 1e-6f * one) {
        const CellIndex source_cell { static_cast<std::uint32_t> (source) };
        const FractionalFlowRoute& route = flow.route (source_cell);
        if (!route.empty ()) {
          const TerrainIndex from = domain.index (source);
          const TerrainIndex to = domain.index (route.arcs[0].receiver.value);
          const meters_t step_x =
            minimum_image_delta ((static_cast<float> (to.column) -
                                  static_cast<float> (from.column)) *
                                   spacing_x,
                                 domain.period_x ());
          const meters_t step_z = minimum_image_delta (
            (static_cast<float> (to.row) - static_cast<float> (from.row)) *
              spacing_z,
            domain.period_z ());
          tangent = Vec2 (meters_value (step_x), meters_value (step_z)) *
                    direction[one];
          tangent_length = hypot (tangent.numerical_value_ref_in (one)) * one;
        }
      }
      if (tangent_length <= 1e-6f * one) {
        tangent = Vec2 (0.0f, 1.0f) * direction[one];
      } else {
        tangent /= tangent_length.numerical_value_in (one);
      }
      const magnitude_t tangent_x =
        tangent.numerical_value_ref_in (one).x () * one;
      const magnitude_t tangent_z =
        tangent.numerical_value_ref_in (one).z () * one;

      const square_meters_t area = square_meters_t (contributing_areas[source]);
      const meters_t width = alluvial_valley_width (area, parameters);
      const meters_t half_width = 0.5f * width;
      const meters_t wall_relief =
        minimum_wall_relief +
        wall_relief_ratio.numerical_value_in (one) * width;
      const meters_t search_radius = half_width + along_radius;
      const int radius_x = static_cast<int> (
        std::ceil ((search_radius / spacing_x).numerical_value_in (one)));
      const int radius_z = static_cast<int> (
        std::ceil ((search_radius / spacing_z).numerical_value_in (one)));
      const TerrainIndex center = domain.index (source);
      const WorkingElevation ceiling = working_heights[source] + wall_relief;

      footprint.clear ();
      for (int dz = -radius_z; dz <= radius_z; ++dz)
        for (int dx = -radius_x; dx <= radius_x; ++dx) {
          const meters_t offset_x = static_cast<float> (dx) * spacing_x;
          const meters_t offset_z = static_cast<float> (dz) * spacing_z;
          const meters_t along =
            mp_units::abs (offset_x * tangent_x + offset_z * tangent_z);
          const meters_t across =
            mp_units::abs (-offset_x * tangent_z + offset_z * tangent_x);
          if (along > along_radius || across > half_width)
            continue;
          const TerrainIndex site = domain.shifted (center, dx, dz);
          const std::size_t cell = domain.offset (site);
          if (!ocean[cell] && working_heights[cell] <= ceiling)
            footprint.push_back (cell);
        }
      std::ranges::sort (footprint);
      const auto unique = std::ranges::unique (footprint);
      footprint.erase (unique.begin (), unique.end ());
      if (footprint.empty ())
        footprint.push_back (source);

      candidates.clear ();
      for (const std::size_t cell : footprint)
        candidates.push_back ({ cell, working_heights[cell] });
      std::ranges::sort (
        candidates, [] (const Candidate& a, const Candidate& b) {
          return a.height != b.height ? a.height < b.height : a.cell < b.cell;
        });

      SedimentVolume remaining = source_volume;
      std::size_t active = 1;
      WorkingElevation floor_level = candidates.front ().height;
      while (active < candidates.size ()) {
        const WorkingElevation next_level = candidates[active].height;
        const SedimentVolume needed =
          sediment_volume_of ((next_level - floor_level) * cell_area *
                              static_cast<double> (active));
        if (remaining < needed)
          break;
        remaining -= needed;
        floor_level = next_level;
        ++active;
      }
      floor_level +=
        thickness_over (remaining, cell_area * static_cast<double> (active));

      SedimentVolume posted = SedimentVolume::zero ();
      for (std::size_t position = 0; position < active; ++position) {
        const std::size_t cell = candidates[position].cell;
        const SedimentVolume share =
          position + 1 == active
            ? source_volume - posted
            : sediment_volume_of ((floor_level - candidates[position].height) *
                                  cell_area);
        distributed[cell] += share;
        working_heights[cell] += thickness_over (share, cell_area);
        posted += share;
      }
      output_total += posted;
    }

    result.deposited = std::move (distributed);
    result.balance_residual =
      (input_total - output_total).numerical_value_in (cubic_metre) *
      cubic_metre;
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
    const SedimentVolume none = SedimentVolume::zero ();
    const SedimentVolume negligible = 1e-12 * sediment_volume[cubic_metre];
    std::vector<SedimentVolume> deposited (count, none);
    std::vector<SedimentVolume> remaining_capacity (count, none);
    std::vector<SedimentVolume> body_load (census.domain ().size (), none);
    std::vector<SedimentVolume> body_capacity (census.domain ().size (), none);
    std::vector<std::vector<std::size_t>> body_cells (census.domain ().size ());
    const WorkingArea cell_area = domain.cell_area ();
    const meters_t spacing_x = domain.spacing_x ();
    const meters_t spacing_z = domain.spacing_z ();
    const meters_t cell_diagonal =
      cr::hypot (meters_value (spacing_x), meters_value (spacing_z)) * u::m;
    const std::size_t no_footprint_position =
      std::numeric_limits<std::size_t>::max ();
    std::vector<std::size_t> footprint_position (count, no_footprint_position);

    SedimentVolume input = none;
    SedimentVolume dry = none;
    SedimentVolume lake = none;
    SedimentVolume ocean = none;
    SedimentVolume exported = none;
    for (std::size_t cell = 0; cell < count; ++cell) {
      const SedimentVolume load = centerline_deposition[cell];
      const SedimentVolume maximum = maximum_deposition[cell];
      if (!finite_non_negative (load) || !finite_non_negative (maximum))
        throw std::invalid_argument (
          "standing-water deposition volumes must be finite and non-negative");
      input += load;
      const WaterBodyId body =
        census.body_at (CellIndex { static_cast<std::uint32_t> (cell) });
      if (body != LakeCensus::dry) {
        const meters_f64_t depth = flood.water_depths ()[cell];
        const SedimentVolume accommodation =
          sediment_volume_of (depth * cell_area);
        remaining_capacity[cell] = std::min (accommodation, maximum);
      }
      if (body == LakeCensus::dry) {
        result.dry_centerline[cell] = centerline_deposition[cell];
        dry += load;
        continue;
      }
      if (!flood.ocean[cell]) {
        body_load[body.value] += load;
        body_capacity[body.value] += remaining_capacity[cell];
        body_cells[body.value].push_back (cell);
        continue;
      }
      if (load == none)
        continue;

      plan_direction_t tangent =
        plan (channel_tangents[cell].numerical_value_in (one)) * direction[one];
      const magnitude_t length =
        hypot (tangent.numerical_value_ref_in (one)) * one;
      tangent = length > 1e-6f * one ? tangent / length.numerical_value_in (one)
                                     : Vec2 (0.0f, 1.0f) * direction[one];
      const magnitude_t tangent_x =
        tangent.numerical_value_ref_in (one).x () * one;
      const magnitude_t tangent_z =
        tangent.numerical_value_ref_in (one).z () * one;
      const meters_t width = alluvial_valley_width (
        square_meters_t (contributing_areas[cell]), parameters);
      const meters_t search = width + cell_diagonal;
      const int radius_x = static_cast<int> (
        std::ceil ((search / spacing_x).numerical_value_in (one)));
      const int radius_z = static_cast<int> (
        std::ceil ((search / spacing_z).numerical_value_in (one)));
      const TerrainIndex center = domain.index (cell);
      struct MouthCell {
        std::size_t cell;
        SedimentVolume weight;
      };
      std::vector<MouthCell> footprint;
      for (int dz = -radius_z; dz <= radius_z; ++dz)
        for (int dx = -radius_x; dx <= radius_x; ++dx) {
          const meters_t offset_x = static_cast<float> (dx) * spacing_x;
          const meters_t offset_z = static_cast<float> (dz) * spacing_z;
          const meters_t along = offset_x * tangent_x + offset_z * tangent_z;
          if (along < -cell_diagonal || along > width)
            continue;
          const blend_weight_t progress =
            std::clamp (
              (along / std::max (width, 1e-6f * u::m)).numerical_value_in (one),
              0.0f,
              1.0f) *
            blend_weight[one];
          const meters_t half_width =
            0.5f * width *
            std::lerp (0.25f, 1.0f, progress.numerical_value_in (one));
          const meters_t across =
            mp_units::abs (-offset_x * tangent_z + offset_z * tangent_x);
          if (across > half_width)
            continue;
          const std::size_t destination =
            domain.offset (domain.shifted (center, dx, dz));
          if (!flood.ocean[destination])
            continue;
          if (remaining_capacity[destination] <= none)
            continue;
          const meters_f64_t distance =
            cr::hypot (meters_value (offset_x), meters_value (offset_z)) * u::m;
          const SedimentVolume weight =
            remaining_capacity[destination] *
            (1.0 - (0.5 * distance / search).numerical_value_in (one));
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

      SedimentVolume remaining = load;
      while (remaining > negligible && !footprint.empty ()) {
        const SedimentVolume weight_total = std::accumulate (
          footprint.begin (),
          footprint.end (),
          none,
          [] (SedimentVolume sum, const MouthCell& destination) {
            return sum + destination.weight;
          });
        if (weight_total <= none)
          break;
        const SedimentVolume pass_load = remaining;
        SedimentVolume posted = none;
        std::vector<MouthCell> still_open;
        for (const MouthCell& destination : footprint) {
          const SedimentVolume requested =
            sediment_volume_of (pass_load * destination.weight / weight_total);
          const SedimentVolume share =
            std::min (requested, remaining_capacity[destination.cell]);
          deposited[destination.cell] += share;
          remaining_capacity[destination.cell] -= share;
          posted += share;
          if (remaining_capacity[destination.cell] > negligible)
            still_open.push_back (destination);
        }
        if (posted <= negligible)
          break;
        remaining -= posted;
        footprint = std::move (still_open);
      }
      ocean += load - remaining;
      exported += remaining;
    }

    for (std::size_t body = 0; body < body_cells.size (); ++body) {
      const SedimentVolume load = body_load[body];
      if (load == none)
        continue;
      const SedimentVolume capacity = body_capacity[body];
      if (capacity <= none) {
        exported += load;
        continue;
      }
      const SedimentVolume stored = std::min (load, capacity);
      SedimentVolume posted = none;
      for (std::size_t position = 0; position < body_cells[body].size ();
           ++position) {
        const std::size_t cell = body_cells[body][position];
        const SedimentVolume local_capacity = remaining_capacity[cell];
        const SedimentVolume share =
          position + 1 == body_cells[body].size ()
            ? stored - posted
            : sediment_volume_of (stored * local_capacity / capacity);
        deposited[cell] += share;
        remaining_capacity[cell] =
          std::max (none, remaining_capacity[cell] - share);
        posted += share;
      }
      lake += posted;
      exported += load - posted;
    }

    SedimentVolume output = dry + exported;
    for (std::size_t cell = 0; cell < count; ++cell)
      output += deposited[cell];
    result.deposited = std::move (deposited);
    result.lake_storage = lake;
    result.ocean_mouth_storage = ocean;
    result.exported = exported;
    result.balance_residual =
      (input - output).numerical_value_in (cubic_metre) * cubic_metre;
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
    const WorkingArea cell_area = domain.cell_area ();
    std::vector<WorkingElevation> levels (count);
    std::vector<SedimentVolume> cover (count, SedimentVolume::zero ());
    std::vector<SurfaceGradient> gradient_x (count);
    std::vector<SurfaceGradient> gradient_z (count);
    for (std::size_t cell = 0; cell < count; ++cell) {
      levels[cell] = elevations[cell];
      cover[cell] = sediment_volume_of (sediment[cell] * cell_area);
    }
    reconstruct_centered_gradients (domain, levels, gradient_x, gradient_z);

    const DiffusivityMultiplier linear = 1.0 * scale_factor[one];
    const std::size_t width = domain.width ();
    const std::size_t height = domain.height ();
    std::vector<std::uint8_t> row_nonlinear (height, 0);
    parallel_for (height, 16, [&] (std::size_t first, std::size_t last) {
      for (std::size_t row = first; row < last; ++row) {
        const std::size_t next_row = row + 1 == height ? 0 : row + 1;
        for (std::size_t column = 0; column < width; ++column) {
          const std::size_t next_column = column + 1 == width ? 0 : column + 1;
          const std::size_t cell = row * width + column;
          if (fixed[cell])
            continue;
          const auto inspect_face = [&] (std::size_t other,
                                         HillslopeFaceAxis axis,
                                         meters_t run) {
            if (fixed[other])
              return;
            const GradientMagnitude gradient = reconstructed_face_gradient (
              levels, gradient_x, gradient_z, cell, other, axis, run);
            if (hillslope_diffusivity_multiplier (
                  gradient, critical_gradient, maximum_diffusivity_multiplier) >
                linear)
              row_nonlinear[row] = 1;
          };
          inspect_face (row * width + next_column,
                        HillslopeFaceAxis::x,
                        domain.spacing_x ());
          inspect_face (next_row * width + column,
                        HillslopeFaceAxis::z,
                        domain.spacing_z ());
        }
      }
    });
    const bool nonlinear_flux_active = std::ranges::any_of (
      row_nonlinear, [] (std::uint8_t row) { return row != 0; });
    const DiffusivityMultiplier stability_multiplier =
      nonlinear_flux_active
        ? static_cast<double> (
            maximum_diffusivity_multiplier.numerical_value_in (one)) *
            scale_factor[one]
        : linear;
    const int sweep_count = hillslope_sweep_count (
      domain, duration, diffusivity, stability_multiplier);
    result.sweeps = iteration_count (sweep_count);
    if (sweep_count == 0)
      return result;

    const julian_years_f64_t sweep_duration = duration / sweep_count;
    // Each face's flux, positive from a cell to its neighbour in +x or +z,
    // then each cell's four faces gathered in one order: every pass writes
    // only its own cells or faces, and the totals add by row, then rows in
    // order, so the sweep computes the same bits on any number of threads.
    const SedimentVolume none = SedimentVolume::zero ();
    std::vector<SedimentVolume> flux_x (count, none);
    std::vector<SedimentVolume> flux_z (count, none);
    std::vector<SedimentVolume> row_bedrock (height, none);
    std::vector<SedimentVolume> row_residual (height, none);
    std::vector<SedimentVolume> row_transferred (height, none);
    const auto face_flux = [&] (std::size_t first,
                                std::size_t second,
                                HillslopeFaceAxis axis,
                                meters_t face_width,
                                meters_t run) -> SedimentVolume {
      if (fixed[first] || fixed[second])
        return none;
      const WorkingElevationChange difference = levels[first] - levels[second];
      if (difference == WorkingElevationChange::zero ())
        return none;
      const GradientMagnitude gradient = reconstructed_face_gradient (
        levels, gradient_x, gradient_z, first, second, axis, run);
      const DiffusivityMultiplier local_multiplier =
        hillslope_diffusivity_multiplier (
          gradient, critical_gradient, maximum_diffusivity_multiplier);
      const WorkingArea conductance =
        (local_multiplier.numerical_value_in (one) * diffusivity *
         sweep_duration * face_width / run)
          .numerical_value_in (u::m * u::m) *
        (u::m * u::m);
      return sediment_volume_of (difference * conductance);
    };

    SedimentVolume transferred = none;
    SedimentVolume bedrock_detached = none;
    SedimentVolume residual = none;
    for (int sweep = 0; sweep < sweep_count; ++sweep) {
      reconstruct_centered_gradients (domain, levels, gradient_x, gradient_z);
      parallel_for (height, 16, [&] (std::size_t first, std::size_t last) {
        for (std::size_t row = first; row < last; ++row) {
          const std::size_t next_row = row + 1 == height ? 0 : row + 1;
          for (std::size_t column = 0; column < width; ++column) {
            const std::size_t next_column =
              column + 1 == width ? 0 : column + 1;
            const std::size_t cell = row * width + column;
            flux_x[cell] = face_flux (cell,
                                      row * width + next_column,
                                      HillslopeFaceAxis::x,
                                      domain.spacing_z (),
                                      domain.spacing_x ());
            flux_z[cell] = face_flux (cell,
                                      next_row * width + column,
                                      HillslopeFaceAxis::z,
                                      domain.spacing_x (),
                                      domain.spacing_z ());
          }
        }
      });

      parallel_for (height, 16, [&] (std::size_t first, std::size_t last) {
        for (std::size_t row = first; row < last; ++row) {
          const std::size_t prior_row = row == 0 ? height - 1 : row - 1;
          SedimentVolume bedrock = none;
          SedimentVolume row_net = none;
          SedimentVolume moved = none;
          for (std::size_t column = 0; column < width; ++column) {
            const std::size_t prior_column =
              column == 0 ? width - 1 : column - 1;
            const std::size_t cell = row * width + column;
            // Positive flux on a face leaves its lower-indexed cell: the
            // left and upper faces bring it in, this cell's own take it out.
            SedimentVolume net = none;
            SedimentVolume incoming = none;
            SedimentVolume outgoing = none;
            for (const SedimentVolume into :
                 { flux_x[row * width + prior_column],
                   flux_z[prior_row * width + column],
                   -flux_x[cell],
                   -flux_z[cell] }) {
              net += into;
              if (into > none)
                incoming += into;
              else
                outgoing -= into;
            }
            const SedimentVolume cover_removed =
              std::min (cover[cell], outgoing);
            bedrock += outgoing - cover_removed;
            cover[cell] =
              std::max (none, cover[cell] - cover_removed + incoming);
            levels[cell] += thickness_over (net, cell_area);
            result.eroded_thickness[cell] +=
              static_cast<float> (
                (outgoing / cell_area).numerical_value_in (u::m)) *
              sediment_thickness[u::m];
            result.deposited_thickness[cell] +=
              static_cast<float> (
                (incoming / cell_area).numerical_value_in (u::m)) *
              sediment_thickness[u::m];
            row_net += net;
            moved += outgoing;
          }
          row_bedrock[row] = bedrock;
          row_residual[row] = row_net;
          row_transferred[row] = moved;
        }
      });
      for (std::size_t row = 0; row < height; ++row) {
        bedrock_detached += row_bedrock[row];
        residual += row_residual[row];
        transferred += row_transferred[row];
      }
    }

    for (std::size_t cell = 0; cell < count; ++cell) {
      result.heights[cell] = mp_units::value_cast<float> (levels[cell]);
      result.sediment_thickness[cell] =
        static_cast<float> (
          (cover[cell] / cell_area).numerical_value_in (u::m)) *
        sediment_thickness[u::m];
    }
    result.transferred = transferred;
    result.bedrock_detached = bedrock_detached;
    result.balance_residual =
      residual.numerical_value_in (cubic_metre) * cubic_metre;
    return result;
  }
}
