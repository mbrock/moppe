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
    std::vector<std::uint32_t> receiver;
    SlopeRaster slope;
    ContributingAreaRaster contributing_area;
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
    square_meters_t contributing_area;
  };

  struct WaterBodyFlow {
    static constexpr std::uint32_t no_cell =
      std::numeric_limits<std::uint32_t>::max ();

    std::uint32_t body_id;
    std::vector<WaterInlet> inlets;
    square_meters_t inflow_area;
    std::uint32_t outlet_cell;
    std::uint32_t spill_cell;
    std::uint32_t downstream_cell;
    square_meters_t outflow_area;
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
    square_meters_t upstream_area;
    square_meters_t downstream_area;
    slope_t maximum_slope;
  };

  struct WaterfallParameters {
    meters_t minimum_drop = 3.0f * mp_units::si::metre;
    slope_t minimum_slope = 0.6f * terrain_slope[mp_units::one];
    std::size_t separation_cells = 1;
  };

  struct Waterfall {
    static constexpr std::uint32_t no_id = RiverReach::no_id;

    std::uint32_t id;
    std::uint32_t reach_id;
    std::uint32_t lip_cell;
    std::uint32_t foot_cell;
    meters_t drop;
    meters_t horizontal_distance;
    slope_t slope;
    square_meters_t contributing_area;
  };

  struct RiverNetwork {
    square_meters_t minimum_area;
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
                         square_meters_t minimum_area,
                         const WaterfallParameters& waterfall_parameters = {});
}

#endif
