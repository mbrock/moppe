#ifndef MOPPE_TERRAIN_STREAM_POWER_EVOLUTION_HH
#define MOPPE_TERRAIN_STREAM_POWER_EVOLUTION_HH

#include <moppe/terrain/terrain_view.hh>
#include <moppe/terrain/types.hh>

#include <span>
#include <vector>

namespace moppe::terrain {
  // Backward-Euler landscape evolution for the n=1 stream-power equation.
  // Erodibility follows the model-year convention documented in units.md:
  // yr^-1 m^(1-2m) when contributing area is measured in square metres.
  struct StreamPowerEvolution {
    julian_years_t duration = 1000000.0f * mp_units::astronomy::Julian_year;
    julian_years_t time_step = 50000.0f * mp_units::astronomy::Julian_year;
    float erodibility = 2e-5f;
    float area_exponent = 0.4f;
    float sea_level = 50.0f / 650.0f;
  };

  struct StreamPowerEvolutionReport {
    CellCount cells = cell_count (0);
    CellCount fixed_boundaries = cell_count (0);
    IterationCount steps = iteration_count (0);
    cubic_meters_f64_t lowered_volume =
      0.0 * mp_units::si::metre * mp_units::si::metre * mp_units::si::metre;
    cubic_meters_f64_t raised_volume =
      0.0 * mp_units::si::metre * mp_units::si::metre * mp_units::si::metre;
    meters_f64_t mean_absolute_change = 0.0 * mp_units::si::metre;
    meters_f64_t maximum_absolute_change = 0.0 * mp_units::si::metre;
    meters_f64_t final_step_maximum_change = 0.0 * mp_units::si::metre;
  };

  struct StreamPowerEvolutionResult {
    // Unique samples: periodic duplicated render seams are omitted.
    std::vector<float> heights;
    StreamPowerEvolutionReport report;
  };

  StreamPowerEvolutionResult
  evolve_stream_power (const TerrainView& terrain,
                       std::span<const meters_per_julian_year_t> uplift_rate,
                       const StreamPowerEvolution& parameters);
}

#endif
