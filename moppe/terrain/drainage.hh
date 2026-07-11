#ifndef MOPPE_TERRAIN_DRAINAGE_HH
#define MOPPE_TERRAIN_DRAINAGE_HH

#include <moppe/terrain/evaluator.hh>
#include <moppe/terrain/terrain_view.hh>

#include <cstdint>
#include <limits>
#include <vector>

namespace moppe::terrain {
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
    std::vector<std::uint32_t> receiver;
    ScalarRaster slope;
    ScalarRaster contributing_area;
    std::vector<std::uint32_t> basin;
    std::vector<std::uint32_t> sinks;

    std::size_t width () const noexcept {
      return source_grid.unique_width ();
    }

    std::size_t height () const noexcept {
      return source_grid.unique_height ();
    }
  };

  struct WaterInlet {
    std::uint32_t upstream_cell;
    std::uint32_t water_cell;
    float contributing_area_m2;
  };

  struct WaterBodyFlow {
    static constexpr std::uint32_t no_cell =
      std::numeric_limits<std::uint32_t>::max ();

    std::uint32_t body_id;
    std::vector<WaterInlet> inlets;
    float inflow_area_m2;
    std::uint32_t outlet_cell;
    std::uint32_t spill_cell;
    std::uint32_t downstream_cell;
    float outflow_area_m2;
  };

  struct WaterNetwork {
    std::vector<WaterBodyFlow> bodies;
  };

  struct RiverReach {
    static constexpr std::uint32_t no_id =
      std::numeric_limits<std::uint32_t>::max ();

    std::uint32_t id;
    std::vector<std::uint32_t> cells;
    std::uint32_t upstream_body;
    std::uint32_t downstream_body;
    bool downstream_ocean;
    std::uint32_t downstream_reach;
    float upstream_area_m2;
    float downstream_area_m2;
    float maximum_slope;
  };

  struct WaterfallParameters {
    float minimum_drop_m = 3.0f;
    float minimum_slope = 0.6f;
    std::size_t separation_cells = 1;
  };

  struct Waterfall {
    static constexpr std::uint32_t no_id = RiverReach::no_id;

    std::uint32_t id;
    std::uint32_t reach_id;
    std::uint32_t lip_cell;
    std::uint32_t foot_cell;
    float drop_m;
    float horizontal_distance_m;
    float slope;
    float contributing_area_m2;
  };

  struct RiverNetwork {
    float minimum_area_m2;
    WaterfallParameters waterfall_parameters;
    std::vector<std::uint32_t> reach_by_cell;
    std::vector<std::uint32_t> waterfall_by_cell;
    std::vector<RiverReach> reaches;
    std::vector<Waterfall> waterfalls;
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
                         float minimum_area_m2,
                         const WaterfallParameters& waterfall_parameters = {});
}

#endif
