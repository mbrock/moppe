#include "atelier/atelier.hh"

#include <stdexcept>

namespace atelier {
  using namespace si::unit_symbols;

  namespace {
    simd_float4 glaze (const EmbeddedTile& tile) {
      constexpr simd_float3 ivory { 0.76f, 0.74f, 0.69f };
      constexpr simd_float3 compressed { 0.68f, 0.65f, 0.64f };
      constexpr simd_float3 new_growth { 0.70f, 0.76f, 0.72f };
      const Real deformation =
        tile.deformation.numerical_value_in (mp_units::one);
      const simd_float3 mature =
        simd_mix (ivory, compressed, simd_float3 (deformation));
      const simd_float3 colour =
        simd_mix (mature, new_growth, simd_float3 (tile.generation));
      return simd_make_float4 (colour, tile.material_seed);
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

  Frame compose_frame (const Landscape& landscape,
                       EmbeddingKind embedding,
                       Duration elapsed,
                       Viewport viewport) {
    const EmbeddedLandscape world = embed_landscape (
      landscape.leaves (), embedding, elapsed, viewport.aspect_ratio ());
    Frame frame {
      .uniforms =
        Uniforms {
          .world_to_clip = world.world_to_clip,
          .eye = simd_make_float4 (in_metres (world.eye), 1),
        },
      .tiles = {},
    };
    frame.tiles.reserve (world.tiles.size ());
    for (const EmbeddedTile& tile : world.tiles)
      frame.tiles.push_back ({ tile.place_in_world, glaze (tile) });
    return frame;
  }
}
