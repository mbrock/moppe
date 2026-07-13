#include "atelier/embedding.hh"

#include <cmath>
#include <numbers>

namespace atelier {
  using namespace si::unit_symbols;

  namespace {
    constexpr Length major_radius = 4.5f * m;
    constexpr Length minor_radius = 1.7f * m;
    constexpr Length shell_hover = 0.06f * m;

    struct SurfaceFrame {
      simd_float3 centre;
      simd_float3 across;
      simd_float3 outward;
      simd_float3 away;
    };

    Matrix placement (const SurfaceFrame& frame, Length radius) {
      const Real scale = in_metres (radius);
      return {
        simd_make_float4 (frame.across * scale, 0),
        simd_make_float4 (frame.outward * scale, 0),
        simd_make_float4 (frame.away * scale, 0),
        simd_make_float4 (frame.centre, 1),
      };
    }

    SurfaceFrame toroidal_frame (const TileLeaf& tile) {
      const Real row_offset = tile.cell.row % 2 == 0 ? 0.0f : 0.5f;
      const Real u = 2.0f * std::numbers::pi_v<Real> *
                     (Real (tile.cell.column) + row_offset) /
                     Real (landscape_columns);
      const Real v = 2.0f * std::numbers::pi_v<Real> * Real (tile.cell.row) /
                     Real (landscape_rows);
      const Real cu = std::cos (u), su = std::sin (u);
      const Real cv = std::cos (v), sv = std::sin (v);
      const simd_float3 outward { cv * cu, sv, cv * su };
      const simd_float3 across { -su, 0, cu };
      const simd_float3 away { -sv * cu, cv, -sv * su };
      const Real major = in_metres (major_radius);
      const Real minor = in_metres (minor_radius);
      const Real lift = in_metres (shell_hover + tile.normal_displacement);
      const simd_float3 surface { (major + minor * cv) * cu,
                                  minor * sv,
                                  (major + minor * cv) * su };
      return {
        .centre = surface + outward * lift +
                  across * in_metres (tile.offset_across) +
                  away * in_metres (tile.offset_away),
        .across = across,
        .outward = outward,
        .away = away,
      };
    }

    SurfaceFrame
    flat_frame (const TileLeaf& tile, int image_across, int image_away) {
      constexpr Length pitch_across = std::numbers::sqrt3_v<Real> * puck_radius;
      constexpr Length pitch_away = 1.5f * puck_radius;
      constexpr Length period_across = Real (landscape_columns) * pitch_across;
      constexpr Length period_away = Real (landscape_rows) * pitch_away;
      const Real row_offset = tile.cell.row % 2 == 0 ? 0.0f : 0.5f;
      const Length x = (Real (tile.cell.column) + row_offset) * pitch_across -
                       period_across / 2 + Real (image_across) * period_across +
                       tile.offset_across;
      const Length z = Real (tile.cell.row) * pitch_away - period_away / 2 +
                       Real (image_away) * period_away + tile.offset_away;
      return {
        .centre = { in_metres (x),
                    in_metres (tile.normal_displacement),
                    in_metres (z) },
        .across = as_simd (east),
        .outward = as_simd (up),
        .away = as_simd (north),
      };
    }

    EmbeddedLandscape toroidal_embedding (const std::vector<TileLeaf>& leaves,
                                          Duration elapsed,
                                          Real aspect_ratio) {
      constexpr Length orbit_radius = 16.0f * m;
      constexpr Length ride_height = 8.0f * m;
      constexpr AngularRate rate = 0.01f * angular::revolution / si::second;
      const Point eye =
        scene + radial (rate * elapsed) * orbit_radius + up * ride_height;
      EmbeddedLandscape result {
        .world_to_clip = simd_mul (
          perspective (
            42.0f * angular::degree, aspect_ratio, 0.05f * m, 80.0f * m),
          look_at (eye, scene + up * (0.0f * m))),
        .eye = eye,
        .tiles = {},
      };
      result.tiles.reserve (leaves.size ());
      for (const TileLeaf& tile : leaves)
        result.tiles.push_back (
          { placement (toroidal_frame (tile), tile.radius),
            tile.deformation,
            tile.generation,
            tile.material_seed });
      return result;
    }

    EmbeddedLandscape repeating_embedding (const std::vector<TileLeaf>& leaves,
                                           Real aspect_ratio) {
      constexpr Length pitch_across = std::numbers::sqrt3_v<Real> * puck_radius;
      constexpr Length period_across = Real (landscape_columns) * pitch_across;
      const Point target = scene - east * (5.75f * m);
      const Point eye =
        target + east * (11.0f * m) + up * (11.0f * m) + north * (11.0f * m);
      EmbeddedLandscape result {
        .world_to_clip = simd_mul (
          orthographic (
            0.58f * period_across, aspect_ratio, 0.05f * m, 80.0f * m),
          look_at (eye, target)),
        .eye = eye,
        .tiles = {},
      };
      result.tiles.reserve (15 * leaves.size ());
      for (int image_away = -2; image_away <= 2; ++image_away) {
        for (int image_across = -1; image_across <= 1; ++image_across) {
          for (const TileLeaf& tile : leaves)
            result.tiles.push_back (
              { placement (flat_frame (tile, image_across, image_away),
                           tile.radius),
                tile.deformation,
                tile.generation,
                tile.material_seed });
        }
      }
      return result;
    }
  }

  EmbeddedLandscape embed_landscape (const std::vector<TileLeaf>& leaves,
                                     EmbeddingKind kind,
                                     Duration elapsed,
                                     Real aspect_ratio) {
    switch (kind) {
    case EmbeddingKind::toroidal_shell:
      return toroidal_embedding (leaves, elapsed, aspect_ratio);
    case EmbeddingKind::repeating_plane:
      return repeating_embedding (leaves, aspect_ratio);
    }
  }
}
