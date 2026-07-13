#pragma once

#include "atelier/hex_sheet.hh"
#include "atelier/matrix.hh"

#include <vector>

// World embeddings are views of the same intrinsic sheet. The embedding
// chooses both its boundary law and its placement in space.

namespace atelier {
  enum class EmbeddingKind { rope_bridge, toroidal_shell, repeating_plane };

  [[nodiscard]] SheetBoundary boundary_for (EmbeddingKind kind);

  struct EmbeddedTile {
    Matrix place_in_world;
    DeformationReading deformation;
    Real generation;
    Real material_seed;
  };

  struct EmbeddedLigament {
    simd_float3 start;
    simd_float3 end;
    simd_float3 start_normal;
    simd_float3 end_normal;
    Real strain;
    Real bend;
    Length radius;
    Real material_seed;
  };

  struct EmbeddedHexSheet {
    Matrix world_to_clip;
    Point eye;
    std::vector<EmbeddedTile> tiles;
    std::vector<EmbeddedLigament> ligaments;
  };

  [[nodiscard]] EmbeddedHexSheet embed_hex_sheet (const HexSheet& sheet,
                                                  EmbeddingKind kind,
                                                  Duration elapsed,
                                                  Real aspect_ratio);
}
