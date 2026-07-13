#include <moppe/terrain/waterline.hh>

#include <algorithm>
#include <cmath>
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

    // Signed wetness at each lattice node; positive is wet.  The
    // painted sheet holds ground height in dry cells, so this field is
    // clamped on the dry side: it decides topology (which corners are
    // wet), while crossing positions come from extending the wet
    // side's level across the edge below.
    const auto field = [&] (std::size_t x, std::size_t y) {
      const std::size_t node = y * width + x;
      return level[node] - terrain.at (x, y) - wet_epsilon;
    };
    const auto ground = [&] (std::size_t x, std::size_t y) {
      return terrain.at (x, y);
    };
    const auto sheet = [&] (std::size_t x, std::size_t y) {
      return level[y * width + x];
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
    // The crossing along a mixed edge: water is locally flat, so the
    // wet corner's level extends across the edge and meets the ground
    // slope where the shoreline actually sits.  A clamped dry-side
    // sheet (ground height in dry cells) carries no gradient of its
    // own, so interpolating the raw difference would pin every
    // crossing onto the dry lattice node -- the staircase this
    // extraction exists to remove.
    const auto ensure_crossing = [&] (std::size_t ax,
                                      std::size_t ay,
                                      std::size_t bx,
                                      std::size_t by,
                                      bool vertical,
                                      float fa,
                                      float fb) {
      const std::uint64_t key = crossing_key (ax, ay, vertical);
      if (crossings.contains (key))
        return key;
      const bool a_wet = fa > 0.0f;
      const float level_wet = a_wet ? sheet (ax, ay) : sheet (bx, by);
      const float dry_z = a_wet ? ground (bx, by) : ground (ax, ay);
      // Extended field at the dry end; a dry corner sitting below the
      // wet level (a steep undercut) keeps a token negative value so
      // the crossing stays on the edge.
      const float dry_extended =
        std::min (level_wet - dry_z - wet_epsilon, -1e-9f);
      const float fa_extended = a_wet ? fa : dry_extended;
      const float fb_extended = a_wet ? dry_extended : fb;
      const float s = fa_extended / (fa_extended - fb_extended);
      crossings.emplace (key,
                         WaterlineCrossing {
                           .x = static_cast<float> (ax) + (vertical ? 0 : s),
                           .y = static_cast<float> (ay) + (vertical ? s : 0),
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
        const std::uint64_t bottom =
          ((mask & 1u) != 0) != ((mask & 2u) != 0)
            ? ensure_crossing (x, y, x1, y, false, f00, f10)
            : WaterlineCrossing::no_link;
        const std::uint64_t right =
          ((mask & 2u) != 0) != ((mask & 8u) != 0)
            ? ensure_crossing (x1, y, x1, y1, true, f10, f11)
            : WaterlineCrossing::no_link;
        const std::uint64_t top =
          ((mask & 4u) != 0) != ((mask & 8u) != 0)
            ? ensure_crossing (x, y1, x1, y1, false, f01, f11)
            : WaterlineCrossing::no_link;
        const std::uint64_t left =
          ((mask & 1u) != 0) != ((mask & 4u) != 0)
            ? ensure_crossing (x, y, x, y1, true, f00, f01)
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

  ScalarRaster waterline_proximity (const Waterline& waterline, float band_m) {
    if (!std::isfinite (band_m) || band_m <= 0.0f)
      throw std::invalid_argument ("waterline band must be positive");
    const TerrainGrid& grid = waterline.source_grid;
    const std::size_t width = grid.unique_width ();
    const std::size_t height = grid.unique_height ();
    const std::size_t count = width * height;
    const bool periodic = grid.topology == Topology::Torus;
    const float spacing_x = grid.spacing_x_m ();
    const float spacing_y = grid.spacing_y_m ();
    const float world_x = spacing_x * static_cast<float> (width);
    const float world_y = spacing_y * static_cast<float> (height);

    std::vector<float> distance (count, band_m);

    // Bucket segments by lattice cell so each node only inspects its
    // neighborhood; a marching-squares segment spans at most two cells.
    struct ShoreSegment {
      float ax, ay, bx, by;
    };
    std::vector<ShoreSegment> shore;
    std::unordered_map<std::uint64_t, std::vector<std::uint32_t>> buckets;
    const auto bucket_key = [&] (int x, int y) -> std::uint64_t {
      if (periodic) {
        x = (x % static_cast<int> (width) + static_cast<int> (width)) %
            static_cast<int> (width);
        y = (y % static_cast<int> (height) + static_cast<int> (height)) %
            static_cast<int> (height);
      }
      return static_cast<std::uint64_t> (y) * width +
             static_cast<std::uint64_t> (x);
    };
    for (const WaterlineContour& contour : waterline.contours) {
      const std::size_t points = contour.size ();
      const std::size_t stretches = contour.closed ? points : points - 1;
      for (std::size_t i = 0; i < stretches; ++i) {
        const std::size_t j = (i + 1) % points;
        ShoreSegment segment { contour.points[2 * i],
                               contour.points[2 * i + 1],
                               contour.points[2 * j],
                               contour.points[2 * j + 1] };
        // A closed contour's return stretch may cross the torus seam;
        // minimum-image its far end next to the near one.
        if (periodic) {
          if (segment.bx - segment.ax > 0.5f * world_x)
            segment.bx -= world_x;
          if (segment.ax - segment.bx > 0.5f * world_x)
            segment.bx += world_x;
          if (segment.by - segment.ay > 0.5f * world_y)
            segment.by -= world_y;
          if (segment.ay - segment.by > 0.5f * world_y)
            segment.by += world_y;
        }
        const std::uint32_t index = static_cast<std::uint32_t> (shore.size ());
        shore.push_back (segment);
        buckets[bucket_key (static_cast<int> (std::floor (
                              0.5f * (segment.ax + segment.bx) / spacing_x)),
                            static_cast<int> (std::floor (
                              0.5f * (segment.ay + segment.by) / spacing_y)))]
          .push_back (index);
      }
    }
    if (shore.empty ())
      return ScalarRaster ({ .width = width, .height = height },
                           std::move (distance));

    const int reach_x = static_cast<int> (std::ceil (band_m / spacing_x)) + 1;
    const int reach_y = static_cast<int> (std::ceil (band_m / spacing_y)) + 1;

    const auto point_segment_distance = [&] (float px,
                                             float py,
                                             const ShoreSegment& segment) {
      float ax = segment.ax, ay = segment.ay;
      float bx = segment.bx, by = segment.by;
      if (periodic) {
        // Bring the segment into the node's minimum image.
        const float mid_x = 0.5f * (ax + bx);
        const float mid_y = 0.5f * (ay + by);
        const float shift_x = std::round ((px - mid_x) / world_x) * world_x;
        const float shift_y = std::round ((py - mid_y) / world_y) * world_y;
        ax += shift_x;
        bx += shift_x;
        ay += shift_y;
        by += shift_y;
      }
      const float dx = bx - ax, dy = by - ay;
      const float length2 = dx * dx + dy * dy;
      const float t =
        length2 > 0.0f
          ? std::clamp (((px - ax) * dx + (py - ay) * dy) / length2, 0.0f, 1.0f)
          : 0.0f;
      const float cx = ax + t * dx - px;
      const float cy = ay + t * dy - py;
      return std::sqrt (cx * cx + cy * cy);
    };

    // Exact distances for every node within the band of a bucketed
    // cell; everything farther keeps the clamp.
    for (const auto& [key, segment_indices] : buckets) {
      const int bx = static_cast<int> (key % width);
      const int by = static_cast<int> (key / width);
      for (int dy = -reach_y; dy <= reach_y; ++dy)
        for (int dx = -reach_x; dx <= reach_x; ++dx) {
          int nx = bx + dx;
          int ny = by + dy;
          if (periodic) {
            nx = (nx % static_cast<int> (width) + static_cast<int> (width)) %
                 static_cast<int> (width);
            ny = (ny % static_cast<int> (height) + static_cast<int> (height)) %
                 static_cast<int> (height);
          } else if (nx < 0 || ny < 0 || nx >= static_cast<int> (width) ||
                     ny >= static_cast<int> (height))
            continue;
          const std::size_t node = static_cast<std::size_t> (ny) * width + nx;
          const float px = static_cast<float> (nx) * spacing_x;
          const float py = static_cast<float> (ny) * spacing_y;
          float& best = distance[node];
          for (const std::uint32_t segment : segment_indices)
            best =
              std::min (best, point_segment_distance (px, py, shore[segment]));
        }
    }

    return ScalarRaster ({ .width = width, .height = height },
                         std::move (distance));
  }
}
