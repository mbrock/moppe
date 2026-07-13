#pragma once

#include "atelier/embedding.hh"
#include "atelier/hex_sheet.hh"
#include "atelier/matrix.hh"
#include "atelier/prism.hh"
#include "atelier/space.hh"
#include "atelier/tree_embedding.hh"
#include "atelier/wire.hh"

#include <cstddef>
#include <vector>

// The scene: a cellular sheet composed each frame into the flat instance data
// the renderer uploads.

namespace atelier {
  enum class SceneKind {
    tree_wind,
    tree_diagram,
    rope_bridge,
    toroidal_sheet,
    repeating_sheet,
  };

  // A viewport measured in physical pixels.
  struct Viewport {
    std::size_t width = 0;
    std::size_t height = 0;

    [[nodiscard]] bool is_empty () const;
    [[nodiscard]] Real aspect_ratio () const;
  };

  // GPU-facing frame data, mirrored field for field by the shader
  // structs in atelier_shaders.hh.
  struct Uniforms {
    Matrix world_to_clip;
    simd_float4 eye; // the camera's place, in metres from the origin
    // x is elapsed time in seconds; the remaining lanes are reserved for
    // atmosphere controls that should stay global rather than per tile.
    simd_float4 atmosphere;
  };

  struct TileInstance {
    Matrix place_in_world;
    // RGB is the body colour and W is a stable intrinsic material seed.
    simd_float4 material;
  };

  struct LigamentInstance {
    // Endpoint W lanes carry strain and signed bend respectively.  Normal W
    // carries the stable material seed and branch radius.
    simd_float4 start;
    simd_float4 end;
    simd_float4 start_normal;
    simd_float4 end_normal;
    // Xylem, phloem, and two subject-specific diagnostic lanes.
    simd_float4 physiology;
  };

  struct Frame {
    Uniforms uniforms;
    std::vector<TileInstance> tiles;
    std::vector<LigamentInstance> ligaments;
  };

  [[nodiscard]] Frame compose_frame (const HexSheet& sheet,
                                     EmbeddingKind embedding,
                                     Duration elapsed,
                                     Viewport viewport);
  [[nodiscard]] Frame compose_frame (const Tree& tree,
                                     TreeEmbeddingKind embedding,
                                     Duration elapsed,
                                     Viewport viewport);
}
