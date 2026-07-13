#include "atelier/embedding.hh"

#include <cmath>
#include <cstdint>
#include <numbers>
#include <stdexcept>

namespace atelier {
  using namespace si::unit_symbols;

  namespace {
    constexpr Length major_radius = 4.5f * m;
    constexpr Length minor_radius = 1.7f * m;
    constexpr Length shell_hover = 0.06f * m;
    constexpr Length ligament_hover = 0.080f * m;
    constexpr Length ligament_rest_length =
      std::numbers::sqrt3_v<Real> * puck_radius;
    constexpr Length bridge_length =
      Real (hex_sheet_columns - 1) * ligament_rest_length;
    constexpr Length bridge_width =
      Real (hex_sheet_rows - 1) * 1.5f * puck_radius;
    constexpr Length bridge_height = 3.2f * m;
    constexpr Length bridge_sag = 3.0f * m;

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
                     Real (hex_sheet_columns);
      const Real v = 2.0f * std::numbers::pi_v<Real> * Real (cell.row) /
                     Real (hex_sheet_rows);
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

    SurfaceFrame toroidal_frame (const HexSite& site,
                                 NormalDisplacement displacement) {
      return toroidal_frame (
        site.base, displacement, site.offset_across, site.offset_away);
    }

    SurfaceFrame toroidal_frame (const TileLeaf& tile) {
      return toroidal_frame (tile.site, tile.normal_displacement);
    }

    SurfaceFrame flat_frame (int column,
                             int row,
                             NormalDisplacement displacement,
                             Length offset_across = {},
                             Length offset_away = {}) {
      constexpr Length pitch_across = std::numbers::sqrt3_v<Real> * puck_radius;
      constexpr Length pitch_away = 1.5f * puck_radius;
      constexpr Length period_across = Real (hex_sheet_columns) * pitch_across;
      constexpr Length period_away = Real (hex_sheet_rows) * pitch_away;
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

    SurfaceFrame flat_frame (const HexSite& site,
                             NormalDisplacement displacement,
                             int image_across,
                             int image_away) {
      return flat_frame (static_cast<int> (site.base.column) +
                           image_across * static_cast<int> (hex_sheet_columns),
                         static_cast<int> (site.base.row) +
                           image_away * static_cast<int> (hex_sheet_rows),
                         displacement,
                         site.offset_across,
                         site.offset_away);
    }

    SurfaceFrame
    flat_frame (const TileLeaf& tile, int image_across, int image_away) {
      return flat_frame (
        tile.site, tile.normal_displacement, image_across, image_away);
    }

    SurfaceFrame bridge_frame (GridCell cell,
                               NormalDisplacement displacement,
                               Length offset_across = {},
                               Length offset_away = {}) {
      const Real u = Real (cell.column) / Real (hex_sheet_columns - 1);
      const Real v = Real (cell.row) / Real (hex_sheet_rows - 1);
      const Real row_offset = cell.row % 2 == 0 ? 0.0f : 0.5f;
      const Real pitch = in_metres (ligament_rest_length);
      const Real x = (Real (cell.column) + row_offset -
                      0.5f * Real (hex_sheet_columns - 1)) *
                     pitch;
      const Real z = (v - 0.5f) * in_metres (bridge_width);
      const Real sag = 0.5f * in_metres (bridge_sag) *
                       (std::sin (std::numbers::pi_v<Real> * u) +
                        std::sin (std::numbers::pi_v<Real> * v));
      const Real across_slope = -0.5f * in_metres (bridge_sag) *
                                std::numbers::pi_v<Real> *
                                std::cos (std::numbers::pi_v<Real> * u);
      const Real away_slope = -0.5f * in_metres (bridge_sag) *
                              std::numbers::pi_v<Real> *
                              std::cos (std::numbers::pi_v<Real> * v);
      const simd_float3 across = simd_normalize (
        simd_float3 { in_metres (bridge_length), across_slope, 0 });
      const simd_float3 rough_away = simd_normalize (
        simd_float3 { 0, away_slope, in_metres (bridge_width) });
      const simd_float3 outward =
        simd_normalize (simd_cross (rough_away, across));
      const simd_float3 away = simd_normalize (simd_cross (across, outward));
      const simd_float3 surface { x, in_metres (bridge_height) - sag, z };
      return {
        .centre = surface + outward * in_metres (displacement) +
                  across * in_metres (offset_across) +
                  away * in_metres (offset_away),
        .across = across,
        .outward = outward,
        .away = away,
      };
    }

    SurfaceFrame bridge_frame (const HexSite& site,
                               NormalDisplacement displacement) {
      return bridge_frame (
        site.base, displacement, site.offset_across, site.offset_away);
    }

    SurfaceFrame bridge_frame (const TileLeaf& tile) {
      return bridge_frame (tile.site, tile.normal_displacement);
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
                                    Length first_radius,
                                    Length second_radius,
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
      const Real first_attachment = in_metres (0.58f * first_radius);
      const Real second_attachment = in_metres (0.58f * second_radius);
      const Real hover = in_metres (ligament_hover);
      const Length rest_length =
        std::numbers::sqrt3_v<Real> * (first_radius + second_radius) / 2.0f;
      const Real bend =
        Real ((second_displacement - first_displacement) / rest_length);
      const Real strain = std::sqrt (1.0f + bend * bend) - 1.0f;
      return {
        .start = first.centre + first_attachment * first_direction +
                 hover * first.outward,
        .end = second.centre + second_attachment * second_direction +
               hover * second.outward,
        .start_normal = first.outward,
        .end_normal = second.outward,
        .strain = strain,
        .bend = bend,
        .radius = 0.060f * m,
        .material_seed = seed,
      };
    }

    void append_toroidal_ligaments (EmbeddedHexSheet& result,
                                    const HexSheet& sheet) {
      const auto& topology = sheet.topology ();
      const auto& displacement = sheet.displacements ();
      result.ligaments.reserve (3 * topology.size ());
      for (TileId id = 0; id < topology.size (); ++id) {
        for (TileId neighbour : topology.neighbours (id)) {
          if (id >= neighbour)
            continue;
          result.ligaments.push_back (make_ligament (
            toroidal_frame (topology.site (id), displacement[id]),
            toroidal_frame (topology.site (neighbour), displacement[neighbour]),
            topology.site (id).radius,
            topology.site (neighbour).radius,
            displacement[id],
            displacement[neighbour],
            ligament_seed (id, neighbour)));
        }
      }
    }

    void append_flat_ligaments (EmbeddedHexSheet& result,
                                const HexSheet& sheet,
                                int image_across,
                                int image_away) {
      const auto& topology = sheet.topology ();
      const auto& displacement = sheet.displacements ();
      for (TileId id = 0; id < topology.size (); ++id) {
        const HexSite& site = topology.site (id);
        for (TileId neighbour : topology.neighbours (id)) {
          if (id >= neighbour)
            continue;
          const HexSite& adjacent = topology.site (neighbour);
          int adjacent_image_across = image_across;
          int adjacent_image_away = image_away;
          const int column_delta = static_cast<int> (adjacent.base.column) -
                                   static_cast<int> (site.base.column);
          const int row_delta = static_cast<int> (adjacent.base.row) -
                                static_cast<int> (site.base.row);
          if (column_delta > static_cast<int> (hex_sheet_columns / 2))
            --adjacent_image_across;
          else if (column_delta < -static_cast<int> (hex_sheet_columns / 2))
            ++adjacent_image_across;
          if (row_delta > static_cast<int> (hex_sheet_rows / 2))
            --adjacent_image_away;
          else if (row_delta < -static_cast<int> (hex_sheet_rows / 2))
            ++adjacent_image_away;
          result.ligaments.push_back (make_ligament (
            flat_frame (site, displacement[id], image_across, image_away),
            flat_frame (adjacent,
                        displacement[neighbour],
                        adjacent_image_across,
                        adjacent_image_away),
            site.radius,
            adjacent.radius,
            displacement[id],
            displacement[neighbour],
            ligament_seed (id, neighbour)));
        }
      }
    }

    void append_bridge_ligaments (EmbeddedHexSheet& result,
                                  const HexSheet& sheet) {
      const auto& topology = sheet.topology ();
      const auto& displacement = sheet.displacements ();
      for (TileId id = 0; id < topology.size (); ++id) {
        for (TileId neighbour : topology.neighbours (id)) {
          if (id >= neighbour)
            continue;
          result.ligaments.push_back (make_ligament (
            bridge_frame (topology.site (id), displacement[id]),
            bridge_frame (topology.site (neighbour), displacement[neighbour]),
            topology.site (id).radius,
            topology.site (neighbour).radius,
            displacement[id],
            displacement[neighbour],
            ligament_seed (id, neighbour)));
        }
      }
    }

    EmbeddedHexSheet bridge_embedding (const HexSheet& sheet,
                                       Real aspect_ratio) {
      const std::vector<TileLeaf> leaves = sheet.leaves ();
      const Point target = scene + up * (1.7f * m);
      const Point eye =
        target + east * (14.0f * m) + up * (13.0f * m) + north * (22.0f * m);
      EmbeddedHexSheet result {
        .world_to_clip = simd_mul (
          perspective (
            48.0f * angular::degree, aspect_ratio, 0.05f * m, 80.0f * m),
          look_at (eye, target)),
        .eye = eye,
        .tiles = {},
        .ligaments = {},
      };
      result.tiles.reserve (leaves.size ());
      for (const TileLeaf& tile : leaves)
        result.tiles.push_back (
          { placement (bridge_frame (tile), tile.site.radius),
            tile.deformation,
            tile.site.generation,
            tile.material_seed });
      append_bridge_ligaments (result, sheet);
      return result;
    }

    EmbeddedHexSheet toroidal_embedding (const HexSheet& sheet,
                                         Duration elapsed,
                                         Real aspect_ratio) {
      const std::vector<TileLeaf> leaves = sheet.leaves ();
      constexpr Length orbit_radius = 16.0f * m;
      constexpr Length ride_height = 8.0f * m;
      constexpr AngularRate rate = 0.01f * angular::revolution / si::second;
      const Point eye =
        scene + radial (rate * elapsed) * orbit_radius + up * ride_height;
      EmbeddedHexSheet result {
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
          { placement (toroidal_frame (tile), tile.site.radius),
            tile.deformation,
            tile.site.generation,
            tile.material_seed });
      append_toroidal_ligaments (result, sheet);
      return result;
    }

    EmbeddedHexSheet repeating_embedding (const HexSheet& sheet,
                                          Real aspect_ratio) {
      const std::vector<TileLeaf> leaves = sheet.leaves ();
      constexpr Length pitch_across = std::numbers::sqrt3_v<Real> * puck_radius;
      constexpr Length period_across = Real (hex_sheet_columns) * pitch_across;
      const Point target = scene - east * (5.75f * m);
      const Point eye =
        target + east * (11.0f * m) + up * (11.0f * m) + north * (11.0f * m);
      EmbeddedHexSheet result {
        .world_to_clip = simd_mul (
          orthographic (
            0.45f * period_across, aspect_ratio, 0.05f * m, 80.0f * m),
          look_at (eye, target)),
        .eye = eye,
        .tiles = {},
        .ligaments = {},
      };
      result.tiles.reserve (15 * leaves.size ());
      result.ligaments.reserve (15 * 3 * sheet.topology ().size ());
      for (int image_away = -2; image_away <= 2; ++image_away) {
        for (int image_across = -1; image_across <= 1; ++image_across) {
          append_flat_ligaments (result, sheet, image_across, image_away);
          for (const TileLeaf& tile : leaves)
            result.tiles.push_back (
              { placement (flat_frame (tile, image_across, image_away),
                           tile.site.radius),
                tile.deformation,
                tile.site.generation,
                tile.material_seed });
        }
      }
      return result;
    }
  }

  SheetBoundary boundary_for (EmbeddingKind kind) {
    return kind == EmbeddingKind::rope_bridge ? SheetBoundary::open
                                              : SheetBoundary::periodic;
  }

  EmbeddedHexSheet embed_hex_sheet (const HexSheet& sheet,
                                    EmbeddingKind kind,
                                    Duration elapsed,
                                    Real aspect_ratio) {
    if (sheet.topology ().boundary () != boundary_for (kind))
      throw std::invalid_argument (
        "The hex sheet boundary does not match its embedding");
    switch (kind) {
    case EmbeddingKind::rope_bridge:
      return bridge_embedding (sheet, aspect_ratio);
    case EmbeddingKind::toroidal_shell:
      return toroidal_embedding (sheet, elapsed, aspect_ratio);
    case EmbeddingKind::repeating_plane:
      return repeating_embedding (sheet, aspect_ratio);
    }
  }
}
