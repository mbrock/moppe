#pragma once

#include "atelier/space.hh"
#include "atelier/tabulate.hh"

#include <algorithm>
#include <array>
#include <numbers>
#include <ranges>

// Wire: line segments drawn as slender boxes that catch the light.
// A segment is thickened into a square-sectioned box of twelve
// triangles, each vertex carrying the outward direction of the
// surface so a shader can shade the wire like a solid.

namespace atelier {
  // A straight run of wire between two places.
  struct Segment {
    Point from;
    Point to;
  };

  // GPU-facing: one vertex of a thickened wire.
  struct WireVertex {
    simd_float3 position; // metres, in model space
    simd_float3 normal;   // unit length, pointing out of the wire
  };

  inline constexpr std::size_t wire_vertex_count = 36;

  // The wireframe of N segments, ready for a vertex buffer.
  template <std::size_t N>
  using Wireframe = std::array<WireVertex, N * wire_vertex_count>;

  // A corner of a wire: a place on the wire's surface, and the
  // outward direction the surface faces there.
  struct Corner {
    Point at;
    Vec3 out;
  };

  // The eight corners of one wire: a box with a square cross-section
  // of half-width `radius`, swept from one end of the segment to the
  // other.
  [[nodiscard]] inline std::array<Corner, 8> wire_corners (const Segment& wire,
                                                           Length radius) {
    const Vec3 axis = direction (wire.to - wire.from);
    const Vec3 anchor = std::abs (scalar_product (axis, up)) > 0.9f ? east : up;
    const Vec3 u = vector_product (axis, anchor).unit ();
    const Vec3 v = vector_product (axis, u);
    const auto corner = [&] (const Point& end, Real su, Real sv) {
      const Vec3 across = u * su + v * sv;
      return Corner { end + across * radius,
                      across / std::numbers::sqrt2_v<Real> };
    };
    return {
      corner (wire.from, -1, -1), corner (wire.from, +1, -1),
      corner (wire.from, +1, +1), corner (wire.from, -1, +1),
      corner (wire.to, -1, -1),   corner (wire.to, +1, -1),
      corner (wire.to, +1, +1),   corner (wire.to, -1, +1),
    };
  }

  // The twelve triangles of a wire, as indices into its corners.
  inline constexpr std::array<unsigned, wire_vertex_count> wire_triangles {
    0, 1, 5, 0, 5, 4, 1, 2, 6, 1, 6, 5, 2, 3, 7, 2, 7, 6,
    3, 0, 4, 3, 4, 7, 0, 3, 2, 0, 2, 1, 4, 5, 6, 4, 6, 7,
  };

  // Thicken every segment of a skeleton into wire.
  template <std::size_t N>
  [[nodiscard]] Wireframe<N> wireframe (const std::array<Segment, N>& skeleton,
                                        Length radius) {
    const auto thicken = [radius] (const Segment& segment) {
      return tabulate<wire_vertex_count> (
        [corners = wire_corners (segment, radius)] (std::size_t index) {
          const Corner& corner = corners[wire_triangles[index]];
          return WireVertex { in_metres (corner.at), as_simd (corner.out) };
        });
    };

    Wireframe<N> mesh;
    std::ranges::copy (skeleton | std::views::transform (thicken) |
                         std::views::join,
                       mesh.begin ());
    return mesh;
  }
}
