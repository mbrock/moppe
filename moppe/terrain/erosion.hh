#ifndef MOPPE_TERRAIN_EROSION_HH
#define MOPPE_TERRAIN_EROSION_HH

#include <moppe/terrain/types.hh>

#include <cstddef>
#include <cstdint>

namespace moppe::terrain {
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

}

#endif
