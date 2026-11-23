#pragma once

#include <moppe/render/renderer.hh>
#include <moppe/terrain/torus.hh>

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <limits>
#include <span>
#include <vector>

namespace moppe::nhal::detail {
  // Index into the periodic forest records. Each selected block supplies
  // up to 64 contiguous records to one compute workgroup.
  class ForestGrid {
  public:
    using Block = std::array<std::uint32_t, 2>;
    const std::array<float, 2>& period () const {
      return m_period;
    }
    static float row_length (const Mat4& view, int r) {
      return std::sqrt (view.element (r) * view.element (r) +
                        view.element (4 + r) * view.element (4 + r) +
                        view.element (8 + r) * view.element (8 + r));
    }
    std::vector<std::uint32_t>
    build (std::span<const render::ForestInstance> instances,
           const Vec3& forest_period) {
      m_period = { forest_period[0], forest_period[2] };
      m_max_bound = m_max_crown = 0;
      m_low = std::numeric_limits<float>::max ();
      m_high = -m_low;
      m_tiles.clear ();
      if (instances.empty ())
        return {};
      const auto tile_side = [] (float extent) {
        return std::max (1, int (std::ceil (extent / 128.f)));
      };
      const int nx = tile_side (forest_period[0]);
      const int nz = tile_side (forest_period[2]);
      m_dimensions = { nx, nz };
      m_tiles.resize (std::size_t (nx) * nz);
      const auto tile_of = [&] (const render::ForestInstance& tree) {
        const Vec3 p = position_value (tree.root);
        const auto tile = [&] (int axis, int n) {
          if (n == 1)
            return 0;
          return std::min (
            n - 1,
            int (terrain::wrap_coordinate (p[axis], forest_period[axis]) /
                 forest_period[axis] * n));
        };
        return tile (0, nx) + nx * tile (2, nz);
      };
      for (const auto& tree : instances) {
        auto& tile = m_tiles[tile_of (tree)];
        ++tile.count;
        const float height = meters_value (tree.height);
        const float crown = meters_value (tree.crown_radius);
        const float centre = position_value (tree.root)[1] + 0.52f * height;
        m_low = std::min (m_low, centre);
        m_high = std::max (m_high, centre);
        tile.low = std::min (tile.low, centre);
        tile.high = std::max (tile.high, centre);
        tile.bound = std::max (tile.bound, 0.55f * height + 1.4f * crown);
        tile.crown = std::max (tile.crown, crown);
        m_max_bound = std::max (m_max_bound, tile.bound);
        m_max_crown = std::max (m_max_crown, crown);
      }
      std::vector<std::uint32_t> cursor;
      std::uint32_t first = 0;
      for (auto& tile : m_tiles) {
        cursor.push_back (first);
        tile.first = first;
        first += tile.count;
      }
      std::vector<std::uint32_t> order (instances.size ());
      for (std::uint32_t i = 0; i < instances.size (); ++i)
        order[cursor[tile_of (instances[i])]++] = i;
      return order;
    }

    std::vector<Block> visible_blocks (const Mat4& view,
                                       const Vec3& eye,
                                       float height,
                                       bool shadow,
                                       bool all_tiles = false) const {
      // Enumerate only tiles within the greatest possible pixel reach.
      // Orthographic shadow maps cover a finite box in all three axes.
      const auto clip = [&] (int r, const Vec3& p) {
        return view.element (r) * p[0] + view.element (4 + r) * p[1] +
               view.element (8 + r) * p[2] + view.element (12 + r);
      };
      if (m_tiles.empty ())
        return {};
      const float rx = row_length (view, 0), ry = row_length (view, 1),
                  rw = row_length (view, 3);
      const float rz = row_length (view, 2);
      const float radius = m_max_crown * ry * height / 4.f + m_max_bound + 1;
      std::array<float, 2> reach { radius, radius };
      if (shadow) {
        const float depth_bound = (1 + std::abs (clip (0, eye))) / rx +
                                  (1 + std::abs (clip (1, eye))) / ry +
                                  (1 + std::abs (clip (2, eye))) / rz +
                                  m_max_bound + 1;
        reach.fill (depth_bound);
        // Intersect the orthographic frustum with the forest's height range.
        // The inverse of its two horizontal rows bounds x and z separately.
        // Near a horizontal sun the finite depth bound still applies.
        const float a = view.element (0), b = view.element (8);
        const float c = view.element (1), d = view.element (9);
        const float det = std::abs (a * d - b * c);
        if (det > 1e-5f * rx * ry) {
          const float dy =
            std::max (std::abs (m_low - eye[1]), std::abs (m_high - eye[1]));
          const float u = 1 + std::abs (clip (0, eye)) +
                          std::abs (view.element (4)) * dy + rx * m_max_bound;
          const float v = 1 + std::abs (clip (1, eye)) +
                          std::abs (view.element (5)) * dy + ry * m_max_bound;
          reach[0] = std::min (depth_bound,
                               (std::abs (d) * u + std::abs (b) * v) / det + 1);
          reach[1] = std::min (depth_bound,
                               (std::abs (c) * u + std::abs (a) * v) / det + 1);
        }
      }
      const bool all = all_tiles || m_period[0] <= 0 || m_period[1] <= 0;
      std::array<int, 2> start {}, span = m_dimensions;
      float step[2] {};
      for (int axis = 0; axis < 2; ++axis) {
        const float period = m_period[axis];
        step[axis] = period / span[axis];
        if (!all && 2 * reach[axis] < period) {
          const float at = terrain::wrap_coordinate (eye[2 * axis], period);
          start[axis] = int (std::floor ((at - reach[axis]) / step[axis]));
          span[axis] =
            std::min (span[axis],
                      int (std::floor ((at + reach[axis]) / step[axis])) -
                        start[axis] + 1);
        }
      }
      std::vector<Block> blocks;
      for (int j = 0; j < span[1]; ++j) {
        const int z = terrain::wrap_index (start[1] + j, m_dimensions[1]);
        for (int i = 0; i < span[0]; ++i) {
          const int x = terrain::wrap_index (start[0] + i, m_dimensions[0]);
          const auto& tile = m_tiles[x + z * m_dimensions[0]];
          if (!tile.count)
            continue;
          const Vec3 centre (
            terrain::nearest_image ((x + .5f) * step[0], eye[0], m_period[0]),
            (tile.low + tile.high) * .5f,
            terrain::nearest_image ((z + .5f) * step[1], eye[2], m_period[1]));
          if (!all) {
            const float radius =
              1.f + tile.bound +
              .5f * std::hypot (step[0], step[1], tile.high - tile.low);
            const auto visible = [&] (const Vec3& p) {
              const float w = clip (3, p);
              if (w + radius * (1 + rw) < 0 ||
                  std::abs (clip (0, p)) > w + radius * (rx + rw) ||
                  std::abs (clip (1, p)) > w + radius * (ry + rw))
                return false;
              if (shadow)
                return clip (2, p) >= -rz * radius &&
                       clip (2, p) <= w + (rw + rz) * radius;
              return length (p - eye) <=
                     radius + tile.crown * ry * height / 4.f;
            };
            // At an antipode different trees in one tile can choose
            // different nearest images. Test each possible image once.
            if (!visible (centre)) {
              Vec3 alternate = centre;
              for (int d = 0; d < 2; ++d) {
                const int axis = 2 * d;
                const float delta = centre[axis] - eye[axis];
                if (2 * std::abs (delta) + step[d] >= m_period[d])
                  alternate[axis] -= std::copysign (m_period[d], delta);
              }
              const bool x = alternate[0] != centre[0];
              const bool z = alternate[2] != centre[2];
              if (!((x || z) && visible (alternate)) &&
                  !(x && z &&
                    (visible (Vec3 (centre[0], centre[1], alternate[2])) ||
                     visible (Vec3 (alternate[0], centre[1], centre[2])))))
                continue;
            }
          }
          for (std::uint32_t n = 0; n < tile.count; n += 64)
            blocks.push_back (
              { tile.first + n, std::min (64u, tile.count - n) });
        }
      }
      return blocks;
    }

  private:
    struct Tile {
      std::uint32_t first = 0, count = 0;
      float low = std::numeric_limits<float>::max (), high = -low;
      float bound = 0, crown = 0;
    };
    std::array<float, 2> m_period {};
    std::array<int, 2> m_dimensions {};
    std::vector<Tile> m_tiles;
    float m_max_crown = 0, m_max_bound = 0;
    float m_low = 0, m_high = 0;
  };
}
