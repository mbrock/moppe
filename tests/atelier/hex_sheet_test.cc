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
  MOPPE_CHECK (sheet.displacements ().size () == hex_sheet_base_cell_count);
  MOPPE_CHECK (sheet.velocities ().size () == hex_sheet_base_cell_count);
  MOPPE_CHECK (sheet.drives ().size () == hex_sheet_base_cell_count);

  const HexSheetTopology& topology = sheet.topology ();
  for (TileId id = 0; id < topology.size (); ++id) {
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
  MOPPE_CHECK (sheet.topology ()
                 .neighbours (sheet.topology ().tile_id ({ 0, 0 }))
                 .size () == 2);
  sheet.advance (2.35f * s);
  const HexSheetTopology& topology = sheet.topology ();
  const std::array corners {
    topology.tile_id ({ 0, 0 }),
    topology.tile_id ({ hex_sheet_columns - 1, 0 }),
    topology.tile_id ({ 0, hex_sheet_rows - 1 }),
    topology.tile_id ({ hex_sheet_columns - 1, hex_sheet_rows - 1 }),
  };
  const auto anchor_count = std::ranges::count_if (
    std::views::iota (TileId { 0 }, TileId { topology.size () }),
    [&] (TileId id) { return topology.is_anchor (id); });
  MOPPE_CHECK (anchor_count == 4);
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

MOPPE_TEST (noise_drive_deforms_the_adaptive_sheet) {
  HexSheet sheet;
  sheet.advance (1.0f * s);
  const auto [minimum, maximum] =
    std::ranges::minmax_element (sheet.displacements ());
  MOPPE_CHECK (*maximum - *minimum > 0.01f * m);
}

MOPPE_TEST (hex_sheet_rebuilds_as_a_mixed_size_simulated_partition) {
  HexSheet sheet;
  sheet.advance (3.1f * s);

  const std::size_t refined = sheet.refined_cell_count ();
  MOPPE_CHECK (refined == 7);
  MOPPE_CHECK (sheet.topology ().size () ==
               hex_sheet_base_cell_count + 6 * refined);
  MOPPE_CHECK (sheet.leaves ().size () == sheet.topology ().size ());
  MOPPE_CHECK (sheet.displacements ().size () == sheet.topology ().size ());

  const auto small_cells =
    std::ranges::count_if (sheet.leaves (), [] (const TileLeaf& tile) {
      return tile.site.generation > 0.5f;
    });
  MOPPE_CHECK (small_cells == 7 * refined);

  bool mixed_size_edge = false;
  for (TileId id = 0; id < sheet.topology ().size (); ++id) {
    for (TileId neighbour : sheet.topology ().neighbours (id)) {
      if (sheet.topology ().site (id).radius !=
          sheet.topology ().site (neighbour).radius)
        mixed_size_edge = true;
    }
  }
  MOPPE_CHECK (mixed_size_edge);
}

MOPPE_TEST (adaptive_partition_propagates_motion_through_the_tile_graph) {
  HexSheet sheet;
  sheet.advance (2.5f * s);

  const bool any_displacement = std::ranges::any_of (
    sheet.displacements (), [] (NormalDisplacement displacement) {
      return std::abs (displacement.numerical_value_in (m)) > 0.001f;
    });
  MOPPE_CHECK (any_displacement);
}
