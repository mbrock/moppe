#pragma once

#include "atelier/space.hh"
#include "atelier/tabulate.hh"
#include "atelier/wire.hh"

#include <array>

// The hexagonal prism: the shape of one tile, described by the wire
// skeleton of its edges.

namespace atelier {
  struct Prism {
    static constexpr std::size_t sides = 6;
    static constexpr std::size_t edge_count = 3 * sides;

    Length radius;
    Length half_depth;

    // A rim vertex: `side` counts around the hexagon, `face` is +1 on
    // the upper face and -1 on the lower.
    [[nodiscard]] Point rim (std::size_t side, Real face) const {
      const Angle around = (Real (side) / sides) * angular::revolution;
      return scene + radial (around) * radius + up * (face * half_depth);
    }

    // Each side contributes three edges: a stretch of upper rim, a
    // stretch of lower rim, and the pillar between them.
    [[nodiscard]] std::array<Segment, edge_count> edges () const {
      return tabulate<edge_count> ([this] (std::size_t index) {
        const std::size_t side = index / 3;
        const std::size_t next = (side + 1) % sides;
        switch (index % 3) {
        case 0:
          return Segment { rim (side, +1), rim (next, +1) };
        case 1:
          return Segment { rim (side, -1), rim (next, -1) };
        default:
          return Segment { rim (side, +1), rim (side, -1) };
        }
      });
    }
  };
}
