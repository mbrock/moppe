#include "atelier/atelier.hh"

#include <algorithm>
#include <array>
#include <cmath>
#include <numbers>
#include <ranges>
#include <stdexcept>
#include <utility>

namespace atelier {
  using namespace mp_units;
  using namespace mp_units::si::unit_symbols;
  using mp_units::angular::sin;
  using mp_units::angular::unit_symbols::deg;
  using mp_units::angular::unit_symbols::rev;

  namespace {
    // The array whose i-th element is f(i): tabulation as one
    // expression, in the same spirit the mp-units cartesian_vector
    // builds itself from a component function.
    template <std::size_t N>
    [[nodiscard]] constexpr auto tabulate (auto f) {
      return [&]<std::size_t... I> (std::index_sequence<I...>) {
        return std::array { f (I)... };
      }(std::make_index_sequence<N> {});
    }

    // --- One coin ---------------------------------------------------

    constexpr Length coin_radius = 1.0f * m;
    constexpr Length coin_half_depth = 0.16f * m;
    constexpr Length wire_radius = 0.025f * m;

    // A straight run of wire between two places.
    struct Segment {
      Point from;
      Point to;
    };

    // A rim vertex: `side` counts around the hexagon, `face` is +1 on
    // the upper face and -1 on the lower.
    Point rim (std::size_t side, Real face) {
      const Angle around = (Real (side) / coin_sides) * rev;
      return scene + radial (around) * coin_radius +
             up * (face * coin_half_depth);
    }

    // Each side contributes three edges: a stretch of upper rim, a
    // stretch of lower rim, and the pillar between them.
    std::array<Segment, coin_edge_count> coin_edges () {
      return tabulate<coin_edge_count> ([] (std::size_t index) {
        const std::size_t side = index / 3;
        const std::size_t next = (side + 1) % coin_sides;
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

    // A corner of a wire: a place on the wire's surface, and the
    // outward direction the surface faces there.
    struct Corner {
      Point at;
      Vec3 out;
    };

    // The eight corners of a wire: a box with a square cross-section
    // of half-width `radius`, swept from one end of the segment to
    // the other.
    std::array<Corner, 8> wire_corners (const Segment& wire, Length radius) {
      const Vec3 axis = direction (wire.to - wire.from);
      const Vec3 anchor =
        std::abs (scalar_product (axis, up)) > 0.9f ? east : up;
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
    constexpr std::array<unsigned, wire_vertex_count> wire_triangles {
      0, 1, 5, 0, 5, 4, 1, 2, 6, 1, 6, 5, 2, 3, 7, 2, 7, 6,
      3, 0, 4, 3, 4, 7, 0, 3, 2, 0, 2, 1, 4, 5, 6, 4, 6, 7,
    };

    // --- The carpet of coins ------------------------------------------

    constexpr Length carpet_pitch = 2.6f * m;
    constexpr Length hover_height = 1.1f * m;
    constexpr Length hover_swell = 0.22f * m;
    constexpr AngularRate spin_rate = 0.04f * rev / s;
    constexpr AngularRate bob_rate = 0.09f * rev / s;

    // The golden angle: successive coins get phases that never repeat
    // and never cluster.
    constexpr Angle golden = 0.381966f * rev;

    // A lattice cell in axial hex coordinates.
    struct Cell {
      int q, r;

      [[nodiscard]] constexpr int ring () const {
        const int s = -q - r;
        return std::max ({ q < 0 ? -q : q, r < 0 ? -r : r, s < 0 ? -s : s });
      }
    };

    consteval std::array<Cell, coin_count> carpet_cells () {
      std::array<Cell, coin_count> cells {};
      std::size_t count = 0;
      for (int q = -carpet_rings; q <= carpet_rings; ++q)
        for (int r = -carpet_rings; r <= carpet_rings; ++r)
          if (Cell { q, r }.ring () <= carpet_rings)
            cells[count++] = { q, r };
      return cells;
    }

    Point cell_centre (Cell cell) {
      const Real across = Real (cell.q) + Real (cell.r) / 2;
      const Real away = Real (cell.r) * (std::numbers::sqrt3_v<Real> / 2);
      return scene + (east * across + north * away) * carpet_pitch;
    }

    // Where a coin sits and how it carries itself at this instant.
    struct Placement {
      Point centre;
      Angle spin;     // about the coin's own axis
      Angle lean;     // away from the vertical
      Vec3 lean_axis; // the horizontal axis it leans about
      Real patina;    // 0 at the golden centre, 1 at the coppery edge
    };

    Placement place_coin (std::size_t index, Cell cell, Duration t) {
      const Angle phase = Real (index) * golden;
      // The carpet gently domes: inner rings ride higher, and every
      // coin breathes up and down on its own golden-angle phase.
      const Length hover = hover_height +
                           0.3f * m * Real (carpet_rings - cell.ring ()) +
                           hover_swell * sin (bob_rate * t + phase);
      return {
        .centre = cell_centre (cell) + up * hover,
        .spin = spin_rate * t + phase,
        .lean = 6.0f * deg + 3.0f * deg * sin (3.0f * phase),
        .lean_axis = radial (phase),
        .patina = Real (cell.ring ()) / carpet_rings,
      };
    }

    // The glaze a coin is dipped in: gold at the heart of the carpet,
    // weathering toward copper at its edge.
    simd_float4 glaze (Real patina) {
      constexpr simd_float3 gold { 1.0f, 0.78f, 0.26f };
      constexpr simd_float3 copper { 0.84f, 0.42f, 0.28f };
      return simd_make_float4 (simd_mix (gold, copper, simd_float3 (patina)),
                               1.0f);
    }

    // --- Matrices: the scene as the GPU sees it ---------------------

    using Matrix = simd_float4x4;

    Matrix translation (const Point& to) {
      Matrix matrix = matrix_identity_float4x4;
      matrix.columns[3] = simd_make_float4 (in_metres (to), 1.0f);
      return matrix;
    }

    Matrix rotation (Angle by, const Vec3& about) {
      return simd_matrix4x4 (
        simd_quaternion (in_radians (by), as_simd (about)));
    }

    Matrix place_in_world (const Placement& coin) {
      return simd_mul (translation (coin.centre),
                       simd_mul (rotation (coin.lean, coin.lean_axis),
                                 rotation (coin.spin, up)));
    }

    Matrix perspective (Angle vertical_fov,
                        Real aspect_ratio,
                        Length near_plane,
                        Length far_plane) {
      const Real y = Real (1.0f / angular::tan (vertical_fov / 2));
      const Real x = y / aspect_ratio;
      const Real z = Real (far_plane / (near_plane - far_plane));
      return { simd_make_float4 (x, 0, 0, 0),
               simd_make_float4 (0, y, 0, 0),
               simd_make_float4 (0, 0, z, -1),
               simd_make_float4 (0, 0, in_metres (near_plane) * z, 0) };
    }

    Matrix look_at (const Point& eye, const Point& focus) {
      const simd_float3 from = in_metres (eye);
      const simd_float3 back = simd_normalize (from - in_metres (focus));
      const simd_float3 right =
        simd_normalize (simd_cross (as_simd (up), back));
      const simd_float3 above = simd_cross (back, right);
      return { simd_make_float4 (right.x, above.x, back.x, 0),
               simd_make_float4 (right.y, above.y, back.y, 0),
               simd_make_float4 (right.z, above.z, back.z, 0),
               simd_make_float4 (-simd_dot (right, from),
                                 -simd_dot (above, from),
                                 -simd_dot (back, from),
                                 1) };
    }

    // --- The camera --------------------------------------------------
    //
    // A camera riding a slow carousel around the carpet, always facing
    // a point just above its centre.

    struct Carousel {
      Length orbit_radius = 11.0f * m;
      Length ride_height = 6.5f * m;
      AngularRate rate = 0.01f * rev / s;
      Angle field_of_view = 42.0f * deg;
      Length near_plane = 0.05f * m;
      Length far_plane = 80.0f * m;

      [[nodiscard]] Point eye (Duration t) const {
        return scene + radial (rate * t) * orbit_radius + up * ride_height;
      }

      [[nodiscard]] Uniforms uniforms (Duration t, Real aspect_ratio) const {
        const Point from = eye (t);
        const Point focus = scene + up * (hover_height / 2);
        return {
          .world_to_clip = simd_mul (
            perspective (field_of_view, aspect_ratio, near_plane, far_plane),
            look_at (from, focus)),
          .eye = simd_make_float4 (in_metres (from), 1),
        };
      }
    };
  }

  bool Viewport::is_empty () const {
    return width == 0 || height == 0;
  }

  Real Viewport::aspect_ratio () const {
    if (is_empty ())
      throw std::invalid_argument ("An empty viewport has no aspect ratio");
    return Real (width) / Real (height);
  }

  CoinMesh coin_wireframe () {
    const auto thicken = [] (const Segment& edge) {
      return tabulate<wire_vertex_count> (
        [corners = wire_corners (edge, wire_radius)] (std::size_t index) {
          const Corner& corner = corners[wire_triangles[index]];
          return WireVertex { in_metres (corner.at), as_simd (corner.out) };
        });
    };

    CoinMesh mesh;
    std::ranges::copy (coin_edges () | std::views::transform (thicken) |
                         std::views::join,
                       mesh.begin ());
    return mesh;
  }

  Frame compose_frame (Duration elapsed, Viewport viewport) {
    static constexpr std::array<Cell, coin_count> cells = carpet_cells ();
    const Carousel camera;
    const auto mint = [&] (std::size_t index) {
      const Placement coin = place_coin (index, cells[index], elapsed);
      return CoinInstance { place_in_world (coin), glaze (coin.patina) };
    };
    return {
      .uniforms = camera.uniforms (elapsed, viewport.aspect_ratio ()),
      .coins = tabulate<coin_count> (mint),
    };
  }
}
