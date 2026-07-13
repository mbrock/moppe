#pragma once

#include "atelier/landscape.hh"
#include "atelier/matrix.hh"
#include "atelier/prism.hh"
#include "atelier/space.hh"
#include "atelier/wire.hh"

#include <cstddef>
#include <vector>

// The scene: a closed cellular landscape under a carousel camera, composed
// each frame into the flat instance data the renderer uploads.

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
    std::vector<TileInstance> tiles;
  };

  [[nodiscard]] Frame compose_frame (const Landscape& landscape,
                                     Duration elapsed,
                                     Viewport viewport);
}
