#ifndef MOPPE_TERRAIN_TRAIL_HH
#define MOPPE_TERRAIN_TRAIL_HH

#include <moppe/terrain/drainage.hh>
#include <moppe/terrain/terrain_view.hh>
#include <moppe/terrain/types.hh>

#include <vector>

namespace moppe::terrain {
  // A rider-scale network selected from the small catchments that converge
  // into valley floors. The centerline is graded with bounded cut and fill,
  // then blended back into the surrounding terrain across soft shoulders.
  struct TrailFormation {
    float sea_level = 50.0f / 650.0f;
    square_meters_t minimum_catchment_area =
      5000.0f * mp_units::si::metre * mp_units::si::metre;
    square_meters_t maximum_catchment_area =
      100000.0f * mp_units::si::metre * mp_units::si::metre;
    meters_t minimum_height_above_sea = 1.5f * mp_units::si::metre;
    meters_t width = 3.0f * mp_units::si::metre;
    meters_t shoulder_blend = 4.0f * mp_units::si::metre;
    meters_t maximum_cut = 1.25f * mp_units::si::metre;
    meters_t maximum_fill = 0.75f * mp_units::si::metre;
    slope_t maximum_grade = 0.24f * terrain_slope[mp_units::one];
    IterationCount grading_iterations = iteration_count (16);
  };

  struct TrailFormationReport {
    CellCount centerline_cells = cell_count (0);
    CellCount shaped_cells = cell_count (0);
    cubic_meters_f64_t cut_volume =
      0.0 * mp_units::si::metre * mp_units::si::metre * mp_units::si::metre;
    cubic_meters_f64_t fill_volume =
      0.0 * mp_units::si::metre * mp_units::si::metre * mp_units::si::metre;
    meters_f64_t mean_absolute_change = 0.0 * mp_units::si::metre;
    meters_f64_t maximum_absolute_change = 0.0 * mp_units::si::metre;
  };

  struct TrailFormationResult {
    std::vector<float> heights;
    TrailFormationReport report;
  };

  TrailFormationResult form_trails (const TerrainView& terrain,
                                    const TrailFormation& parameters = {});
}

#endif
