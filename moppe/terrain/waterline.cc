#include <moppe/terrain/waterline.hh>

#include <algorithm>
#include <cstdint>
#include <limits>
#include <stdexcept>
#include <unordered_map>

namespace moppe::terrain {
  namespace {
    // Every lattice edge crosses the waterline at most once (the
    // bilinear difference is linear along an edge), so a crossing is
    // identified by its edge: 2 * node + 0 horizontal (toward +x),
    // 2 * node + 1 vertical (toward +y).
    struct WaterlineCrossing {
      float x;
      float y;
      std::uint64_t link[2] = { no_link, no_link };
      std::uint8_t degree = 0;
      bool visited = false;

      static constexpr std::uint64_t no_link =
        std::numeric_limits<std::uint64_t>::max ();
    };
  }

  Waterline extract_waterline (const TerrainView& terrain,
                               const ScalarRaster& surface,
                               const LakeCensus& census,
                               float wet_epsilon) {
    if (!std::isfinite (wet_epsilon) || wet_epsilon < 0.0f)
      throw std::invalid_argument ("waterline epsilon must be non-negative");
    const TerrainGrid& grid = terrain.grid ();
    const std::size_t width = grid.unique_width ();
    const std::size_t height = grid.unique_height ();
    const std::size_t count = width * height;
    if (surface.values ().size () != count)
      throw std::invalid_argument ("water surface does not match terrain");
    if (census.body.size () != count)
      throw std::invalid_argument ("lake census does not match terrain");
    const bool periodic = grid.topology == Topology::Torus;
    const std::span<const float> level = surface.values ();

    // Signed wetness at each lattice node; positive is wet.
    const auto field = [&] (std::size_t x, std::size_t y) {
      const std::size_t node = y * width + x;
      return level[node] - terrain.at (x, y) - wet_epsilon;
    };

    const std::size_t cells_x = periodic ? width : width - 1;
    const std::size_t cells_y = periodic ? height : height - 1;

    std::unordered_map<std::uint64_t, WaterlineCrossing> crossings;
    std::vector<std::pair<std::uint64_t, std::uint64_t>> segments;
    std::vector<WaterBodyId> segment_body;

    // The crossing on an edge, computed identically from both adjacent
    // cells: linear interpolation of the sign change.
    const auto crossing_key =
      [&] (std::size_t x, std::size_t y, bool vertical) -> std::uint64_t {
      return 2 * static_cast<std::uint64_t> (y * width + x) +
             (vertical ? 1 : 0);
    };
    const auto ensure_crossing =
      [&] (std::size_t x, std::size_t y, bool vertical, float fa, float fb) {
        const std::uint64_t key = crossing_key (x, y, vertical);
        if (crossings.contains (key))
          return key;
        const float s = fa / (fa - fb);
        crossings.emplace (key,
                           WaterlineCrossing {
                             .x = static_cast<float> (x) + (vertical ? 0 : s),
                             .y = static_cast<float> (y) + (vertical ? s : 0),
                           });
        return key;
      };

    for (std::size_t y = 0; y < cells_y; ++y) {
      const std::size_t y1 = periodic ? (y + 1) % height : y + 1;
      for (std::size_t x = 0; x < cells_x; ++x) {
        const std::size_t x1 = periodic ? (x + 1) % width : x + 1;
        const float f00 = field (x, y);
        const float f10 = field (x1, y);
        const float f01 = field (x, y1);
        const float f11 = field (x1, y1);
        const unsigned mask = (f00 > 0 ? 1u : 0u) | (f10 > 0 ? 2u : 0u) |
                              (f01 > 0 ? 4u : 0u) | (f11 > 0 ? 8u : 0u);
        if (mask == 0u || mask == 15u)
          continue;

        // Edge crossings of this cell, in a fixed order: bottom,
        // right, top, left.  A crossing exists where the edge's corner
        // signs differ.
        const std::uint64_t bottom = ((mask & 1u) != 0) != ((mask & 2u) != 0)
                                       ? ensure_crossing (x, y, false, f00, f10)
                                       : WaterlineCrossing::no_link;
        const std::uint64_t right = ((mask & 2u) != 0) != ((mask & 8u) != 0)
                                      ? ensure_crossing (x1, y, true, f10, f11)
                                      : WaterlineCrossing::no_link;
        const std::uint64_t top = ((mask & 4u) != 0) != ((mask & 8u) != 0)
                                    ? ensure_crossing (x, y1, false, f01, f11)
                                    : WaterlineCrossing::no_link;
        const std::uint64_t left = ((mask & 1u) != 0) != ((mask & 4u) != 0)
                                     ? ensure_crossing (x, y, true, f00, f01)
                                     : WaterlineCrossing::no_link;

        // The wet body beside this stretch: the smallest wet corner.
        WaterBodyId body = no_water_body;
        const std::size_t corner_nodes[4] = {
          y * width + x, y * width + x1, y1 * width + x, y1 * width + x1
        };
        for (int corner = 0; corner < 4; ++corner)
          if ((mask & (1u << corner)) != 0 &&
              census.body[corner_nodes[corner]] != no_water_body) {
            body = census.body[corner_nodes[corner]];
            break;
          }

        const auto emit = [&] (std::uint64_t a, std::uint64_t b) {
          segments.emplace_back (a, b);
          segment_body.push_back (body);
        };

        switch (mask) {
        case 1u:
        case 14u:
          emit (left, bottom);
          break;
        case 2u:
        case 13u:
          emit (bottom, right);
          break;
        case 4u:
        case 11u:
          emit (left, top);
          break;
        case 8u:
        case 7u:
          emit (top, right);
          break;
        case 3u:
        case 12u:
          emit (left, right);
          break;
        case 5u:
        case 10u:
          emit (bottom, top);
          break;
        case 6u:
        case 9u: {
          // Saddle: resolve by the bilinear center, matching the sign
          // convention of the corners it joins.
          const float center = 0.25f * (f00 + f10 + f01 + f11);
          const bool center_wet = center > 0.0f;
          const bool corners_00_11_wet = mask == 9u;
          // The boundary isolates whichever corner pair disagrees with
          // the center: dry corners when the middle is wet, wet
          // corners when it is dry.
          if (center_wet != corners_00_11_wet) {
            emit (left, bottom);
            emit (top, right);
          } else {
            emit (left, top);
            emit (bottom, right);
          }
          break;
        }
        default:
          break;
        }
      }
    }

    // Link crossings through their segments; every crossing meets at
    // most two segments (one per adjacent cell side).
    std::vector<WaterBodyId> crossing_body (0);
    std::unordered_map<std::uint64_t, WaterBodyId> body_of;
    for (std::size_t i = 0; i < segments.size (); ++i) {
      const auto [a, b] = segments[i];
      WaterlineCrossing& ca = crossings.at (a);
      WaterlineCrossing& cb = crossings.at (b);
      if (ca.degree < 2)
        ca.link[ca.degree++] = b;
      if (cb.degree < 2)
        cb.link[cb.degree++] = a;
      body_of.try_emplace (a, segment_body[i]);
      body_of.try_emplace (b, segment_body[i]);
    }

    // Deterministic chains: sorted starting keys, open chains (degree
    // one, bounded maps only) claimed before loops.
    std::vector<std::uint64_t> keys;
    keys.reserve (crossings.size ());
    for (const auto& [key, crossing] : crossings)
      keys.push_back (key);
    std::sort (keys.begin (), keys.end ());

    Waterline waterline { .source_grid = grid };
    const float spacing_x = grid.spacing_x_m ();
    const float spacing_y = grid.spacing_y_m ();

    const auto walk = [&] (std::uint64_t start, bool closed) {
      WaterlineContour contour { .closed = closed };
      contour.body =
        body_of.contains (start) ? body_of.at (start) : no_water_body;
      std::uint64_t previous = WaterlineCrossing::no_link;
      std::uint64_t current = start;
      while (current != WaterlineCrossing::no_link) {
        WaterlineCrossing& node = crossings.at (current);
        if (node.visited)
          break;
        node.visited = true;
        contour.points.push_back (node.x * spacing_x);
        contour.points.push_back (node.y * spacing_y);
        const std::uint64_t next =
          node.link[0] != previous ? node.link[0] : node.link[1];
        previous = current;
        current = next;
        if (closed && current == start)
          break;
      }
      if (contour.size () >= 2)
        waterline.contours.push_back (std::move (contour));
    };

    for (const std::uint64_t key : keys)
      if (!crossings.at (key).visited && crossings.at (key).degree == 1)
        walk (key, false);
    for (const std::uint64_t key : keys)
      if (!crossings.at (key).visited)
        walk (key, true);

    return waterline;
  }
}
