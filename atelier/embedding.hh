#pragma once

#include "atelier/landscape.hh"
#include "atelier/matrix.hh"

#include <vector>

// World embeddings are views of the same intrinsic, closed landscape.  Tile
// simulation and refinement never see this choice: a renderer may wrap the
// quotient into a doughnut or show several copies of its flat universal cover.

namespace atelier {
  enum class EmbeddingKind { toroidal_shell, repeating_plane };

  struct EmbeddedTile {
    Matrix place_in_world;
    DeformationReading deformation;
    Real generation;
  };

  struct EmbeddedLandscape {
    Matrix world_to_clip;
    Point eye;
    std::vector<EmbeddedTile> tiles;
  };

  [[nodiscard]] EmbeddedLandscape
  embed_landscape (const std::vector<TileLeaf>& leaves,
                   EmbeddingKind kind,
                   Duration elapsed,
                   Real aspect_ratio);
}
