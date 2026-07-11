#include <moppe/terrain/drainage.hh>

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
