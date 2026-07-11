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
    0.f, 0.f, 0.f, 0.f, 0.f
  };
  const TerrainView terrain ({ .width = 5, .height = 5 }, heights);
  const FloodField flood = analyze_standing_water (terrain, 0.0f);
  const DrainageGraph dry = analyze_drainage (terrain);
  const DrainageGraph wet = analyze_wet_drainage (terrain, flood);

  MOPPE_CHECK (dry.receiver[12] == 12);
  MOPPE_CHECK (wet.receiver[12] != 12);
  MOPPE_CHECK (wet.sinks.size () == flood.outlets.size ());

  std::uint32_t cell = 12;
  std::size_t steps = 0;
  while (wet.receiver[cell] != cell && steps < heights.size ()) {
    cell = wet.receiver[cell];
    ++steps;
  }
  MOPPE_CHECK (steps < heights.size ());
  // The lake cell, its upstream bank, and the reached sea cell share one
  // accumulated route. Other submerged boundary cells remain explicit sea
  // outlets in the current flood contract.
  MOPPE_CHECK_NEAR (wet.contributing_area.values ()[cell], 3.0f, 0.0f);
}

MOPPE_TEST (wet_drainage_preserves_steepest_descent_on_dry_ground) {
  const std::array heights {
    5.f, 4.f, 3.f,
    4.f, 2.f, 1.f,
    3.f, 1.f, 0.f
  };
  const TerrainView terrain ({ .width = 3, .height = 3 }, heights);
  const FloodField flood = analyze_standing_water (terrain, 0.0f);
  const DrainageGraph dry = analyze_drainage (terrain);
  const DrainageGraph wet = analyze_wet_drainage (terrain, flood);

  MOPPE_CHECK (wet.receiver == dry.receiver);
  MOPPE_CHECK (wet.sinks == dry.sinks);
  for (std::size_t cell = 0; cell < heights.size (); ++cell)
    MOPPE_CHECK_NEAR (wet.contributing_area.values ()[cell],
		      dry.contributing_area.values ()[cell], 0.0f);
}
