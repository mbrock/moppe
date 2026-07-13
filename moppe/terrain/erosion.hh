#ifndef MOPPE_TERRAIN_EROSION_HH
#define MOPPE_TERRAIN_EROSION_HH

#include <moppe/terrain/types.hh>

#include <cstddef>
#include <cstdint>

namespace moppe::terrain {
  enum class SedimentDisposition { Discard, Deposit };

  enum class CarvingRule { Unconstrained, PathMonotone };

  // Operational reading produced by one hydraulic erosion stage. Amounts are
  // measured in normalized terrain-height units; the balance is independent
  // of the physical cell area shared by every term.
  struct HydraulicErosionReport {
    EventCount droplets = event_count (0);
    EventCount steps = event_count (0);
    EventCount stopped_flat = event_count (0);
    EventCount stopped_at_boundary = event_count (0);
    EventCount stopped_at_step_limit = event_count (0);
    EventCount stopped_at_water_cutoff = event_count (0);
    double eroded = 0.0;
    double deposited = 0.0;
    double discarded_sediment = 0.0;
    double final_water = 0.0;

    double mean_steps () const noexcept {
      return count_value (droplets)
               ? static_cast<double> (count_value (steps)) /
                   count_value (droplets)
               : 0.0;
    }

    double mean_final_water () const noexcept {
      return count_value (droplets) ? final_water / count_value (droplets)
                                    : 0.0;
    }

    double discarded_fraction () const noexcept {
      return eroded > 0.0 ? discarded_sediment / eroded : 0.0;
    }
  };

  // Finite-time n=1 stream-power evolution.  SI units keep age, uplift,
  // erodibility, drainage area, and terrain scale comparable across grid
  // resolutions.  sea_level is expressed in the source heightfield's units.
  struct AnalyticalErosion {
    julian_years_t duration = 100000.0f * mp_units::astronomy::Julian_year;
    meters_per_julian_year_t uplift_rate =
      0.0f * mp_units::si::metre / mp_units::astronomy::Julian_year;
    float erodibility = 2e-5f;
    float area_exponent = 0.4f;
    float sea_level = 50.0f / 650.0f;
    IterationCount fixed_point_iterations = iteration_count (1);
    float relaxation = 1.0f;
  };

  struct AnalyticalErosionReport {
    CellCount cells = cell_count (0);
    CellCount fixed_boundaries = cell_count (0);
    IterationCount fixed_point_iterations = iteration_count (0);
    cubic_meters_f64_t lowered_volume =
      0.0 * mp_units::si::metre * mp_units::si::metre * mp_units::si::metre;
    cubic_meters_f64_t raised_volume =
      0.0 * mp_units::si::metre * mp_units::si::metre * mp_units::si::metre;
    meters_f64_t mean_absolute_change = 0.0 * mp_units::si::metre;
    meters_f64_t maximum_absolute_change = 0.0 * mp_units::si::metre;
  };

  // Linear hillslope diffusion (soil creep): dz/dt = D * laplacian(z),
  // explicit 5-point Jacobi with an internally derived stable timestep
  // dt <= 1 / (2 D (1/hx^2 + 1/hz^2)).  Pairs advective incision in
  // channels with diffusive rounding on hillslopes: smooth convex
  // crests against sharp valley cuts.
  struct HillslopeDiffusion {
    julian_years_t duration = 2000.0f * mp_units::astronomy::Julian_year;
    square_meters_per_julian_year_t diffusivity =
      0.01f * mp_units::si::metre * mp_units::si::metre /
      mp_units::astronomy::Julian_year;
  };

  struct HillslopeDiffusionReport {
    CellCount cells = cell_count (0);
    IterationCount sweeps = iteration_count (0);
    cubic_meters_f64_t lowered_volume =
      0.0 * mp_units::si::metre * mp_units::si::metre * mp_units::si::metre;
    cubic_meters_f64_t raised_volume =
      0.0 * mp_units::si::metre * mp_units::si::metre * mp_units::si::metre;
    meters_f64_t mean_absolute_change = 0.0 * mp_units::si::metre;
    meters_f64_t maximum_absolute_change = 0.0 * mp_units::si::metre;
  };

  // Deterministic channel stamping along the extracted river network.
  // Bed depth follows catchment area (hydraulic-geometry style) and beds
  // are monotone downstream, so every carved channel drains to its mouth.
  // Depths and widths are in meters; area threshold is in terrain cells so
  // carved channels match the renderer's visible-river extraction.
  struct ChannelCarving {
    float sea_level = 50.0f / 650.0f;
    float minimum_area_cells = 1024.0f;
    float depth_per_sqrt_m2 = 0.0015f;
    meters_t minimum_depth = 0.4f * mp_units::si::metre;
    meters_t maximum_depth = 2.5f * mp_units::si::metre;
    meters_t bank_blend = 6.0f * mp_units::si::metre;
  };

  struct ChannelCarvingReport {
    ReachCount reaches = reach_count (0);
    CellCount carved_cells = cell_count (0);
    cubic_meters_f64_t lowered_volume =
      0.0 * mp_units::si::metre * mp_units::si::metre * mp_units::si::metre;
    meters_f64_t mean_lowering = 0.0 * mp_units::si::metre;
    meters_f64_t maximum_lowering = 0.0 * mp_units::si::metre;
  };
}

#endif
