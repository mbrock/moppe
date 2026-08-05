#ifndef MOPPE_TERRAIN_SEDIMENT_TRANSPORT_HH
#define MOPPE_TERRAIN_SEDIMENT_TRANSPORT_HH

#include <moppe/terrain/flood.hh>
#include <moppe/terrain/fractional_drainage.hh>

#include <cstdint>
#include <span>
#include <vector>

namespace moppe::terrain {
  using SedimentVolume = mp_units::quantity<
    sediment_volume[mp_units::si::metre * mp_units::si::metre *
                    mp_units::si::metre],
    double>;
  using SedimentThickness =
    mp_units::quantity<sediment_thickness[mp_units::si::metre], float>;
  using SedimentConcentration =
    mp_units::quantity<sediment_concentration[mp_units::one], float>;

  struct FluvialTransport {
    // Contributing area times runoff is the water discharge represented by
    // one terrain cell. Concentration scales that discharge into solid volume
    // at unit slope; actual capacity also responds linearly to local slope.
    meters_per_julian_year_t runoff_rate =
      1.0f * mp_units::si::metre / mp_units::astronomy::Julian_year;
    SedimentConcentration concentration_at_unit_slope =
      2e-5f * sediment_concentration[mp_units::one];
  };

  struct ValleyDeposition {
    meters_t minimum_width = 6.0f * mp_units::si::metre;
    meters_t maximum_width = 160.0f * mp_units::si::metre;
    proportion_t width_per_sqrt_area = 0.04f * proportion[mp_units::one];
    meters_t minimum_wall_relief = 1.0f * mp_units::si::metre;
    proportion_t wall_relief_per_width = 0.08f * proportion[mp_units::one];
  };

  meters_t
  alluvial_valley_width (square_meters_t contributing_area,
                         const ValleyDeposition& parameters = {}) noexcept;

  SedimentVolume
  sediment_transport_capacity (julian_years_f64_t duration,
                               square_meters_t contributing_area,
                               slope_t slope,
                               float channel_share,
                               const FluvialTransport& parameters);

  struct SedimentRoutingResult {
    // Per-cell solid volumes for this geological step.
    std::vector<SedimentVolume> detached;
    std::vector<SedimentVolume> entrained_cover;
    std::vector<SedimentVolume> bedrock_detached;
    std::vector<SedimentVolume> deposited;
    std::vector<SedimentVolume> outgoing;

    SedimentVolume detached_total = SedimentVolume::zero ();
    SedimentVolume entrained_cover_total = SedimentVolume::zero ();
    SedimentVolume bedrock_detached_total = SedimentVolume::zero ();
    SedimentVolume deposited_total = SedimentVolume::zero ();
    SedimentVolume exported_to_ocean = SedimentVolume::zero ();
    cubic_meters_f64_t balance_residual =
      0.0 * mp_units::si::metre * mp_units::si::metre * mp_units::si::metre;
  };

  struct StandingWaterStorage {
    std::vector<SedimentVolume> body_capacity;
    std::vector<SedimentVolume> ocean_mouth_capacity;
  };

  StandingWaterStorage standing_water_storage_capacity (
    const FloodField& flood,
    const LakeCensus& census,
    std::span<const FractionalContributingArea> contributing_areas,
    std::span<const SedimentVolume> maximum_deposition,
    const ValleyDeposition& parameters = {});

  // Routes solid material through an already-solved fractional drainage DAG.
  // Potential detachment and transport capacity are volumes for this one
  // geological step. A cell deposits incoming material above its capacity up
  // to its aggradation limit, then carries the remainder onward. It uses any
  // spare capacity to detach its own surface. Optional standing-water storage
  // lets one lake share a finite accommodation budget and lets an ocean mouth
  // retain a bounded part of its incoming load; all remaining ocean flux is
  // exported. Non-ocean sinks retain incoming material locally. Stored mobile
  // cover consumes spare capacity before the router may detach bedrock.
  SedimentRoutingResult
  route_sediment (const FractionalFlowDomain& flow,
                  std::span<const SedimentVolume> potential_detachment,
                  std::span<const SedimentVolume> transport_capacity,
                  std::span<const SedimentVolume> maximum_deposition,
                  std::span<const std::uint8_t> ocean,
                  std::span<const SedimentVolume> available_cover = {},
                  std::span<const WaterBodyId> water_body = {},
                  std::span<const SedimentVolume> body_storage_capacity = {},
                  std::span<const SedimentVolume> ocean_mouth_capacity = {});

  struct LateralDepositionResult {
    std::vector<SedimentVolume> deposited;
    cubic_meters_f64_t balance_residual =
      0.0 * mp_units::si::metre * mp_units::si::metre * mp_units::si::metre;
  };

  // Redistributes each routed centerline deposit across the local valley
  // cross-section. The source volume remains exact; only its destination
  // cells change. Low cells within the physical footprint are raised toward
  // one common floor before higher cells receive material.
  LateralDepositionResult spread_valley_deposition (
    const FractionalFlowDomain& flow,
    std::span<const SurfaceElevation> elevations,
    std::span<const FractionalContributingArea> contributing_areas,
    std::span<const ChannelTangent> channel_tangents,
    std::span<const SedimentVolume> centerline_deposition,
    std::span<const std::uint8_t> ocean,
    const ValleyDeposition& parameters = {});

  struct StandingWaterDepositionResult {
    std::vector<SedimentVolume> dry_centerline;
    std::vector<SedimentVolume> deposited;
    SedimentVolume lake_storage = SedimentVolume::zero ();
    SedimentVolume ocean_mouth_storage = SedimentVolume::zero ();
    SedimentVolume exported = SedimentVolume::zero ();
    cubic_meters_f64_t balance_residual =
      0.0 * mp_units::si::metre * mp_units::si::metre * mp_units::si::metre;
  };

  StandingWaterDepositionResult spread_standing_water_deposition (
    const FloodField& flood,
    const LakeCensus& census,
    const FractionalFlowDomain& flow,
    std::span<const FractionalContributingArea> contributing_areas,
    std::span<const ChannelTangent> channel_tangents,
    std::span<const SedimentVolume> centerline_deposition,
    std::span<const SedimentVolume> maximum_deposition,
    const ValleyDeposition& parameters = {});

  struct HillslopeTransportResult {
    std::vector<SurfaceElevation> heights;
    std::vector<SedimentThickness> sediment_thickness;
    std::vector<SedimentThickness> eroded_thickness;
    std::vector<SedimentThickness> deposited_thickness;
    IterationCount sweeps = iteration_count (0);
    SedimentVolume transferred = SedimentVolume::zero ();
    SedimentVolume bedrock_detached = SedimentVolume::zero ();
    cubic_meters_f64_t balance_residual =
      0.0 * mp_units::si::metre * mp_units::si::metre * mp_units::si::metre;
  };

  // Reconstructs a two-dimensional surface gradient at each cardinal cell
  // face, then moves solid down the face-normal component. The nonlinear
  // diffusivity responds to the full gradient magnitude rather than one
  // grid-axis component. Every face posts one equal-and-opposite volume pair.
  // Faces touching a fixed cell are no-flux boundaries. Stable internal
  // sweeps make the result independent of a geological step being longer than
  // the explicit diffusion limit. Existing mobile cover leaves a source
  // before any bedrock is detached there.
  HillslopeTransportResult route_hillslope_sediment (
    const TerrainDomain& domain,
    std::span<const SurfaceElevation> elevations,
    std::span<const SedimentThickness> sediment,
    std::span<const std::uint8_t> fixed,
    julian_years_f64_t duration,
    square_meters_per_julian_year_t diffusivity,
    proportion_t critical_gradient = 1.0f * proportion[mp_units::one],
    proportion_t maximum_diffusivity_multiplier = 1.0f *
                                                  proportion[mp_units::one]);
}

#endif
