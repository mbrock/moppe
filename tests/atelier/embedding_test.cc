#include <atelier/embedding.hh>

#include <tests/test.hh>

using namespace atelier;
using namespace mp_units::si::unit_symbols;

MOPPE_TEST (embeddings_leave_the_intrinsic_partition_unchanged) {
  Landscape landscape;
  landscape.advance (2.35f * s);
  const std::vector<TileLeaf> leaves = landscape.leaves ();

  const EmbeddedLandscape shell =
    embed_landscape (leaves, EmbeddingKind::toroidal_shell, 2.35f * s, 1.5f);
  const EmbeddedLandscape plane =
    embed_landscape (leaves, EmbeddingKind::repeating_plane, 2.35f * s, 1.5f);

  MOPPE_CHECK (shell.tiles.size () == leaves.size ());
  MOPPE_CHECK (plane.tiles.size () == 15 * leaves.size ());
  MOPPE_CHECK (landscape.leaves ().size () == leaves.size ());
}

MOPPE_TEST (flat_embedding_repeats_identical_tile_readings) {
  Landscape landscape;
  landscape.advance (2.35f * s);
  const std::vector<TileLeaf> leaves = landscape.leaves ();
  const EmbeddedLandscape plane =
    embed_landscape (leaves, EmbeddingKind::repeating_plane, 2.35f * s, 1.5f);

  const EmbeddedTile& first = plane.tiles.front ();
  for (std::size_t image = 1; image < 15; ++image) {
    const EmbeddedTile& copy = plane.tiles[image * leaves.size ()];
    MOPPE_CHECK_NEAR (copy.deformation.numerical_value_in (mp_units::one),
                      first.deformation.numerical_value_in (mp_units::one),
                      0.0001f);
    MOPPE_CHECK_NEAR (copy.generation, first.generation, 0.0001f);
  }
}
