#ifndef MOPPE_TERRAIN_STREAM_POWER_EVOLUTION_HH
#define MOPPE_TERRAIN_STREAM_POWER_EVOLUTION_HH

#include <moppe/terrain/domain.hh>
#include <moppe/terrain/fractional_drainage.hh>
#include <moppe/terrain/sediment_transport.hh>

#include <functional>
#include <span>
#include <vector>

namespace moppe::terrain {
  // Backward-Euler landscape evolution for the n=1 stream-power equation.
  // Incision velocity is calibrated at a reference drainage area, keeping
  // every parameter dimensionally stable while the area exponent remains an
  // inspectable runtime value.
  struct StreamPowerEvolution {
    julian_years_t duration = 1000000.0f * mp_units::astronomy::Julian_year;
    julian_years_t time_step = 50000.0f * mp_units::astronomy::Julian_year;
    // Tectonic forcing may end before geomorphic relaxation does. The uplift
    // field is active from the start of evolution through this duration; a
    // geological step crossing that boundary integrates only its overlap.
    julian_years_t uplift_duration =
      1000000.0f * mp_units::astronomy::Julian_year;
    meters_per_julian_year_t reference_incision_rate =
      2e-5f * mp_units::si::metre / mp_units::astronomy::Julian_year;
    // Overloaded material continues downstream after this local aggradation
    // rate is reached. A long geological step may not pile a whole catchment
    // into one routing cell.
    meters_per_julian_year_t maximum_deposition_rate =
      1e-5f * mp_units::si::metre / mp_units::astronomy::Julian_year;
    square_meters_t reference_area =
      1.0f * mp_units::si::metre * mp_units::si::metre;
    float area_exponent = 0.4f;
    // Where the channel network begins. Above this catchment running water
    // cuts; below it the ground is a hillslope and belongs to creep.
    //
    // Off by default, and the reason is a judgement rather than a measurement.
    // Raising it does what the geomorphology says it should: hillslopes appear
    // between the channels, the mean slope of the world falls from 36 to 28
    // degrees, and the spectral spike at the rill wavelength halves. The world
    // that comes out is also blobby and dull. The fine dendritic rilling this
    // suppresses is most of what makes the terrain beautiful to ride through,
    // and no statistic said so -- see docs/hillslopes-and-channels.md.
    square_meters_t channel_initiation_area =
      1.0f * mp_units::si::metre * mp_units::si::metre;
    FluvialTransport fluvial_transport;
    square_meters_per_julian_year_t diffusivity =
      0.0f * mp_units::si::metre * mp_units::si::metre /
      mp_units::astronomy::Julian_year;
    // Metres in the terrain elevation frame.
    float sea_level = 50.0f;
    // How strongly the prior geological step's channel tangent favours an
    // aligned, still-downhill D-infinity route. Zero recovers memoryless
    // routing; values must remain below one.
    ChannelPersistence channel_persistence =
      0.35f * moppe::terrain::channel_persistence[mp_units::one];
  };

  struct StreamPowerEvolutionReport {
    CellCount cells = cell_count (0);
    CellCount fixed_boundaries = cell_count (0);
    IterationCount steps = iteration_count (0);
    IterationCount hillslope_sweeps = iteration_count (0);
    cubic_meters_f64_t tectonic_uplift_volume =
      0.0 * mp_units::si::metre * mp_units::si::metre * mp_units::si::metre;
    cubic_meters_f64_t eroded_volume =
      0.0 * mp_units::si::metre * mp_units::si::metre * mp_units::si::metre;
    cubic_meters_f64_t deposited_volume =
      0.0 * mp_units::si::metre * mp_units::si::metre * mp_units::si::metre;
    cubic_meters_f64_t exported_sediment_volume =
      0.0 * mp_units::si::metre * mp_units::si::metre * mp_units::si::metre;
    cubic_meters_f64_t fluvial_entrained_cover_volume =
      0.0 * mp_units::si::metre * mp_units::si::metre * mp_units::si::metre;
    cubic_meters_f64_t fluvial_bedrock_detached_volume =
      0.0 * mp_units::si::metre * mp_units::si::metre * mp_units::si::metre;
    cubic_meters_f64_t sediment_balance_residual =
      0.0 * mp_units::si::metre * mp_units::si::metre * mp_units::si::metre;
    cubic_meters_f64_t hillslope_transferred_volume =
      0.0 * mp_units::si::metre * mp_units::si::metre * mp_units::si::metre;
    cubic_meters_f64_t hillslope_bedrock_detached_volume =
      0.0 * mp_units::si::metre * mp_units::si::metre * mp_units::si::metre;
    cubic_meters_f64_t lowered_volume =
      0.0 * mp_units::si::metre * mp_units::si::metre * mp_units::si::metre;
    cubic_meters_f64_t raised_volume =
      0.0 * mp_units::si::metre * mp_units::si::metre * mp_units::si::metre;
    meters_f64_t mean_absolute_change = 0.0 * mp_units::si::metre;
    meters_f64_t maximum_absolute_change = 0.0 * mp_units::si::metre;
    meters_f64_t final_step_mean_change = 0.0 * mp_units::si::metre;
    meters_f64_t final_step_maximum_change = 0.0 * mp_units::si::metre;
  };

  struct StreamPowerEvolutionResult {
    // One value per lattice sample.
    std::vector<SurfaceElevation> heights;
    std::vector<SedimentThickness> sediment_thickness;
    std::vector<SedimentThickness> eroded_thickness;
    std::vector<SedimentThickness> deposited_thickness;
    std::vector<ChannelTangent> channel_tangents;
    StreamPowerEvolutionReport report;
  };

  // Called after each geological step. The elevation span contains the
  // current lattice samples and remains valid only for the duration of the
  // callback.
  using StreamPowerProgress = std::function<void (
    IterationCount, IterationCount, std::span<const SurfaceElevation>)>;

  namespace detail {
    StreamPowerEvolutionResult evolve_stream_power (
      const TerrainDomain& domain,
      std::span<const SurfaceElevation> elevations,
      std::span<const meters_per_julian_year_t> uplift_rate,
      const StreamPowerEvolution& parameters,
      const StreamPowerProgress& progress,
      std::span<const ChannelTangent> initial_channel_tangents,
      std::span<const SedimentThickness> initial_sediment);
  }

  template <TerrainElevations Terrain>
  StreamPowerEvolutionResult evolve_stream_power (
    const Terrain& terrain,
    std::span<const meters_per_julian_year_t> uplift_rate,
    const StreamPowerEvolution& parameters,
    const StreamPowerProgress& progress = {},
    std::span<const ChannelTangent> initial_channel_tangents = {},
    std::span<const SedimentThickness> initial_sediment = {}) {
    return detail::evolve_stream_power (terrain.domain (),
                                        elevations (terrain),
                                        uplift_rate,
                                        parameters,
                                        progress,
                                        initial_channel_tangents,
                                        initial_sediment);
  }
}

#endif
