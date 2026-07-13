#pragma once

#include "atelier/space.hh"
#include "atelier/tabulate.hh"

#include <algorithm>
#include <array>
#include <numbers>

// The carpet: a honeycomb of hexagonal tiles, joined edge to edge and
// riding a slow swell.  Tiles do not move on their own; the carpet
// moves, and every tile floats and tilts to follow it, so neighbours
// stay connected.

namespace atelier {
  // A lattice cell, in axial hex coordinates.
  struct Cell {
    int q, r;

    [[nodiscard]] constexpr int ring () const {
      const int s = -q - r;
      return std::max ({ q < 0 ? -q : q, r < 0 ? -r : r, s < 0 ? -s : s });
    }
  };

  inline constexpr int carpet_rings = 2;
  inline constexpr std::size_t tile_count =
    3 * carpet_rings * (carpet_rings + 1) + 1;

  [[nodiscard]] consteval std::array<Cell, tile_count> hex_cells () {
    std::array<Cell, tile_count> cells {};
    std::size_t count = 0;
    for (int q = -carpet_rings; q <= carpet_rings; ++q)
      for (int r = -carpet_rings; r <= carpet_rings; ++r)
        if (Cell { q, r }.ring () <= carpet_rings)
          cells[count++] = { q, r };
    return cells;
  }

  inline constexpr std::array<Cell, tile_count> carpet_cells = hex_cells ();

  // One tile reaches from its centre to its six corners; neighbouring
  // centres sit sqrt(3) radii apart, so hexagons meet edge to edge.
  inline constexpr Length tile_radius = 1.0f * si::metre;

  // How a tile stands at one instant: where its centre floats, and
  // which way its face points.
  struct Pose {
    Point centre;
    Vec3 upward;
  };

  // One travelling wave of the swell.
  struct Swell {
    Length amplitude;
    Wavenumber ripple;
    AngularRate tempo;
    Vec3 heading;

    [[nodiscard]] Length lift (const Point& p, Duration t) const {
      return amplitude *
             angular::sin (ripple * along (p - scene, heading) + tempo * t);
    }
  };

  struct Carpet {
    Length pitch = std::numbers::sqrt3_v<Real> * tile_radius;
    Length hover = 1.1f * si::metre;
    std::array<Swell, 2> swells {
      Swell {
        .amplitude = 0.16f * si::metre,
        .ripple = 0.5f * angular::radian / si::metre,
        .tempo = 0.05f * angular::revolution / si::second,
        .heading = radial (65.0f * angular::degree),
      },
      Swell {
        .amplitude = 0.08f * si::metre,
        .ripple = 0.8f * angular::radian / si::metre,
        .tempo = -0.035f * angular::revolution / si::second,
        .heading = radial (150.0f * angular::degree),
      },
    };

    [[nodiscard]] Point centre_of (Cell cell) const {
      const Real across = Real (cell.q) + Real (cell.r) / 2;
      const Real away = Real (cell.r) * (std::numbers::sqrt3_v<Real> / 2);
      return scene + (east * across + north * away) * pitch;
    }

    // How high the carpet floats above a point of the ground plane.
    [[nodiscard]] Length lift (const Point& p, Duration t) const {
      return std::ranges::fold_left (
        swells, hover, [&] (Length sum, const Swell& swell) {
          return sum + swell.lift (p, t);
        });
    }

    // A tile floats up to the carpet at its centre and tilts to match
    // the carpet's slope, sampled a tile radius out, so neighbouring
    // tiles keep facing each other across their shared edge.
    [[nodiscard]] Pose pose (Cell cell, Duration t) const {
      const Point rest = centre_of (cell);
      const auto floated = [&] (const Point& p) {
        return p + up * lift (p, t);
      };
      const Point centre = floated (rest);
      const Displacement to_east = floated (rest + east * tile_radius) - centre;
      const Displacement to_north =
        floated (rest + north * tile_radius) - centre;
      return {
        centre,
        vector_product (direction (to_north), direction (to_east)).unit ()
      };
    }

    // 0 at the golden centre of the carpet, 1 at its coppery edge.
    [[nodiscard]] Real patina (Cell cell) const {
      return Real (cell.ring ()) / carpet_rings;
    }
  };
}
