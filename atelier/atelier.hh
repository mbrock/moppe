#pragma once

#include "atelier/carpet.hh"
#include "atelier/matrix.hh"
#include "atelier/prism.hh"
#include "atelier/space.hh"
#include "atelier/wire.hh"

#include <array>
#include <cstddef>

// The scene: a carpet of wireframe tiles under a carousel camera,
// composed each frame into the flat data the renderer uploads.

namespace atelier {
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
  };

  struct TileInstance {
    Matrix place_in_world;
    simd_float4 glaze;
  };

  struct Frame {
    Uniforms uniforms;
    std::array<TileInstance, tile_count> tiles;
  };

  // Every tile on the carpet is an instance of one wireframe prism.
  using TileMesh = Wireframe<Prism::edge_count>;

  [[nodiscard]] TileMesh tile_wireframe ();
  [[nodiscard]] Frame compose_frame (Duration elapsed, Viewport viewport);
}
