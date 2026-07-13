#include "atelier/atelier.hh"

#include <stdexcept>

namespace atelier {
  using namespace si::unit_symbols;

  namespace {
    simd_float4 glaze (const EmbeddedTile& tile) {
      constexpr simd_float3 clay { 0.73f, 0.42f, 0.22f };
      constexpr simd_float3 fired { 1.0f, 0.76f, 0.28f };
      constexpr simd_float3 new_growth { 0.52f, 0.85f, 0.76f };
      const Real deformation =
        tile.deformation.numerical_value_in (mp_units::one);
      const simd_float3 mature =
        simd_mix (clay, fired, simd_float3 (deformation));
      const simd_float3 colour =
        simd_mix (mature, new_growth, simd_float3 (tile.generation));
      return simd_make_float4 (colour, 1.0f);
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
