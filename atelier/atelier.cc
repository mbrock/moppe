#include "atelier/atelier.hh"

#include <stdexcept>

namespace atelier {
  using namespace si::unit_symbols;

  namespace {
    simd_float4 material_parameters (const EmbeddedTile& tile) {
      constexpr simd_float3 ivory { 0.74f, 0.66f, 0.56f };
      constexpr simd_float3 compressed { 0.64f, 0.54f, 0.50f };
      constexpr simd_float3 new_growth { 0.64f, 0.70f, 0.62f };
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

  Frame compose_frame (const HexSheet& sheet,
                       EmbeddingKind embedding,
                       Duration elapsed,
                       Viewport viewport) {
    const EmbeddedHexSheet world =
      embed_hex_sheet (sheet, embedding, elapsed, viewport.aspect_ratio ());
    Frame frame {
      .uniforms =
        Uniforms {
          .world_to_clip = world.world_to_clip,
          .eye = simd_make_float4 (in_metres (world.eye), 1),
          .atmosphere =
            simd_make_float4 (elapsed.numerical_value_in (s), 0.0f, 0.0f, 0.0f),
        },
      .tiles = {},
      .ligaments = {},
    };
    frame.tiles.reserve (world.tiles.size ());
    for (const EmbeddedTile& tile : world.tiles)
      frame.tiles.push_back (
        { tile.place_in_world, material_parameters (tile) });
    frame.ligaments.reserve (world.ligaments.size ());
    for (const EmbeddedLigament& ligament : world.ligaments) {
      frame.ligaments.push_back ({
        .start = simd_make_float4 (ligament.start, ligament.strain),
        .end = simd_make_float4 (ligament.end, ligament.bend),
        .start_normal =
          simd_make_float4 (ligament.start_normal, ligament.material_seed),
        .end_normal = simd_make_float4 (ligament.end_normal, 0),
      });
    }
    return frame;
  }
}
