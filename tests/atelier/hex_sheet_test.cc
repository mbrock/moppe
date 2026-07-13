#include <atelier/hex_sheet.hh>

#include <tests/test.hh>

#include <algorithm>
#include <array>
#include <ranges>

using namespace atelier;
using namespace mp_units::si::unit_symbols;

MOPPE_TEST (periodic_hex_sheet_has_reciprocal_six_neighbour_topology) {
  const HexSheet sheet;
  MOPPE_CHECK (sheet.topology_is_valid ());
  MOPPE_CHECK (sheet.displacements ().size () == hex_sheet_tile_count);
  MOPPE_CHECK (sheet.velocities ().size () == hex_sheet_tile_count);
  MOPPE_CHECK (sheet.drives ().size () == hex_sheet_tile_count);

  const HexSheetTopology& topology = sheet.topology ();
  for (TileId id = 0; id < hex_sheet_tile_count; ++id) {
    auto neighbours = topology.neighbours (id);
    std::ranges::sort (neighbours);
    MOPPE_CHECK (std::ranges::adjacent_find (neighbours) == neighbours.end ());
    for (TileId neighbour : neighbours) {
      const auto reverse = topology.neighbours (neighbour);
      MOPPE_CHECK (std::ranges::find (reverse, id) != reverse.end ());
    }
  }
}

MOPPE_TEST (open_hex_sheet_ends_at_four_fixed_corners) {
  HexSheet sheet (SheetBoundary::open);
  const HexSheetTopology& topology = sheet.topology ();
  const std::array corners {
    topology.tile_id ({ 0, 0 }),
    topology.tile_id ({ hex_sheet_columns - 1, 0 }),
    topology.tile_id ({ 0, hex_sheet_rows - 1 }),
    topology.tile_id ({ hex_sheet_columns - 1, hex_sheet_rows - 1 }),
  };

  MOPPE_CHECK (topology.neighbours (corners.front ()).size () == 2);
  const auto anchor_count = std::ranges::count_if (
    std::views::iota (TileId { 0 }, TileId { hex_sheet_tile_count }),
    [&] (TileId id) { return topology.is_anchor (id); });
  MOPPE_CHECK (anchor_count == 4);
  sheet.advance (2.35f * s);
  for (TileId corner : corners) {
    MOPPE_CHECK (topology.is_anchor (corner));
    MOPPE_CHECK_NEAR (
      sheet.displacements ()[corner].numerical_value_in (m), 0.0f, 0.0001f);
    MOPPE_CHECK_NEAR (
      sheet.velocities ()[corner].numerical_value_in (m / s), 0.0f, 0.0001f);
  }
}

MOPPE_TEST (local_noise_materializes_as_a_tile_acceleration_section) {
  const HexSheet sheet;
  const auto [minimum, maximum] = std::ranges::minmax_element (sheet.drives ());
  MOPPE_CHECK (*maximum - *minimum > 0.1f * m / (s * s));
}

MOPPE_TEST (noise_drive_deforms_the_sheet_before_refinement) {
  HexSheet sheet;
  sheet.advance (1.0f * s);
  const auto [minimum, maximum] =
    std::ranges::minmax_element (sheet.displacements ());
  MOPPE_CHECK (*maximum - *minimum > 0.01f * m);
}

MOPPE_TEST (hex_sheet_refines_and_melds_one_partition_cell) {
  HexSheet sheet;
  sheet.advance (1.0f * s);
  MOPPE_CHECK (!sheet.is_refined ());
  MOPPE_CHECK (sheet.leaves ().size () == hex_sheet_tile_count);

  sheet.advance (4.0f * s);
  MOPPE_CHECK (sheet.is_refined ());
  MOPPE_CHECK_NEAR (sheet.refinement (), 1.0f, 0.001f);
  MOPPE_CHECK (sheet.leaves ().size () == hex_sheet_tile_count + 6);

  sheet.advance (10.5f * s);
  MOPPE_CHECK (!sheet.is_refined ());
  MOPPE_CHECK (sheet.leaves ().size () == hex_sheet_tile_count);
}

MOPPE_TEST (refinement_impulse_propagates_through_the_tile_graph) {
  HexSheet sheet;
  sheet.advance (2.5f * s);

  const bool any_displacement = std::ranges::any_of (
    sheet.displacements (), [] (NormalDisplacement displacement) {
      return std::abs (displacement.numerical_value_in (m)) > 0.001f;
    });
  MOPPE_CHECK (any_displacement);
}
