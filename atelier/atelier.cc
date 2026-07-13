#include "atelier/atelier.hh"

#include "atelier/camera.hh"
#include "atelier/tabulate.hh"

#include <stdexcept>

namespace atelier {
  using namespace si::unit_symbols;

  namespace {
    // The tile: a hexagonal prism cut a seam short of its cell, so
    // that neighbouring tiles' wires sit side by side across their
    // shared edge instead of colliding.
    constexpr Length seam = 3.0f * cm;
    constexpr Prism tile { .radius = tile_radius - seam,
                           .half_depth = 16.0f * cm };
    constexpr Length wire_radius = 2.5f * cm;

    // The glaze a tile is dipped in: gold at the heart of the carpet,
    // weathering toward copper at its edge.
    simd_float4 glaze (Real patina) {
      constexpr simd_float3 gold { 1.0f, 0.78f, 0.26f };
      constexpr simd_float3 copper { 0.84f, 0.42f, 0.28f };
      return simd_make_float4 (simd_mix (gold, copper, simd_float3 (patina)),
                               1.0f);
    }

    Matrix place_in_world (const Pose& pose) {
      return simd_mul (translation (pose.centre), rotation (up, pose.upward));
    }
  }

  bool Viewport::is_empty () const {
    return width == 0 || height == 0;
  }

  Real Viewport::aspect_ratio () const {
    if (is_empty ())
      throw std::invalid_argument ("An empty viewport has no aspect ratio");
    return Real (width) / Real (height);
  }

  TileMesh tile_wireframe () {
    return wireframe (tile.edges (), wire_radius);
  }

  Frame compose_frame (Duration elapsed, Viewport viewport) {
    const Carousel camera;
    const Carpet carpet;
    return {
      .uniforms = {
        .world_to_clip =
          camera.world_to_clip (elapsed, viewport.aspect_ratio ()),
        .eye = simd_make_float4 (in_metres (camera.eye (elapsed)), 1),
      },
      .tiles = tabulate<tile_count> ([&] (std::size_t index) {
        const Cell cell = carpet_cells[index];
        return TileInstance { place_in_world (carpet.pose (cell, elapsed)),
                              glaze (carpet.patina (cell)) };
      }),
    };
  }
}
