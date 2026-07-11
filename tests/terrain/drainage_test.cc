#include <moppe/terrain/drainage.hh>
#include <moppe/terrain/flood.hh>

#include <tests/test.hh>

#include <array>

using namespace moppe::terrain;

MOPPE_TEST (d8_drainage_routes_to_the_steepest_lower_neighbor) {
  const std::array heights {
    3.0f, 2.0f, 3.0f,
    2.0f, 0.0f, 2.0f,
    3.0f, 2.0f, 3.0f
  };
  const TerrainView terrain
    ({ .width = 3, .height = 3, .spacing_x = 2.0f,
       .spacing_y = 2.0f, .height_scale = 10.0f }, heights);
  const DrainageGraph graph = analyze_drainage (terrain);

  MOPPE_CHECK (graph.sinks.size () == 1);
  MOPPE_CHECK (graph.sinks[0] == 4);
  for (std::uint32_t cell = 0; cell < 9; ++cell)
    MOPPE_CHECK (graph.receiver[cell] == 4);
  MOPPE_CHECK_NEAR (graph.contributing_area.at (1, 1), 36.0f, 0.0f);
  MOPPE_CHECK_NEAR (graph.slope.at (0, 0),
		    30.0f / std::sqrt (8.0f), 1e-6f);
}

MOPPE_TEST (periodic_drainage_crosses_the_duplicated_seam) {
  // The last row and column duplicate the first. Cell (0,0) reaches the low
  // point at (2,0) by crossing the periodic seam.
  const std::array heights {
    2.0f, 3.0f, 0.0f, 2.0f,
    4.0f, 4.0f, 4.0f, 4.0f,
    4.0f, 4.0f, 4.0f, 4.0f,
    2.0f, 3.0f, 0.0f, 2.0f
  };
  const TerrainView terrain
    ({ .width = 4, .height = 4, .topology = Topology::Torus }, heights);
  const DrainageGraph graph = analyze_drainage (terrain);

  MOPPE_CHECK (graph.width () == 3);
  MOPPE_CHECK (graph.height () == 3);
  MOPPE_CHECK (graph.source_grid.topology == Topology::Torus);
  MOPPE_CHECK (graph.receiver[0] == 2);
  MOPPE_CHECK (graph.basin[0] == 2);
}

MOPPE_TEST (wet_drainage_carries_a_catchment_across_a_lake) {
  const std::array heights {
    0.f, 0.f, 0.f, 0.f, 0.f,
    0.f, 4.f, 2.f, 3.f, 0.f,
    0.f, 5.f, 1.f, 4.f, 0.f,
    0.f, 6.f, 5.f, 5.f, 0.f,
    0.f, 6.f, 6.f, 6.f, 0.f
  };
  const TerrainView terrain ({ .width = 5, .height = 5 }, heights);
  const FloodField flood = analyze_standing_water (terrain, 0.0f);
  const LakeCensus census = census_lakes (flood);
  const DrainageGraph dry = analyze_drainage (terrain);
  const DrainageGraph wet = analyze_wet_drainage (terrain, flood, census);

  MOPPE_CHECK (dry.receiver[12] == 12);
  MOPPE_CHECK (wet.receiver[12] != 12);
  MOPPE_CHECK (wet.sinks.size () == flood.outlets.size ());

  const WaterBody& lake = census.bodies[0];
  std::size_t exits = 0;
  for (std::uint32_t member = 0; member < census.body.size (); ++member)
    if (census.body[member] == lake.id
	&& census.body[wet.receiver[member]] != lake.id)
      ++exits;
  MOPPE_CHECK (exits == 1);
  MOPPE_CHECK (wet.receiver[lake.outlet_cell] == lake.spill_cell);

  const WaterNetwork network = analyze_water_network (flood, census, wet);
  const WaterBodyFlow& flow = network.bodies[lake.id];
  MOPPE_CHECK (!flow.inlets.empty ());
  MOPPE_CHECK (flow.outlet_cell == lake.outlet_cell);
  MOPPE_CHECK (flow.spill_cell == lake.spill_cell);
  MOPPE_CHECK (flow.downstream_cell == wet.receiver[lake.spill_cell]);
  MOPPE_CHECK (flow.outflow_area_m2 > flow.inflow_area_m2);

  const RiverNetwork rivers =
    extract_river_network (flood, census, wet, 1.0f);
  bool enters_lake = false;
  bool leaves_lake = false;
  for (const RiverReach& reach : rivers.reaches) {
    enters_lake |= reach.downstream_body == lake.id;
    leaves_lake |= reach.upstream_body == lake.id;
    for (const std::uint32_t member : reach.cells) {
      MOPPE_CHECK (census.body[member] == LakeCensus::dry);
      MOPPE_CHECK (!flood.ocean[member]);
    }
  }
  MOPPE_CHECK (enters_lake);
  MOPPE_CHECK (leaves_lake);
  MOPPE_CHECK (rivers.reach_by_cell[lake.outlet_cell]
	       == RiverReach::no_id);

  std::uint32_t cell = 12;
  std::size_t steps = 0;
  while (wet.receiver[cell] != cell && steps < heights.size ()) {
    cell = wet.receiver[cell];
    ++steps;
  }
  MOPPE_CHECK (steps < heights.size ());
  MOPPE_CHECK_NEAR (wet.contributing_area.values ()[cell], 25.0f, 0.0f);
}

MOPPE_TEST (wet_drainage_preserves_steepest_descent_on_dry_ground) {
  const std::array heights {
    5.f, 4.f, 3.f,
    4.f, 2.f, 1.f,
    3.f, 1.f, 0.f
  };
  const TerrainView terrain ({ .width = 3, .height = 3 }, heights);
  const FloodField flood = analyze_standing_water (terrain, 0.0f);
  const LakeCensus census = census_lakes (flood);
  const DrainageGraph dry = analyze_drainage (terrain);
  const DrainageGraph wet = analyze_wet_drainage (terrain, flood, census);

  MOPPE_CHECK (wet.receiver == dry.receiver);
  MOPPE_CHECK (wet.sinks == dry.sinks);
  for (std::size_t cell = 0; cell < heights.size (); ++cell)
    MOPPE_CHECK_NEAR (wet.contributing_area.values ()[cell],
      dry.contributing_area.values ()[cell], 0.0f);
}

MOPPE_TEST (wet_drainage_and_body_flow_are_deterministic) {
  const std::array heights {
    0.f, 4.f, 3.f, 0.f,
    2.f, 1.f, 5.f, 2.f,
    3.f, 2.f, 4.f, 3.f,
    0.f, 4.f, 3.f, 0.f
  };
  const TerrainView terrain
    ({ .width = 4, .height = 4, .topology = Topology::Torus }, heights);
  const FloodField flood_a = analyze_standing_water (terrain, 0.0f);
  const FloodField flood_b = analyze_standing_water (terrain, 0.0f);
  const LakeCensus census_a = census_lakes (flood_a);
  const LakeCensus census_b = census_lakes (flood_b);
  const DrainageGraph graph_a =
    analyze_wet_drainage (terrain, flood_a, census_a);
  const DrainageGraph graph_b =
    analyze_wet_drainage (terrain, flood_b, census_b);
  const WaterNetwork network_a =
    analyze_water_network (flood_a, census_a, graph_a);
  const WaterNetwork network_b =
    analyze_water_network (flood_b, census_b, graph_b);
  const RiverNetwork rivers_a =
    extract_river_network (flood_a, census_a, graph_a, 1.0f);
  const RiverNetwork rivers_b =
    extract_river_network (flood_b, census_b, graph_b, 1.0f);

  MOPPE_CHECK (flood_a.spill_receiver == flood_b.spill_receiver);
  MOPPE_CHECK (graph_a.receiver == graph_b.receiver);
  MOPPE_CHECK (graph_a.basin == graph_b.basin);
  MOPPE_CHECK (graph_a.sinks == graph_b.sinks);
  MOPPE_CHECK (network_a.bodies.size () == network_b.bodies.size ());
  MOPPE_CHECK (rivers_a.reach_by_cell == rivers_b.reach_by_cell);
  MOPPE_CHECK (rivers_a.reaches.size () == rivers_b.reaches.size ());
  for (std::size_t i = 0; i < graph_a.contributing_area.values ().size ();
       ++i)
    MOPPE_CHECK_NEAR (graph_a.contributing_area.values ()[i],
		      graph_b.contributing_area.values ()[i], 0.0f);
  for (std::size_t i = 0; i < network_a.bodies.size (); ++i) {
    const WaterBodyFlow& a = network_a.bodies[i];
    const WaterBodyFlow& b = network_b.bodies[i];
    MOPPE_CHECK (a.inlets.size () == b.inlets.size ());
    MOPPE_CHECK (a.outlet_cell == b.outlet_cell);
    MOPPE_CHECK (a.spill_cell == b.spill_cell);
    MOPPE_CHECK (a.downstream_cell == b.downstream_cell);
    MOPPE_CHECK_NEAR (a.inflow_area_m2, b.inflow_area_m2, 0.0f);
    MOPPE_CHECK_NEAR (a.outflow_area_m2, b.outflow_area_m2, 0.0f);
  }
  for (std::size_t i = 0; i < rivers_a.reaches.size (); ++i) {
    const RiverReach& a = rivers_a.reaches[i];
    const RiverReach& b = rivers_b.reaches[i];
    MOPPE_CHECK (a.cells == b.cells);
    MOPPE_CHECK (a.upstream_body == b.upstream_body);
    MOPPE_CHECK (a.downstream_body == b.downstream_body);
    MOPPE_CHECK (a.downstream_ocean == b.downstream_ocean);
    MOPPE_CHECK (a.downstream_reach == b.downstream_reach);
    MOPPE_CHECK_NEAR (a.downstream_area_m2, b.downstream_area_m2, 0.0f);
    MOPPE_CHECK_NEAR (a.maximum_slope, b.maximum_slope, 0.0f);
  }
}
