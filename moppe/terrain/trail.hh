#ifndef MOPPE_TERRAIN_TRAIL_HH
#define MOPPE_TERRAIN_TRAIL_HH

#include <moppe/terrain/drainage.hh>
#include <moppe/terrain/terrain_view.hh>
#include <moppe/terrain/types.hh>

#include <cstddef>
#include <vector>

namespace moppe::terrain {
  // The Motorcycle Association's brief for one deliberately made circuit.
  // Site selection and routing express intent; cut and fill then reconcile
  // that intent with the terrain rather than letting drainage draw the route.
  struct TrailFormation {
    float sea_level = 50.0f / 650.0f;
    square_meters_t minimum_catchment_area =
      5000.0f * mp_units::si::metre * mp_units::si::metre;
    square_meters_t maximum_catchment_area =
      100000.0f * mp_units::si::metre * mp_units::si::metre;
    meters_t minimum_height_above_sea = 1.5f * mp_units::si::metre;
    meters_t width = 3.0f * mp_units::si::metre;
    meters_t shoulder_blend = 4.0f * mp_units::si::metre;
    meters_t maximum_cut = 2.5f * mp_units::si::metre;
    meters_t maximum_fill = 1.5f * mp_units::si::metre;
    // Grade is longitudinal rise over distance along the route: following a
    // contour is zero grade even on a steep sidehill. The formation pass
    // separately benches the path cross-section toward its centerline height.
    // Designed grade is the ordinary leisure-path target. Maximum grade is a
    // local exception after the available cut and fill have been accounted
    // for, not the grade that the route search should routinely accept.
    slope_t maximum_grade = 0.12f * terrain_slope[mp_units::one];
    slope_t designed_grade = 0.05f * terrain_slope[mp_units::one];
    IterationCount grading_iterations = iteration_count (24);
    meters_t home_base_water_distance = 90.0f * mp_units::si::metre;
    meters_t home_base_pad_radius = 18.0f * mp_units::si::metre;
    meters_t desired_circuit_radius = 900.0f * mp_units::si::metre;
    // The first built circuit should visit foothills and viewpoints without
    // becoming an alpine summit route. The quadratic preference begins at the
    // first height and becomes strong at the second; neither is a hard wall.
    // Height is measured from sea so the policy survives datum changes.
    meters_t highland_preference_height_above_sea =
      180.0f * mp_units::si::metre;
    meters_t alpine_avoidance_height_above_sea =
      285.0f * mp_units::si::metre;
  };

  struct TrailFormationReport {
    CellCount centerline_cells = cell_count (0);
    CellCount shaped_cells = cell_count (0);
    std::size_t connected_components = 0;
    meters_f64_t circuit_length = 0.0 * mp_units::si::metre;
    cubic_meters_f64_t cut_volume =
      0.0 * mp_units::si::metre * mp_units::si::metre * mp_units::si::metre;
    cubic_meters_f64_t fill_volume =
      0.0 * mp_units::si::metre * mp_units::si::metre * mp_units::si::metre;
    double mean_centerline_grade = 0.0;
    double maximum_centerline_grade = 0.0;
    std::size_t grade_exceptions = 0;
    meters_f64_t maximum_centerline_height_above_sea =
      0.0 * mp_units::si::metre;
    meters_f64_t maximum_centerline_step = 0.0 * mp_units::si::metre;
    meters_f64_t mean_absolute_change = 0.0 * mp_units::si::metre;
    meters_f64_t maximum_absolute_change = 0.0 * mp_units::si::metre;
  };

  struct TrailComponent {
    TrailComponentId id;
    CellIndex anchor_cell;
    CellCount cells = cell_count (0);
  };

  struct TrailPlan {
    CellIndex home_base = no_cell;
    CellIndex scenic_focus = no_cell;
    std::vector<CellIndex> control_sites;
    std::vector<CellIndex> circuit;
  };

  // The deliberately built, continuous plan-view route. A* supplies the
  // discrete corridor above; this alignment is the geometric authority for
  // grading, materials, and traversal-facing presentation. Points are stored
  // in an unwrapped chart so a toroidal seam cannot introduce a false corner;
  // the closing segment from the last point to the first is implicit.
  struct TrailAlignmentPoint {
    float x_m = 0.0f;
    float z_m = 0.0f;

    friend bool operator== (const TrailAlignmentPoint&,
                            const TrailAlignmentPoint&) = default;
  };

  struct TrailAlignment {
    std::vector<TrailAlignmentPoint> points;
    meters_f64_t length = 0.0 * mp_units::si::metre;

    friend bool operator== (const TrailAlignment&,
                            const TrailAlignment&) = default;
  };

  // The plan's one directed cycle, plus its two physical surface readings.
  // The graph is connected by construction: following receiver once per
  // circuit cell returns to home base.
  struct TrailNetwork {
    TerrainGrid source_grid;
    TrailPlan plan;
    std::vector<CellIndex> receiver;
    std::vector<TrailComponentId> component_by_cell;
    std::vector<CellIndex> cells;
    std::vector<TrailComponent> components;
    TrailAlignment alignment;
    meters_t formed_width = 0.0f * mp_units::si::metre;
    // Physical metres relative to the terrain entering TrailFormation.
    // The renderer and physics still consume the composed heightmap, while
    // this retained construction layer remains inspectable and reversible.
    std::vector<float> earthwork_delta_m;
    ScalarRaster influence;
    ScalarRaster home_base_influence;

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
  std::vector<float> expand_home_base_influence (const TrailNetwork& network);
  meters_f64_t trail_circuit_length (const TrailNetwork& network);

  TrailFormationResult form_trails (const TerrainView& terrain,
                                    const TrailFormation& parameters = {});
}

#endif
