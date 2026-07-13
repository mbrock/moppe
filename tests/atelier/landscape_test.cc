#include <atelier/landscape.hh>

#include <tests/test.hh>

#include <algorithm>
#include <array>

using namespace atelier;
using namespace mp_units::si::unit_symbols;

MOPPE_TEST (toroidal_landscape_has_reciprocal_six_neighbour_topology) {
  const Landscape landscape;
  MOPPE_CHECK (landscape.topology_is_valid ());
  MOPPE_CHECK (landscape.displacements ().size () == landscape_tile_count);
  MOPPE_CHECK (landscape.velocities ().size () == landscape_tile_count);
  MOPPE_CHECK (landscape.drives ().size () == landscape_tile_count);

  const PeriodicHexTopology& topology = landscape.topology ();
  for (TileId id = 0; id < landscape_tile_count; ++id) {
    auto neighbours = topology.neighbours (id);
    std::ranges::sort (neighbours);
    MOPPE_CHECK (std::ranges::adjacent_find (neighbours) == neighbours.end ());
    for (TileId neighbour : neighbours) {
      const auto reverse = topology.neighbours (neighbour);
      MOPPE_CHECK (std::ranges::find (reverse, id) != reverse.end ());
    }
  }
}

MOPPE_TEST (moppe_noise_field_materializes_as_a_tile_acceleration_section) {
  const Landscape landscape;
  const auto [minimum, maximum] =
    std::ranges::minmax_element (landscape.drives ());
  MOPPE_CHECK (*maximum - *minimum > 0.1f * m / (s * s));
}

MOPPE_TEST (noise_drive_deforms_the_sheet_before_refinement) {
  Landscape landscape;
  landscape.advance (1.0f * s);
  const auto [minimum, maximum] =
    std::ranges::minmax_element (landscape.displacements ());
  MOPPE_CHECK (*maximum - *minimum > 0.01f * m);
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

  const bool any_displacement = std::ranges::any_of (
    landscape.displacements (), [] (NormalDisplacement displacement) {
      return std::abs (displacement.numerical_value_in (m)) > 0.001f;
    });
  MOPPE_CHECK (any_displacement);
}
