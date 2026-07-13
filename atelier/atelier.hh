#pragma once

#include <simd/simd.h>

#include <chrono>
#include <cstddef>
#include <vector>

namespace atelier {
  using VertexPosition = simd_float3;

  struct Viewport {
    std::size_t width = 0;
    std::size_t height = 0;

    [[nodiscard]] bool is_empty () const;
    [[nodiscard]] float aspect_ratio () const;
  };

  struct Uniforms {
    simd_float4x4 world_to_clip;
  };

  struct Frame {
    Uniforms uniforms;
  };

  std::vector<VertexPosition> make_coin_wireframe ();
  Frame make_frame (std::chrono::steady_clock::duration elapsed,
                    Viewport viewport);
}
