#ifndef MOPPE_TERRAIN_STREAM_POWER_EVOLUTION_HH
#define MOPPE_TERRAIN_STREAM_POWER_EVOLUTION_HH

#include <moppe/terrain/fractional_drainage.hh>
#include <moppe/terrain/terrain_view.hh>
#include <moppe/terrain/types.hh>

#include <functional>
#include <span>
#include <vector>

namespace moppe::terrain {
  enum class StreamPowerRouting { D8, DInfinity };

  // Backward-Euler landscape evolution for the n=1 stream-power equation.
  // Incision velocity is calibrated at a reference drainage area, keeping
  // every parameter dimensionally stable while the area exponent remains an
  // inspectable runtime value.
  struct StreamPowerEvolution {
    julian_years_t duration = 1000000.0f * mp_units::astronomy::Julian_year;
    julian_years_t time_step = 50000.0f * mp_units::astronomy::Julian_year;
    meters_per_julian_year_t reference_incision_rate =
      2e-5f * mp_units::si::metre / mp_units::astronomy::Julian_year;
    square_meters_t reference_area =
      1.0f * mp_units::si::metre * mp_units::si::metre;
    float area_exponent = 0.4f;
    square_meters_per_julian_year_t diffusivity =
      0.0f * mp_units::si::metre * mp_units::si::metre /
      mp_units::astronomy::Julian_year;
    float sea_level = 50.0f / 650.0f;
    StreamPowerRouting routing = StreamPowerRouting::DInfinity;
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
    IterationCount diffusion_sweeps = iteration_count (0);
    cubic_meters_f64_t tectonic_uplift_volume =
      0.0 * mp_units::si::metre * mp_units::si::metre * mp_units::si::metre;
    cubic_meters_f64_t incised_volume =
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
    // Unique samples: periodic duplicated render seams are omitted.
    std::vector<float> heights;
    std::vector<ChannelTangent> channel_tangents;
    StreamPowerEvolutionReport report;
  };

  // Called after each geological step. The height span contains the current
  // unique samples (periodic render seams omitted) and remains valid only for
  // the duration of the callback.
  using StreamPowerProgress =
    std::function<void (int, int, std::span<const float>)>;

  StreamPowerEvolutionResult evolve_stream_power (
    const TerrainView& terrain,
    std::span<const meters_per_julian_year_t> uplift_rate,
    const StreamPowerEvolution& parameters,
    const StreamPowerProgress& progress = {},
    std::span<const ChannelTangent> initial_channel_tangents = {});
}

#endif
