#ifndef MOPPE_TERRAIN_TRAIL_HH
#define MOPPE_TERRAIN_TRAIL_HH

#include <moppe/terrain/drainage.hh>
#include <moppe/terrain/terrain_view.hh>
#include <moppe/terrain/types.hh>

#include <cstddef>
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
    std::size_t connected_components = 0;
    cubic_meters_f64_t cut_volume =
      0.0 * mp_units::si::metre * mp_units::si::metre * mp_units::si::metre;
    cubic_meters_f64_t fill_volume =
      0.0 * mp_units::si::metre * mp_units::si::metre * mp_units::si::metre;
    meters_f64_t mean_absolute_change = 0.0 * mp_units::si::metre;
    meters_f64_t maximum_absolute_change = 0.0 * mp_units::si::metre;
  };

  struct TrailComponent {
    TrailComponentId id;
    CellIndex terminal_cell;
    CellCount cells = cell_count (0);
  };

  // A directed forest over terrain cells. Every centerline cell has at most
  // one downstream receiver; cells sharing a terminal belong to the same
  // undirected connected component. The influence raster is the shoulder-
  // blended material footprint over the same unique terrain domain.
  struct TrailNetwork {
    TerrainGrid source_grid;
    std::vector<CellIndex> receiver;
    std::vector<TrailComponentId> component_by_cell;
    std::vector<CellIndex> cells;
    std::vector<TrailComponent> components;
    ScalarRaster influence;

    bool contains (CellIndex cell) const noexcept {
      return cell.value < component_by_cell.size () &&
             component_by_cell[cell.value] != no_trail_component;
    }
  };

  struct TrailFormationResult {
    std::vector<float> heights;
    TrailNetwork network;
    TrailFormationReport report;
  };

  TrailNetwork analyze_trail_network (const TerrainView& terrain,
                                      const TrailFormation& parameters,
                                      const DrainageGraph& drainage,
                                      const FloodField& flood);
  TrailNetwork analyze_trail_network (const TerrainView& terrain,
                                      const TrailFormation& parameters = {});

  // Expand unique torus samples to the duplicated render seam carried by the
  // source grid. Bounded grids are returned unchanged.
  std::vector<float> expand_trail_influence (const TrailNetwork& network);

  TrailFormationResult form_trails (const TerrainView& terrain,
                                    const TrailFormation& parameters = {});
}

#endif
