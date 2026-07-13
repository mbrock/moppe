#include <atelier/landscape.hh>

#include <tests/test.hh>

#include <algorithm>
#include <array>

using namespace atelier;
using namespace mp_units::si::unit_symbols;

MOPPE_TEST (toroidal_landscape_has_reciprocal_six_neighbour_topology) {
  const Landscape landscape;
  MOPPE_CHECK (landscape.topology_is_valid ());
  MOPPE_CHECK (landscape.tiles ().size () == landscape_tile_count);

  for (TileId id = 0; id < landscape.tiles ().size (); ++id) {
    auto neighbours = landscape.tiles ()[id].neighbours;
    std::ranges::sort (neighbours);
    MOPPE_CHECK (std::ranges::adjacent_find (neighbours) == neighbours.end ());
    for (TileId neighbour : neighbours) {
      const auto& reverse = landscape.tiles ()[neighbour].neighbours;
      MOPPE_CHECK (std::ranges::find (reverse, id) != reverse.end ());
    }
  }
}

MOPPE_TEST (landscape_refines_and_melds_one_partition_cell) {
  Landscape landscape;
  landscape.advance (1.0f * s);
  MOPPE_CHECK (!landscape.is_refined ());
  MOPPE_CHECK (landscape.leaves ().size () == landscape_tile_count);

  landscape.advance (4.0f * s);
  MOPPE_CHECK (landscape.is_refined ());
  MOPPE_CHECK_NEAR (landscape.refinement (), 1.0f, 0.001f);
  MOPPE_CHECK (landscape.leaves ().size () == landscape_tile_count + 6);

  landscape.advance (10.5f * s);
  MOPPE_CHECK (!landscape.is_refined ());
  MOPPE_CHECK (landscape.leaves ().size () == landscape_tile_count);
}

MOPPE_TEST (refinement_impulse_propagates_through_the_tile_graph) {
  Landscape landscape;
  landscape.advance (2.5f * s);

  const bool any_displacement =
    std::ranges::any_of (landscape.tiles (), [] (const LandscapeTile& tile) {
      return std::abs (tile.displacement.numerical_value_in (m)) > 0.001f;
    });
  MOPPE_CHECK (any_displacement);
}
