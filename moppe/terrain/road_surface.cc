#include <moppe/terrain/roads.hh>

#include <algorithm>
#include <array>
#include <cmath>
#include <limits>

namespace moppe::terrain {
  namespace {
    float seam (float delta, float period) {
      return delta - period * std::round (delta / period);
    }

    float smoothstep (float low, float high, float value) {
      const float t = std::clamp ((value - low) / (high - low), 0.0f, 1.0f);
      return t * t * (3.0f - 2.0f * t);
    }

    // How far along a segment a point lies, 0 at its start to 1 at its
    // end, measured between the mitres at its ends: `on` metres along it
    // from its start and `aside` metres to its left, on a segment `length`
    // long that bends by angles of half-tangents `bend_a` and `bend_b`
    // there. On the inside of a bend the mitre cuts the segment short; on
    // the outside the segment ends square, and the wedge between it and
    // the next is level at their shared point's height, whichever of the
    // two is taken for the nearer. No further aside than the tread and
    // its blend reach, beyond which nothing is held to the height.
    float
    mitred (float on, float aside, float length, float bend_a, float bend_b) {
      constexpr float widest = 5.0f;
      const float a = std::clamp (aside, -widest, widest);
      const float cut_a = std::max (a * bend_a, 0.0f);
      const float cut_b = std::max (a * bend_b, 0.0f);
      const float span = std::max (length - cut_a - cut_b, 0.1f * length);
      return span > 0.0f ? std::clamp ((on - cut_a) / span, 0.0f, 1.0f) : 0.0f;
    }
  }

  std::vector<bool> paved_roads (const RoadNetwork& network) {
    std::uint32_t busiest = 0;
    for (const Road& road : network.roads)
      if (road.kind == Road::Kind::Road)
        busiest = std::max (busiest, road.traffic);
    std::vector<bool> paved;
    paved.reserve (network.roads.size ());
    for (const Road& road : network.roads)
      paved.push_back (road.kind == Road::Kind::Road &&
                       road.abandoned_years_ago == 0.0f && busiest > 0 &&
                       2 * road.traffic >= busiest);
    return paved;
  }

  std::vector<RoadTurning>
  road_turnings (const RoadNetwork& network, float period_x, float period_z) {
    // Once round, a twelfth of a turn at a time.
    constexpr float half = 0.5f, root = 0.8660254f;
    constexpr std::array<std::array<float, 2>, RoadTurning::segments> round { {
      { 1.0f, 0.0f },
      { root, half },
      { half, root },
      { 0.0f, 1.0f },
      { -half, root },
      { -root, half },
      { -1.0f, 0.0f },
      { -root, -half },
      { -half, -root },
      { 0.0f, -1.0f },
      { half, -root },
      { root, -half },
    } };
    // Another way within the tread of an end makes it a junction.
    constexpr float junction_m = 6.0f;
    std::vector<RoadTurning> turnings;
    for (std::size_t r = 0; r < network.roads.size (); ++r) {
      const Road& road = network.roads[r];
      if (road.kind != Road::Kind::Road || road.points.size () < 3)
        continue;
      for (const bool back : { false, true }) {
        const std::size_t n = road.points.size ();
        const std::size_t e = back ? n - 1 : 0;
        const std::size_t before = back ? n - 2 : 1;
        const RoadPoint end = road.points[e];
        const float ux = end.x_m - road.points[before].x_m;
        const float uz = end.z_m - road.points[before].z_m;
        const float length = std::hypot (ux, uz);
        if (length < 1e-3f)
          continue;
        bool junction = false;
        for (std::size_t q = 0; q < network.roads.size () && !junction; ++q) {
          const Road& other = network.roads[q];
          if (q == r || other.kind != Road::Kind::Road)
            continue;
          for (std::size_t i = 1; i < other.points.size () && !junction; ++i) {
            const float ax = seam (other.points[i - 1].x_m - end.x_m, period_x);
            const float az = seam (other.points[i - 1].z_m - end.z_m, period_z);
            const float dx = other.points[i].x_m - other.points[i - 1].x_m;
            const float dz = other.points[i].z_m - other.points[i - 1].z_m;
            const float length2 = dx * dx + dz * dz;
            const float t =
              length2 > 0.0f
                ? std::clamp (-(ax * dx + az * dz) / length2, 0.0f, 1.0f)
                : 0.0f;
            junction = std::hypot (ax + dx * t, az + dz * t) < junction_m;
          }
        }
        if (junction)
          continue;
        // The way on past the end, the way to its left, and the ring's
        // centre a radius back along the road.
        const float dx = ux / length, dz = uz / length;
        const float lx = -dz, lz = dx;
        const float cx = end.x_m - dx * RoadTurning::radius_m;
        const float cz = end.z_m - dz * RoadTurning::radius_m;
        const bool solved = road.heights.size () == n;
        const float grade =
          solved ? (road.heights[e] - road.heights[before]) / length : 0.0f;
        RoadTurning turning { r, back, {}, {} };
        for (std::size_t k = 0; k <= RoadTurning::segments; ++k) {
          const auto& [c, s] = round[k % RoadTurning::segments];
          // How far on past the centre, and to the left of it.
          const float on = RoadTurning::radius_m * c;
          const float aside = RoadTurning::radius_m * s;
          turning.points.push_back (
            { cx + dx * on + lx * aside, cz + dz * on + lz * aside });
          if (solved)
            turning.heights.push_back (road.heights[e] +
                                       grade * (on - RoadTurning::radius_m));
        }
        turnings.push_back (std::move (turning));
      }
    }
    return turnings;
  }

  RoadSurface::RoadSurface (const RoadNetwork& network,
                            float period_x,
                            float period_z,
                            const RoadFormation& formation)
      : m_formation (formation), m_period_x (period_x), m_period_z (period_z),
        m_buckets_x (
          std::max (1, static_cast<int> (std::ceil (period_x / bucket_m)))),
        m_buckets_z (
          std::max (1, static_cast<int> (std::ceil (period_z / bucket_m)))) {
    // Each segment begins inside the period, and its end follows it
    // across the seam.
    std::vector<std::vector<std::uint32_t>> buckets (
      static_cast<std::size_t> (m_buckets_x) * m_buckets_z);
    const float cell_x = bucket_x ();
    const float cell_z = bucket_z ();
    // One line of segments: its points, their heights (or none), and each
    // segment's kind.
    const auto lay = [&] (std::span<const RoadPoint> points,
                          std::span<const float> heights,
                          const auto& kind_of,
                          bool closed) {
      const std::size_t n = points.size ();
      if (n < 2)
        return;
      // The tangent of half the bend at a point between two segments,
      // positive turning left.
      const auto bend =
        [&] (std::size_t before, std::size_t at, std::size_t after) {
          const float ux = points[at].x_m - points[before].x_m;
          const float uz = points[at].z_m - points[before].z_m;
          const float vx = points[after].x_m - points[at].x_m;
          const float vz = points[after].z_m - points[at].z_m;
          const float cross = ux * vz - uz * vx;
          const float dot = ux * vx + uz * vz;
          const float lengths = std::hypot (ux, uz) * std::hypot (vx, vz);
          // tan (t / 2) = sin t / (1 + cos t); a hairpin's is held to that
          // of a right angle.
          return lengths > 0.0f
                   ? std::clamp (cross /
                                   std::max (lengths + dot, 1e-3f * lengths),
                                 -1.0f,
                                 1.0f)
                   : 0.0f;
        };
      float along = 0.0f;
      for (std::size_t i = 1; i < n; ++i) {
        const RoadPoint a = points[i - 1];
        const RoadPoint b = points[i];
        const float length = std::hypot (b.x_m - a.x_m, b.z_m - a.z_m);
        const float ax = a.x_m - period_x * std::floor (a.x_m / period_x);
        const float az = a.z_m - period_z * std::floor (a.z_m / period_z);
        const float bx = ax + (b.x_m - a.x_m);
        const float bz = az + (b.z_m - a.z_m);
        const auto segment =
          static_cast<std::uint32_t> (m_rows.size () / rows_per_segment);
        m_rows.push_back ({ ax, az, bx, bz });
        m_rows.push_back ({ heights.empty () ? 0.0f : heights[i - 1],
                            heights.empty () ? 0.0f : heights[i],
                            along,
                            kind_of (i) });
        m_rows.push_back ({ i > 1    ? bend (i - 2, i - 1, i)
                            : closed ? bend (n - 2, 0, 1)
                                     : 0.0f,
                            i + 1 < n ? bend (i - 1, i, i + 1)
                            : closed  ? bend (n - 2, n - 1, 1)
                                      : 0.0f,
                            0.0f,
                            0.0f });
        along += length;
        const int x0 = static_cast<int> (
          std::floor ((std::min (ax, bx) - reach_m) / cell_x));
        const int x1 = static_cast<int> (
          std::floor ((std::max (ax, bx) + reach_m) / cell_x));
        const int z0 = static_cast<int> (
          std::floor ((std::min (az, bz) - reach_m) / cell_z));
        const int z1 = static_cast<int> (
          std::floor ((std::max (az, bz) + reach_m) / cell_z));
        for (int z = z0; z <= z1; ++z)
          for (int x = x0; x <= x1; ++x)
            buckets[static_cast<std::size_t> (wrap_index (z, m_buckets_z)) *
                      m_buckets_x +
                    static_cast<std::size_t> (wrap_index (x, m_buckets_x))]
              .push_back (segment);
      }
    };
    const std::vector<bool> paved = paved_roads (network);
    for (std::size_t r = 0; r < network.roads.size (); ++r) {
      const Road& road = network.roads[r];
      const bool heights = road.heights.size () == road.points.size ();
      std::vector<bool> spans (road.points.size (), false);
      for (const RoadBridge& bridge : road.bridges)
        for (std::size_t k = bridge.first + 1;
             k <= bridge.last && k < spans.size ();
             ++k)
          spans[k] = true;
      lay (
        road.points,
        heights ? std::span<const float> (road.heights)
                : std::span<const float> (),
        [&] (std::size_t i) {
          return road.kind != Road::Kind::Road || !heights ? 1.0f
                 : spans[i]                                ? 2.0f
                 : paved[r]                                ? 3.0f
                                                           : 0.0f;
        },
        false);
    }
    for (const RoadTurning& turning :
         road_turnings (network, period_x, period_z))
      if (!turning.heights.empty ())
        lay (
          turning.points,
          turning.heights,
          [&] (std::size_t) { return paved[turning.road] ? 3.0f : 0.0f; },
          true);
    m_segment_count = m_rows.size () / rows_per_segment;
    m_index.reserve (2 * buckets.size ());
    std::uint32_t offset = static_cast<std::uint32_t> (2 * buckets.size ());
    for (const auto& bucket : buckets) {
      m_index.push_back (offset);
      m_index.push_back (static_cast<std::uint32_t> (bucket.size ()));
      offset += static_cast<std::uint32_t> (bucket.size ());
    }
    for (const auto& bucket : buckets)
      m_index.insert (m_index.end (), bucket.begin (), bucket.end ());
  }

  RoadSample RoadSurface::nearest (float x_m, float z_m) const {
    RoadSample best;
    if (empty ())
      return best;
    const float x = x_m - m_period_x * std::floor (x_m / m_period_x);
    const float z = z_m - m_period_z * std::floor (z_m / m_period_z);
    const std::size_t bucket =
      static_cast<std::size_t> (
        std::min (m_buckets_z - 1, static_cast<int> (z / bucket_z ()))) *
        m_buckets_x +
      static_cast<std::size_t> (
        std::min (m_buckets_x - 1, static_cast<int> (x / bucket_x ())));
    const std::uint32_t offset = m_index[2 * bucket];
    const std::uint32_t count = m_index[2 * bucket + 1];
    // A built road is the ground wherever it reaches, over any footpath
    // beside it; between two of a kind the nearer wins.
    const auto rank = [] (bool road, float distance) {
      return (road ? 0.0f : 2.0f * reach_m) + distance;
    };
    float best_rank = std::numeric_limits<float>::infinity ();
    for (std::uint32_t k = 0; k < count; ++k) {
      const std::uint32_t s = m_index[offset + k];
      const auto& [ax, az, bx, bz] = m_rows[rows_per_segment * s];
      const auto& [ha, hb, along_a, kind] = m_rows[rows_per_segment * s + 1];
      const auto& [bend_a, bend_b, unused_0, unused_1] =
        m_rows[rows_per_segment * s + 2];
      const float px = seam (x - ax, m_period_x);
      const float pz = seam (z - az, m_period_z);
      const float dx = bx - ax;
      const float dz = bz - az;
      const float length2 = dx * dx + dz * dz;
      const float t = length2 > 0.0f
                        ? std::clamp ((px * dx + pz * dz) / length2, 0.0f, 1.0f)
                        : 0.0f;
      const float ox = px - dx * t;
      const float oz = pz - dz * t;
      const float distance = std::hypot (ox, oz);
      const bool road = kind != 1.0f;
      if (distance > reach_m || !(rank (road, distance) < best_rank))
        continue;
      best_rank = rank (road, distance);
      const float length = std::sqrt (length2);
      best.found = true;
      best.kind = road ? Road::Kind::Road : Road::Kind::Footpath;
      best.bridge = kind == 2.0f;
      best.paved = kind == 3.0f;
      best.direction_x = length > 0.0f ? dx / length : 1.0f;
      best.direction_z = length > 0.0f ? dz / length : 0.0f;
      // Past the segment's end the distance is from the end: a road
      // ends rounded where it ends, and no further on.
      best.across =
        std::copysign (distance, best.direction_x * oz - best.direction_z * ox);
      const float share = mitred (px * best.direction_x + pz * best.direction_z,
                                  best.direction_x * pz - best.direction_z * px,
                                  length,
                                  bend_a,
                                  bend_b);
      best.along = along_a + share * length;
      best.centre_m = ha + (hb - ha) * share;
      best.grade = length > 0.0f ? (hb - ha) / length : 0.0f;
    }
    return best;
  }

  float RoadSurface::ground (float x_m, float z_m, float field_m) const {
    const RoadSample at = nearest (x_m, z_m);
    if (!at.found || at.kind != Road::Kind::Road)
      return field_m;
    const float d = std::fabs (at.across);
    // The deck is level across, out to its kerbs; beyond them, and under
    // it, is the field. A bank that stands above the deck's end is its
    // abutment.
    if (at.bridge)
      return d <= m_formation.deck_half_width ? std::max (at.centre_m, field_m)
                                              : field_m;
    const float tread = m_formation.half_width + m_formation.shoulder;
    const float half = m_formation.half_width;
    const float surfacing =
      at.paved ? asphalt_m * (1.0f - smoothstep (half - 0.1f, half + 0.1f, d))
               : gravel_m * (1.0f - smoothstep (half - 0.3f, half + 0.6f, d));
    const float road =
      at.centre_m - m_formation.crown * std::min (d, half) + surfacing;
    const float weight = 1.0f - smoothstep (tread, tread + blend_m, d);
    return field_m + (road - field_m) * weight;
  }

  void RoadSurface::place_guardrails (
    const RoadNetwork& network,
    const std::function<float (float, float)>& field) {
    m_guardrails.clear ();
    const float tread = m_formation.half_width + m_formation.shoulder;
    const auto drawn = [&] (float x, float z) {
      return ground (x, z, field (x, z));
    };
    const std::vector<bool> paved = paved_roads (network);
    for (std::size_t r = 0; r < network.roads.size (); ++r) {
      const Road& road = network.roads[r];
      if (!paved[r] || road.heights.size () != road.points.size () ||
          road.points.size () < 2)
        continue;
      std::vector<bool> spans (road.points.size (), false);
      for (const RoadBridge& bridge : road.bridges)
        for (std::size_t k = bridge.first;
             k <= bridge.last && k < spans.size ();
             ++k)
          spans[k] = true;
      // A post's place every few metres along the road, on either side.
      struct Station {
        float x, z, left_x, left_z, edge;
        bool bridge;
      };
      std::vector<Station> stations;
      float next = 0.0f, along = 0.0f;
      for (std::size_t k = 1; k < road.points.size (); ++k) {
        const RoadPoint a = road.points[k - 1];
        const RoadPoint b = road.points[k];
        const float length = std::hypot (b.x_m - a.x_m, b.z_m - a.z_m);
        if (length < 1e-3f)
          continue;
        const float ux = (b.x_m - a.x_m) / length;
        const float uz = (b.z_m - a.z_m) / length;
        for (; next <= along + length; next += guardrail_spacing_m) {
          const float t = (next - along) / length;
          stations.push_back (
            { a.x_m + (b.x_m - a.x_m) * t,
              a.z_m + (b.z_m - a.z_m) * t,
              -uz,
              ux,
              std::lerp (road.heights[k - 1], road.heights[k], t) -
                m_formation.crown * m_formation.half_width,
              spans[k - 1] || spans[k] });
        }
        along += length;
      }
      for (const float side : { -1.0f, 1.0f }) {
        const std::size_t n = stations.size ();
        std::vector<bool> valid (n), wanted (n);
        for (std::size_t i = 0; i < n; ++i) {
          const Station& s = stations[i];
          const float px = s.x + s.left_x * side * guardrail_offset_m;
          const float pz = s.z + s.left_z * side * guardrail_offset_m;
          // Not on a tread: inside a tight bend, or across a junction.
          const RoadSample there = nearest (px, pz);
          valid[i] =
            !s.bridge && !(there.found && there.kind == Road::Kind::Road &&
                           std::fabs (there.across) < tread + 0.2f);
          // Where the ground a few metres out lies well below the road.
          const float fx = s.x + s.left_x * side * (tread + 3.0f);
          const float fz = s.z + s.left_z * side * (tread + 3.0f);
          wanted[i] = valid[i] && s.edge - drawn (fx, fz) >= 0.7f;
        }
        // One post more at either end of a run, which also closes a gap
        // of one; runs shorter than a car stay unbuilt.
        Guardrail run;
        const auto flush = [&] {
          if (run.posts.size () >= 4)
            m_guardrails.push_back (std::move (run));
          run.posts.clear ();
        };
        for (std::size_t i = 0; i < n; ++i) {
          const bool on = valid[i] && (wanted[i] || (i > 0 && wanted[i - 1]) ||
                                       (i + 1 < n && wanted[i + 1]));
          if (!on) {
            flush ();
            continue;
          }
          const Station& s = stations[i];
          const float ox = s.left_x * side, oz = s.left_z * side;
          const float px = s.x + ox * guardrail_offset_m;
          const float pz = s.z + oz * guardrail_offset_m;
          run.posts.push_back (
            { px, pz, s.edge + guardrail_height_m, drawn (px, pz), ox, oz });
        }
        flush ();
      }
    }
  }
}
