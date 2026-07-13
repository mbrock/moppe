#include "atelier/atelier.hh"

#include "atelier/camera.hh"

#include <stdexcept>

namespace atelier {
  using namespace si::unit_symbols;

  namespace {
    simd_float4 glaze (const TileVisual& tile) {
      constexpr simd_float3 clay { 0.73f, 0.42f, 0.22f };
      constexpr simd_float3 fired { 1.0f, 0.76f, 0.28f };
      constexpr simd_float3 new_growth { 0.52f, 0.85f, 0.76f };
      const simd_float3 mature =
        simd_mix (clay, fired, simd_float3 (tile.stress));
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
                       Duration elapsed,
                       Viewport viewport) {
    const Carousel camera;
    Frame frame {
      .uniforms =
        Uniforms {
          .world_to_clip =
            camera.world_to_clip (elapsed, viewport.aspect_ratio ()),
          .eye = simd_make_float4 (in_metres (camera.eye (elapsed)), 1),
        },
      .tiles = {},
    };
    const std::vector<TileVisual> visuals = landscape.visuals ();
    frame.tiles.reserve (visuals.size ());
    for (const TileVisual& tile : visuals)
      frame.tiles.push_back ({ tile.place_in_world, glaze (tile) });
    return frame;
  }
}
