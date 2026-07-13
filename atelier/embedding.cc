#include "atelier/embedding.hh"

#include <cmath>
#include <cstdint>
#include <numbers>

namespace atelier {
  using namespace si::unit_symbols;

  namespace {
    constexpr Length major_radius = 4.5f * m;
    constexpr Length minor_radius = 1.7f * m;
    constexpr Length shell_hover = 0.06f * m;
    constexpr Length ligament_attachment = 0.58f * puck_radius;
    constexpr Length ligament_hover = 0.080f * m;
    constexpr Length ligament_rest_length =
      std::numbers::sqrt3_v<Real> * puck_radius;

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

    SurfaceFrame toroidal_frame (GridCell cell,
                                 NormalDisplacement displacement,
                                 Length offset_across = {},
                                 Length offset_away = {}) {
      const Real row_offset = cell.row % 2 == 0 ? 0.0f : 0.5f;
      const Real u = 2.0f * std::numbers::pi_v<Real> *
                     (Real (cell.column) + row_offset) /
                     Real (landscape_columns);
      const Real v = 2.0f * std::numbers::pi_v<Real> * Real (cell.row) /
                     Real (landscape_rows);
      const Real cu = std::cos (u), su = std::sin (u);
      const Real cv = std::cos (v), sv = std::sin (v);
      const simd_float3 outward { cv * cu, sv, cv * su };
      const simd_float3 across { -su, 0, cu };
      const simd_float3 away { -sv * cu, cv, -sv * su };
      const Real major = in_metres (major_radius);
      const Real minor = in_metres (minor_radius);
      const Real lift = in_metres (shell_hover + displacement);
      const simd_float3 surface { (major + minor * cv) * cu,
                                  minor * sv,
                                  (major + minor * cv) * su };
      return {
        .centre = surface + outward * lift +
                  across * in_metres (offset_across) +
                  away * in_metres (offset_away),
        .across = across,
        .outward = outward,
        .away = away,
      };
    }

    SurfaceFrame toroidal_frame (const TileLeaf& tile) {
      return toroidal_frame (tile.cell,
                             tile.normal_displacement,
                             tile.offset_across,
                             tile.offset_away);
    }

    SurfaceFrame flat_frame (int column,
                             int row,
                             NormalDisplacement displacement,
                             Length offset_across = {},
                             Length offset_away = {}) {
      constexpr Length pitch_across = std::numbers::sqrt3_v<Real> * puck_radius;
      constexpr Length pitch_away = 1.5f * puck_radius;
      constexpr Length period_across = Real (landscape_columns) * pitch_across;
      constexpr Length period_away = Real (landscape_rows) * pitch_away;
      const Real row_offset = row % 2 == 0 ? 0.0f : 0.5f;
      const Length x = (Real (column) + row_offset) * pitch_across -
                       period_across / 2 + offset_across;
      const Length z = Real (row) * pitch_away - period_away / 2 + offset_away;
      return {
        .centre = { in_metres (x), in_metres (displacement), in_metres (z) },
        .across = as_simd (east),
        .outward = as_simd (up),
        .away = as_simd (north),
      };
    }

    SurfaceFrame
    flat_frame (const TileLeaf& tile, int image_across, int image_away) {
      return flat_frame (static_cast<int> (tile.cell.column) +
                           image_across * static_cast<int> (landscape_columns),
                         static_cast<int> (tile.cell.row) +
                           image_away * static_cast<int> (landscape_rows),
                         tile.normal_displacement,
                         tile.offset_across,
                         tile.offset_away);
    }

    Real ligament_seed (TileId first, TileId second) {
      std::uint32_t bits =
        static_cast<std::uint32_t> (first * 0x9e3779b9U + second * 0x85ebca6bU);
      bits ^= bits >> 16;
      bits *= 0x7feb352dU;
      bits ^= bits >> 15;
      return Real (bits & 0x00ffffffU) / Real (0x01000000U);
    }

    EmbeddedLigament make_ligament (const SurfaceFrame& first,
                                    const SurfaceFrame& second,
                                    NormalDisplacement first_displacement,
                                    NormalDisplacement second_displacement,
                                    Real seed) {
      const simd_float3 between = second.centre - first.centre;
      simd_float3 first_direction =
        between - first.outward * simd_dot (between, first.outward);
      simd_float3 second_direction =
        -between + second.outward * simd_dot (between, second.outward);
      first_direction = simd_normalize (first_direction);
      second_direction = simd_normalize (second_direction);
      const Real attachment = in_metres (ligament_attachment);
      const Real hover = in_metres (ligament_hover);
      const Real bend = Real ((second_displacement - first_displacement) /
                              ligament_rest_length);
      const Real strain = std::sqrt (1.0f + bend * bend) - 1.0f;
      return {
        .start =
          first.centre + attachment * first_direction + hover * first.outward,
        .end = second.centre + attachment * second_direction +
               hover * second.outward,
        .start_normal = first.outward,
        .end_normal = second.outward,
        .strain = strain,
        .bend = bend,
        .material_seed = seed,
      };
    }

    void append_toroidal_ligaments (EmbeddedLandscape& result,
                                    const Landscape& landscape) {
      const auto& topology = landscape.topology ();
      const auto& displacement = landscape.displacements ();
      result.ligaments.reserve (3 * landscape_tile_count);
      for (TileId id = 0; id < landscape_tile_count; ++id) {
        for (std::size_t side = 0; side < 6; ++side) {
          const TileId neighbour = topology.neighbour (id, side);
          if (id >= neighbour)
            continue;
          result.ligaments.push_back (make_ligament (
            toroidal_frame (topology.cell (id), displacement[id]),
            toroidal_frame (topology.cell (neighbour), displacement[neighbour]),
            displacement[id],
            displacement[neighbour],
            ligament_seed (id, neighbour)));
        }
      }
    }

    void append_flat_ligaments (EmbeddedLandscape& result,
                                const Landscape& landscape,
                                int image_across,
                                int image_away) {
      const auto& topology = landscape.topology ();
      const auto& displacement = landscape.displacements ();
      for (TileId id = 0; id < landscape_tile_count; ++id) {
        const GridCell cell = topology.cell (id);
        const int column = static_cast<int> (cell.column) +
                           image_across * static_cast<int> (landscape_columns);
        const int row = static_cast<int> (cell.row) +
                        image_away * static_cast<int> (landscape_rows);
        for (std::size_t side = 0; side < 6; ++side) {
          const TileId neighbour = topology.neighbour (id, side);
          if (id >= neighbour)
            continue;
          const GridStep step = topology.neighbour_step (id, side);
          result.ligaments.push_back (make_ligament (
            flat_frame (column, row, displacement[id]),
            flat_frame (
              column + step.columns, row + step.rows, displacement[neighbour]),
            displacement[id],
            displacement[neighbour],
            ligament_seed (id, neighbour)));
        }
      }
    }

    EmbeddedLandscape toroidal_embedding (const Landscape& landscape,
                                          Duration elapsed,
                                          Real aspect_ratio) {
      const std::vector<TileLeaf> leaves = landscape.leaves ();
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
        .ligaments = {},
      };
      result.tiles.reserve (leaves.size ());
      for (const TileLeaf& tile : leaves)
        result.tiles.push_back (
          { placement (toroidal_frame (tile), tile.radius),
            tile.deformation,
            tile.generation,
            tile.material_seed });
      append_toroidal_ligaments (result, landscape);
      return result;
    }

    EmbeddedLandscape repeating_embedding (const Landscape& landscape,
                                           Real aspect_ratio) {
      const std::vector<TileLeaf> leaves = landscape.leaves ();
      constexpr Length pitch_across = std::numbers::sqrt3_v<Real> * puck_radius;
      constexpr Length period_across = Real (landscape_columns) * pitch_across;
      const Point target = scene - east * (5.75f * m);
      const Point eye =
        target + east * (11.0f * m) + up * (11.0f * m) + north * (11.0f * m);
      EmbeddedLandscape result {
        .world_to_clip = simd_mul (
          orthographic (
            0.45f * period_across, aspect_ratio, 0.05f * m, 80.0f * m),
          look_at (eye, target)),
        .eye = eye,
        .tiles = {},
        .ligaments = {},
      };
      result.tiles.reserve (15 * leaves.size ());
      result.ligaments.reserve (15 * 3 * landscape_tile_count);
      for (int image_away = -2; image_away <= 2; ++image_away) {
        for (int image_across = -1; image_across <= 1; ++image_across) {
          append_flat_ligaments (result, landscape, image_across, image_away);
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

  EmbeddedLandscape embed_landscape (const Landscape& landscape,
                                     EmbeddingKind kind,
                                     Duration elapsed,
                                     Real aspect_ratio) {
    switch (kind) {
    case EmbeddingKind::toroidal_shell:
      return toroidal_embedding (landscape, elapsed, aspect_ratio);
    case EmbeddingKind::repeating_plane:
      return repeating_embedding (landscape, aspect_ratio);
    }
  }
}
