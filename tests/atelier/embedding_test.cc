#include <atelier/embedding.hh>

#include <tests/test.hh>

using namespace atelier;
using namespace mp_units::si::unit_symbols;

MOPPE_TEST (embeddings_leave_the_intrinsic_partition_unchanged) {
  HexSheet sheet;
  sheet.advance (2.35f * s);
  const std::vector<TileLeaf> leaves = sheet.leaves ();

  const EmbeddedHexSheet shell =
    embed_hex_sheet (sheet, EmbeddingKind::toroidal_shell, 2.35f * s, 1.5f);
  const EmbeddedHexSheet plane =
    embed_hex_sheet (sheet, EmbeddingKind::repeating_plane, 2.35f * s, 1.5f);

  MOPPE_CHECK (shell.tiles.size () == leaves.size ());
  MOPPE_CHECK (plane.tiles.size () == 15 * leaves.size ());
  MOPPE_CHECK (shell.ligaments.size () == 3 * hex_sheet_tile_count);
  MOPPE_CHECK (plane.ligaments.size () == 15 * 3 * hex_sheet_tile_count);
  MOPPE_CHECK (sheet.leaves ().size () == leaves.size ());
}

MOPPE_TEST (flat_embedding_repeats_identical_tile_readings) {
  HexSheet sheet;
  sheet.advance (2.35f * s);
  const std::vector<TileLeaf> leaves = sheet.leaves ();
  const EmbeddedHexSheet plane =
    embed_hex_sheet (sheet, EmbeddingKind::repeating_plane, 2.35f * s, 1.5f);

  const EmbeddedTile& first = plane.tiles.front ();
  for (std::size_t image = 1; image < 15; ++image) {
    const EmbeddedTile& copy = plane.tiles[image * leaves.size ()];
    MOPPE_CHECK_NEAR (copy.deformation.numerical_value_in (mp_units::one),
                      first.deformation.numerical_value_in (mp_units::one),
                      0.0001f);
    MOPPE_CHECK_NEAR (copy.generation, first.generation, 0.0001f);
    MOPPE_CHECK_NEAR (copy.material_seed, first.material_seed, 0.0001f);
  }
}

MOPPE_TEST (rope_bridge_embeds_one_finite_sheet_without_wrapping) {
  HexSheet sheet (SheetBoundary::open);
  sheet.advance (2.35f * s);
  const EmbeddedHexSheet bridge =
    embed_hex_sheet (sheet, EmbeddingKind::rope_bridge, 2.35f * s, 1.5f);

  std::size_t degree_sum = 0;
  for (TileId id = 0; id < hex_sheet_tile_count; ++id)
    degree_sum += sheet.topology ().neighbours (id).size ();

  MOPPE_CHECK (bridge.tiles.size () == sheet.leaves ().size ());
  MOPPE_CHECK (bridge.ligaments.size () == degree_sum / 2);
  MOPPE_CHECK (bridge.ligaments.size () < 3 * hex_sheet_tile_count);
}
