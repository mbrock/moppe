#pragma once

#include "atelier/space.hh"

#include <array>
#include <cstddef>

namespace atelier {
  // A viewport measured in physical pixels.
  struct Viewport {
    std::size_t width = 0;
    std::size_t height = 0;

    [[nodiscard]] bool is_empty () const;
    [[nodiscard]] Real aspect_ratio () const;
  };

  // The coins rest on a hexagonal carpet: one coin at the centre and
  // `carpet_rings` full rings around it.
  inline constexpr int carpet_rings = 2;
  inline constexpr std::size_t coin_count =
    3 * carpet_rings * (carpet_rings + 1) + 1;

  // GPU-facing frame data, mirrored field for field by the shader
  // structs in atelier_shaders.hh.
  struct Uniforms {
    simd_float4x4 world_to_clip;
    simd_float4 eye; // the camera's place, in metres from the origin
  };

  struct CoinInstance {
    simd_float4x4 place_in_world;
    simd_float4 glaze;
  };

  struct Frame {
    Uniforms uniforms;
    std::array<CoinInstance, coin_count> coins;
  };

  // One coin's wireframe: a hexagonal prism of eighteen edges (two
  // rims and six pillars), each edge thickened into a box of twelve
  // triangles.  Every coin on the carpet is an instance of this mesh.
  struct WireVertex {
    simd_float3 position; // metres, in the coin's own space
    simd_float3 normal;   // unit length, pointing out of the wire
  };

  inline constexpr std::size_t coin_sides = 6;
  inline constexpr std::size_t coin_edge_count = 3 * coin_sides;
  inline constexpr std::size_t wire_vertex_count = 36;
  using CoinMesh = std::array<WireVertex, coin_edge_count * wire_vertex_count>;

  [[nodiscard]] CoinMesh coin_wireframe ();
  [[nodiscard]] Frame compose_frame (Duration elapsed, Viewport viewport);
}
