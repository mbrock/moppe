#ifndef MOPPE_TERRAIN_DRAINAGE_HH
#define MOPPE_TERRAIN_DRAINAGE_HH

#include <moppe/terrain/evaluator.hh>
#include <moppe/terrain/terrain_view.hh>

#include <cstdint>
#include <limits>
#include <vector>

namespace moppe::terrain {
  QUANTITY_SPEC (terrain_slope,
                 mp_units::dimensionless,
                 mp_units::non_negative);

  using SlopeRaster = Raster<terrain_slope[mp_units::one]>;
  using slope_t = mp_units::quantity<terrain_slope[mp_units::one], float>;
  using ContributingAreaRaster =
    Raster<mp_units::isq::area[mp_units::si::metre * mp_units::si::metre]>;

  struct FloodField;
  struct LakeCensus;

  enum class DrainageRouting { D8 };

  struct DrainageParameters {
    DrainageRouting routing = DrainageRouting::D8;
  };

  // A compact functional graph. Dry drainage uses strictly lower receivers;
  // wet drainage may also follow acyclic equal-surface routes through water.
  // Rasters contain unique torus samples without the duplicated render seam.
  struct DrainageGraph {
    TerrainGrid source_grid;
    std::vector<CellIndex> receiver;
    SlopeRaster slope;
    ContributingAreaRaster contributing_area;
    std::vector<CellIndex> basin;
    std::vector<CellIndex> sinks;

    std::size_t width () const noexcept {
      return source_grid.unique_width ();
    }

    std::size_t height () const noexcept {
      return source_grid.unique_height ();
    }
  };

  struct WaterInlet {
    CellIndex upstream_cell;
    CellIndex water_cell;
    square_meters_t contributing_area;
  };

  struct WaterBodyFlow {
    static constexpr CellIndex no_cell = terrain::no_cell;

    WaterBodyId body_id;
    std::vector<WaterInlet> inlets;
    square_meters_t inflow_area;
    CellIndex outlet_cell;
    CellIndex spill_cell;
    CellIndex downstream_cell;
    square_meters_t outflow_area;
  };

  struct WaterNetwork {
    std::vector<WaterBodyFlow> bodies;
  };

  // A dense, unwrapped plan-view trajectory derived from a D8 reach. The
  // drainage cells remain the topological authority; this continuous reading
  // is shared by rendering, flow painting, and future traversal. Distance is
  // local to the reach, while flow_distance_m is globally continuous through
  // confluences and reaches zero at the final mouth.
  struct RiverAlignmentPoint {
    float x_m = 0.0f;
    float z_m = 0.0f;
    float distance_m = 0.0f;
    float flow_distance_m = 0.0f;
    float contributing_area_m2 = 0.0f;
    float slope = 0.0f;
    float waterfall = 0.0f;
    float standing_water = 0.0f;
    float water_level_m = 0.0f;
    // Flooded water the ribbon itself owns: channel-like bodies and small
    // depressions the route crosses. The ribbon holds water_level_m across
    // such stretches and widens to the local waterline, where standing_water
    // instead retires the ribbon under a sheet-rendered body.
    float pooled = 0.0f;
  };

  struct RiverAlignment {
    std::vector<RiverAlignmentPoint> points;
    meters_f64_t length = 0.0 * mp_units::si::metre;
  };

  struct RiverReach {
    static constexpr RiverReachId no_id = no_river_reach;

    RiverReachId id;
    std::vector<CellIndex> cells;
    WaterBodyId upstream_body;
    WaterBodyId downstream_body;
    bool downstream_ocean;
    RiverReachId downstream_reach;
    square_meters_t upstream_area;
    square_meters_t downstream_area;
    slope_t maximum_slope;
    RiverAlignment alignment;
  };

  struct WaterfallParameters {
    meters_t minimum_drop = 3.0f * mp_units::si::metre;
    slope_t minimum_slope = 0.6f * terrain_slope[mp_units::one];
    SeparationCellCount separation_cells = separation_cell_count (1);
  };

  struct Waterfall {
    static constexpr WaterfallId no_id = no_waterfall;

    WaterfallId id;
    RiverReachId reach_id;
    CellIndex lip_cell;
    CellIndex foot_cell;
    meters_t drop;
    meters_t horizontal_distance;
    slope_t slope;
    square_meters_t contributing_area;
  };

  struct RiverNetwork {
    square_meters_t minimum_area;
    WaterfallParameters waterfall_parameters;
    std::vector<RiverReachId> reach_by_cell;
    std::vector<WaterfallId> waterfall_by_cell;
    std::vector<RiverReach> reaches;
    std::vector<Waterfall> waterfalls;
    // One flag per census body: a river alignment passes through it from
    // inlet to outlet. Traversed channel-like bodies render as ribbon pools
    // and must therefore stay out of the standing-water sheet.
    std::vector<std::uint8_t> body_traversed;
  };

  DrainageGraph analyze_drainage (const TerrainView& terrain,
                                  const DrainageParameters& parameters = {});
  DrainageGraph
  analyze_wet_drainage (const TerrainView& terrain,
                        const FloodField& flood,
                        const LakeCensus& census,
                        const DrainageParameters& parameters = {});
  WaterNetwork analyze_water_network (const FloodField& flood,
                                      const LakeCensus& census,
                                      const DrainageGraph& drainage);
  RiverNetwork
  extract_river_network (const FloodField& flood,
                         const LakeCensus& census,
                         const DrainageGraph& drainage,
                         square_meters_t minimum_area,
                         const WaterfallParameters& waterfall_parameters = {});
}

#endif
