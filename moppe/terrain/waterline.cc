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
      plan_position_t at;
      std::uint64_t link[2] = { no_link, no_link };
      std::uint8_t degree = 0;
      bool visited = false;

      static constexpr std::uint64_t no_link =
        std::numeric_limits<std::uint64_t>::max ();
    };
  }

  Waterline
  detail::extract_waterline (const TerrainDomain& grid,
                             std::span<const SurfaceElevation> elevations,
                             std::span<const SurfaceElevation> surface,
                             const WaterBodyMembership& water_bodies,
                             ElevationChange wet_epsilon) {
    if (!isfinite (wet_epsilon) || wet_epsilon < ElevationChange::zero ())
      throw std::invalid_argument ("waterline epsilon must be non-negative");
    const std::size_t width = grid.width ();
    const std::size_t height = grid.height ();
    const std::size_t count = width * height;
    if (surface.size () != count)
      throw std::invalid_argument ("water surface does not match terrain");
    if (water_bodies.size () != count)
      throw std::invalid_argument ("lake census does not match terrain");
    // Signed wetness at each lattice node; positive is wet.  The
    // painted sheet holds ground height in dry cells, so this field is
    // clamped on the dry side: it decides topology (which corners are
    // wet), while crossing positions come from extending the wet
    // side's level across the edge below.
    const auto ground = [&] (std::size_t x, std::size_t y) {
      return elevations[grid.offset ({ .column = x, .row = y })];
    };
    const auto sheet = [&] (std::size_t x, std::size_t y) {
      return surface[y * width + x];
    };
    const auto field = [&] (std::size_t x, std::size_t y) -> ElevationChange {
      return sheet (x, y) - ground (x, y) - wet_epsilon;
    };
    const ElevationChange dry = ElevationChange::zero ();
    const meters_t spacing_x = grid.spacing_x ();
    const meters_t spacing_y = grid.spacing_z ();

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
                                      ElevationChange fa,
                                      ElevationChange fb) {
      const std::uint64_t key = crossing_key (ax, ay, vertical);
      if (crossings.contains (key))
        return key;
      const bool a_wet = fa > dry;
      const SurfaceElevation level_wet =
        a_wet ? sheet (ax, ay) : sheet (bx, by);
      const SurfaceElevation dry_z = a_wet ? ground (bx, by) : ground (ax, ay);
      // Extended field at the dry end; a dry corner sitting below the
      // wet level (a steep undercut) keeps a token negative value so
      // the crossing stays on the edge.
      const ElevationChange dry_extended = std::min (
        level_wet - dry_z - wet_epsilon, -1e-9f * surface_elevation[u::m]);
      const ElevationChange fa_extended = a_wet ? fa : dry_extended;
      const ElevationChange fb_extended = a_wet ? dry_extended : fb;
      // How far along the edge from a to b.
      const proportion_t s =
        quantity_cast<proportion> (fa_extended / (fa_extended - fb_extended));
      const proportion_t none = 0.0f * proportion[one];
      crossings.emplace (
        key,
        WaterlineCrossing {
          .at = plan_position (
            meters_t ((static_cast<float> (ax) * one + (vertical ? none : s)) *
                      spacing_x),
            meters_t ((static_cast<float> (ay) * one + (vertical ? s : none)) *
                      spacing_y)),
        });
      return key;
    };

    for (std::size_t y = 0; y < height; ++y) {
      const std::size_t y1 = (y + 1) % height;
      for (std::size_t x = 0; x < width; ++x) {
        const std::size_t x1 = (x + 1) % width;
        const ElevationChange f00 = field (x, y);
        const ElevationChange f10 = field (x1, y);
        const ElevationChange f01 = field (x, y1);
        const ElevationChange f11 = field (x1, y1);
        const unsigned mask = (f00 > dry ? 1u : 0u) | (f10 > dry ? 2u : 0u) |
                              (f01 > dry ? 4u : 0u) | (f11 > dry ? 8u : 0u);
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
              water_bodies.body_at (CellIndex { static_cast<std::uint32_t> (
                corner_nodes[corner]) }) != no_water_body) {
            body = water_bodies.body_at (
              CellIndex { static_cast<std::uint32_t> (corner_nodes[corner]) });
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
          const ElevationChange center = 0.25f * (f00 + f10 + f01 + f11);
          const bool center_wet = center > dry;
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

    // Deterministic chains: sorted starting keys, open chains claimed before
    // loops.
    std::vector<std::uint64_t> keys;
    keys.reserve (crossings.size ());
    for (const auto& [key, crossing] : crossings)
      keys.push_back (key);
    std::sort (keys.begin (), keys.end ());

    Waterline waterline { .domain = grid };

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
        contour.points.push_back (node.at);
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

  WaterlineProximity waterline_proximity (const Waterline& waterline,
                                          meters_t band) {
    if (!isfinite (band) || band <= 0.0f * u::m)
      throw std::invalid_argument ("waterline band must be positive");
    const TerrainDomain& grid = waterline.domain;
    const std::size_t width = grid.width ();
    const std::size_t height = grid.height ();
    const std::size_t count = width * height;
    const meters_t spacing_x = grid.spacing_x ();
    const meters_t spacing_y = grid.spacing_z ();
    const meters_t world_x = spacing_x * static_cast<float> (width);
    const meters_t world_y = spacing_y * static_cast<float> (height);

    std::vector<WaterlineDistance> distance (count, WaterlineDistance (band));

    // Bucket segments by lattice cell so each node only inspects its
    // neighborhood; a marching-squares segment spans at most two cells.
    // A stretch's ends, x then z, each on the map's own lanes.
    struct ShoreSegment {
      meters_t ax, ay, bx, by;
    };
    std::vector<ShoreSegment> shore;
    std::unordered_map<std::uint64_t, std::vector<std::uint32_t>> buckets;
    const auto bucket_key = [&] (int x, int y) -> std::uint64_t {
      x = wrap_index (x, static_cast<int> (width));
      y = wrap_index (y, static_cast<int> (height));
      return static_cast<std::uint64_t> (y) * width +
             static_cast<std::uint64_t> (x);
    };
    for (const WaterlineContour& contour : waterline.contours) {
      const std::size_t points = contour.size ();
      const std::size_t stretches = contour.closed ? points : points - 1;
      for (std::size_t i = 0; i < stretches; ++i) {
        const std::size_t j = (i + 1) % points;
        ShoreSegment segment { x_of (contour.points[i]),
                               z_of (contour.points[i]),
                               x_of (contour.points[j]),
                               z_of (contour.points[j]) };
        // A closed contour's return stretch may cross the torus seam;
        // minimum-image its far end next to the near one.
        if (segment.bx - segment.ax > 0.5f * world_x)
          segment.bx -= world_x;
        if (segment.ax - segment.bx > 0.5f * world_x)
          segment.bx += world_x;
        if (segment.by - segment.ay > 0.5f * world_y)
          segment.by -= world_y;
        if (segment.ay - segment.by > 0.5f * world_y)
          segment.by += world_y;
        const std::uint32_t index = static_cast<std::uint32_t> (shore.size ());
        shore.push_back (segment);
        buckets[bucket_key (static_cast<int> (std::floor (
                              (0.5f * (segment.ax + segment.bx) / spacing_x)
                                .numerical_value_in (one))),
                            static_cast<int> (std::floor (
                              (0.5f * (segment.ay + segment.by) / spacing_y)
                                .numerical_value_in (one))))]
          .push_back (index);
      }
    }
    if (shore.empty ())
      return WaterlineProximity (grid, std::move (distance));

    const int reach_x = static_cast<int> (std::ceil (
                          (band / spacing_x).numerical_value_in (one))) +
                        1;
    const int reach_y = static_cast<int> (std::ceil (
                          (band / spacing_y).numerical_value_in (one))) +
                        1;

    const auto point_segment_distance =
      [&] (meters_t px, meters_t py, const ShoreSegment& segment) -> meters_t {
      meters_t ax = segment.ax, ay = segment.ay;
      meters_t bx = segment.bx, by = segment.by;
      // Bring the segment into the node's minimum image.
      const meters_t mid_x = 0.5f * (ax + bx);
      const meters_t mid_y = 0.5f * (ay + by);
      const meters_t shift_x =
        std::round (((px - mid_x) / world_x).numerical_value_in (one)) *
        world_x;
      const meters_t shift_y =
        std::round (((py - mid_y) / world_y).numerical_value_in (one)) *
        world_y;
      ax += shift_x;
      bx += shift_x;
      ay += shift_y;
      by += shift_y;
      const meters_t dx = bx - ax, dy = by - ay;
      const square_meters_t length2 = dx * dx + dy * dy;
      // How far along the stretch the nearest point lies.
      const proportion_t t =
        length2 > square_meters_t::zero ()
          ? std::clamp (
              proportion_t (((px - ax) * dx + (py - ay) * dy) / length2),
              0.0f * proportion[one],
              1.0f * proportion[one])
          : 0.0f * proportion[one];
      const meters_t cx = meters_t (ax + t * dx) - px;
      const meters_t cy = meters_t (ay + t * dy) - py;
      return sqrt (cx * cx + cy * cy);
    };

    // Exact distances for every node within the band of a bucketed
    // cell; everything farther keeps the clamp.
    for (const auto& [key, segment_indices] : buckets) {
      const int bx = static_cast<int> (key % width);
      const int by = static_cast<int> (key / width);
      for (int dy = -reach_y; dy <= reach_y; ++dy)
        for (int dx = -reach_x; dx <= reach_x; ++dx) {
          const int nx = wrap_index (bx + dx, static_cast<int> (width));
          const int ny = wrap_index (by + dy, static_cast<int> (height));
          const std::size_t node = static_cast<std::size_t> (ny) * width + nx;
          const meters_t px = static_cast<float> (nx) * spacing_x;
          const meters_t py = static_cast<float> (ny) * spacing_y;
          WaterlineDistance& best = distance[node];
          for (const std::uint32_t segment : segment_indices)
            best = std::min (best,
                             WaterlineDistance (point_segment_distance (
                               px, py, shore[segment])));
        }
    }

    return WaterlineProximity (grid, std::move (distance));
  }
}
