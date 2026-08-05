#ifndef MOPPE_TERRAIN_SEDIMENT_TRANSPORT_HH
#define MOPPE_TERRAIN_SEDIMENT_TRANSPORT_HH

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

  // Routes solid material through an already-solved fractional drainage DAG.
  // Potential detachment and transport capacity are volumes for this one
  // geological step. A cell deposits incoming material above its capacity up
  // to its aggradation limit, then carries the remainder onward. It uses any
  // spare capacity to detach its own surface. Ocean cells export incoming
  // flux; non-ocean sinks retain it locally. Stored mobile cover consumes
  // spare capacity before the router may detach underlying bedrock.
  SedimentRoutingResult
  route_sediment (const FractionalFlowDomain& flow,
                  std::span<const SedimentVolume> potential_detachment,
                  std::span<const SedimentVolume> transport_capacity,
                  std::span<const SedimentVolume> maximum_deposition,
                  std::span<const std::uint8_t> ocean,
                  std::span<const SedimentVolume> available_cover = {});

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

  // Moves solid material down local surface gradients across cardinal cell
  // faces. Every face posts one equal-and-opposite volume pair. Faces touching
  // a fixed cell are no-flux boundaries. Stable internal sweeps make the
  // result independent of a geological step being longer than the explicit
  // diffusion limit. Existing mobile cover leaves a source before any
  // bedrock is detached there.
  HillslopeTransportResult
  route_hillslope_sediment (const TerrainDomain& domain,
                            std::span<const SurfaceElevation> elevations,
                            std::span<const SedimentThickness> sediment,
                            std::span<const std::uint8_t> fixed,
                            julian_years_f64_t duration,
                            square_meters_per_julian_year_t diffusivity);
}

#endif
