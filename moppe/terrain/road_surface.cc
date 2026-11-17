#include <moppe/terrain/roads.hh>

#include <moppe/correct_math.hh>

#include <algorithm>
#include <array>
#include <cmath>
#include <limits>

namespace moppe::terrain {
  namespace {
    float road_smoothstep (meters_t low, meters_t high, meters_t value) {
      const float t = std::clamp (
        ((value - low) / (high - low)).numerical_value_in (mp_units::one),
        0.0f,
        1.0f);
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
    float mitred (meters_t on,
                  meters_t aside,
                  meters_t length,
                  float bend_a,
                  float bend_b) {
      constexpr meters_t widest = 5.0f * u::m;
      constexpr meters_t zero = 0.0f * u::m;
      const meters_t a = std::clamp (aside, -widest, widest);
      const meters_t cut_a = std::max (a * bend_a, zero);
      const meters_t cut_b = std::max (a * bend_b, zero);
      const meters_t span = std::max (length - cut_a - cut_b, 0.1f * length);
      return span > zero
               ? std::clamp (
                   ((on - cut_a) / span).numerical_value_in (mp_units::one),
                   0.0f,
                   1.0f)
               : 0.0f;
    }

    // A coordinate brought into [0, period) as the road surface always
    // has: `value - period * floor (value / period)` (not quite
    // wrap_coordinate, whose product is the other way round and which
    // folds the period itself to zero).
    meters_t into_period (meters_t value, meters_t period) {
      return value -
             period * std::floor ((value / period).numerical_value_in (one));
    }

    plan_position_t into_period (plan_position_t at, plan_extent_t period) {
      return plan_position (
        into_period (x_of (at), plan_value (period).x () * u::m),
        into_period (z_of (at), plan_value (period).z () * u::m));
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
      paved.push_back (road.loop ||
                       (road.kind == Road::Kind::Road &&
                        road.abandoned == age_t::zero () && busiest > 0 &&
                        2 * road.traffic >= busiest));
    return paved;
  }

  std::vector<RoadTurning> road_turnings (const RoadNetwork& network,
                                          plan_extent_t period) {
    // Once round, a twelfth of a turn at a time: the cosine and sine.
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
    constexpr meters_t junction_reach = 6.0f * u::m;
    std::vector<RoadTurning> turnings;
    for (std::size_t r = 0; r < network.roads.size (); ++r) {
      const Road& road = network.roads[r];
      if (road.kind != Road::Kind::Road || road.loop || road.points.size () < 3)
        continue;
      for (const bool back : { false, true }) {
        const std::size_t n = road.points.size ();
        const std::size_t e = back ? n - 1 : 0;
        const std::size_t before = back ? n - 2 : 1;
        const plan_position_t end = road.points[e];
        const plan_displacement_t last = end - road.points[before];
        const meters_t length = hypot (last);
        if (length < 1e-3f * u::m)
          continue;
        bool junction = false;
        for (std::size_t q = 0; q < network.roads.size () && !junction; ++q) {
          const Road& other = network.roads[q];
          if (q == r || other.kind != Road::Kind::Road)
            continue;
          for (std::size_t i = 1; i < other.points.size () && !junction; ++i) {
            // From the end to the segment's start, and along the segment.
            const plan_displacement_t a =
              minimum_image (other.points[i - 1] - end, period);
            const plan_displacement_t d = other.points[i] - other.points[i - 1];
            const float length2 = moppe::length2 (plan_value (d));
            const float t =
              length2 > 0.0f
                ? std::clamp (
                    -dot (plan_value (a), plan_value (d)) / length2, 0.0f, 1.0f)
                : 0.0f;
            junction = hypot (a + plan_displacement (plan_value (d) * t)) <
                       junction_reach;
          }
        }
        if (junction)
          continue;
        // The way on past the end, the way to its left, and the ring's
        // centre a radius back along the road.
        const plan_direction_t way =
          plan_unit_direction (plan_value (last) / meters_value (length));
        const plan_direction_t left = plan_unit_direction (
          Vec2 (-plan_value (way).z (), plan_value (way).x ()));
        const plan_position_t centre = end - along (way, RoadTurning::radius);
        const bool solved = road.heights.size () == n;
        const grade_t climb = solved ? (road.heights[e] - road.heights[before])
                                           .numerical_value_in (u::m) /
                                         meters_value (length) * grade[one]
                                     : 0.0f * grade[one];
        RoadTurning turning { r, back, {}, {} };
        for (std::size_t k = 0; k <= RoadTurning::segments; ++k) {
          const auto& [c, s] = round[k % RoadTurning::segments];
          // How far on past the centre, and to the left of it.
          const meters_t on = RoadTurning::radius * c;
          const meters_t aside = RoadTurning::radius * s;
          turning.points.push_back (centre + along (way, on) +
                                    along (left, aside));
          if (solved)
            turning.heights.push_back (road.heights[e] +
                                       climb.numerical_value_in (one) *
                                         (on - RoadTurning::radius));
        }
        turnings.push_back (std::move (turning));
      }
    }
    return turnings;
  }

  RoadSurface::RoadSurface (const RoadNetwork& network,
                            plan_extent_t period,
                            const RoadFormation& formation)
      : m_formation (formation), m_period (period),
        m_buckets_x (std::max (1,
                               static_cast<int> (std::ceil (
                                 (plan_value (period).x () * u::m / bucket)
                                   .numerical_value_in (one))))),
        m_buckets_z (std::max (1,
                               static_cast<int> (std::ceil (
                                 (plan_value (period).z () * u::m / bucket)
                                   .numerical_value_in (one))))) {
    // Each segment begins inside the period, and its end follows it
    // across the seam.
    std::vector<std::vector<std::uint32_t>> buckets (
      static_cast<std::size_t> (m_buckets_x) * m_buckets_z);
    const meters_t cell_x = bucket_x ();
    const meters_t cell_z = bucket_z ();
    // Which bucket a coordinate falls in, unwrapped.
    const auto cell = [] (meters_t at, meters_t side) {
      return static_cast<int> (
        std::floor ((at / side).numerical_value_in (one)));
    };
    // One line of segments: its points, their heights (or none), and each
    // segment's kind.
    const auto lay = [&] (std::span<const plan_position_t> points,
                          std::span<const SurfaceElevation> heights,
                          const auto& kind_of,
                          bool closed) {
      const std::size_t n = points.size ();
      if (n < 2)
        return;
      // The tangent of half the bend at a point between two segments,
      // positive turning left: a row's number for the GPU.
      const auto bend =
        [&] (std::size_t before, std::size_t at, std::size_t after) {
          const Vec2 u = plan_value (points[at] - points[before]);
          const Vec2 v = plan_value (points[after] - points[at]);
          const float cross = u.x () * v.z () - u.z () * v.x ();
          const float dot = u.x () * v.x () + u.z () * v.z ();
          const float lengths = moppe::hypot (u) * moppe::hypot (v);
          // tan (t / 2) = sin t / (1 + cos t); a hairpin's is held to that
          // of a right angle.
          return lengths > 0.0f
                   ? std::clamp (cross /
                                   std::max (lengths + dot, 1e-3f * lengths),
                                 -1.0f,
                                 1.0f)
                   : 0.0f;
        };
      const auto height = [&] (std::size_t i) {
        return heights.empty () ? 0.0f : surface_elevation_value (heights[i]);
      };
      meters_t along = 0.0f * u::m;
      for (std::size_t i = 1; i < n; ++i) {
        const plan_displacement_t step = points[i] - points[i - 1];
        const meters_t length = hypot (step);
        const plan_position_t a = into_period (points[i - 1], m_period);
        const plan_position_t b = a + step;
        const auto segment =
          static_cast<std::uint32_t> (m_rows.size () / rows_per_segment);
        m_rows.push_back ({ plan_value (a).x (),
                            plan_value (a).z (),
                            plan_value (b).x (),
                            plan_value (b).z () });
        m_rows.push_back (
          { height (i - 1), height (i), meters_value (along), kind_of (i) });
        m_rows.push_back ({ i > 1    ? bend (i - 2, i - 1, i)
                            : closed ? bend (n - 2, 0, 1)
                                     : 0.0f,
                            i + 1 < n ? bend (i - 1, i, i + 1)
                            : closed  ? bend (n - 2, n - 1, 1)
                                      : 0.0f,
                            0.0f,
                            0.0f });
        along += length;
        const int x0 = cell (std::min (x_of (a), x_of (b)) - reach, cell_x);
        const int x1 = cell (std::max (x_of (a), x_of (b)) + reach, cell_x);
        const int z0 = cell (std::min (z_of (a), z_of (b)) - reach, cell_z);
        const int z1 = cell (std::max (z_of (a), z_of (b)) + reach, cell_z);
        for (int z = z0; z <= z1; ++z)
          for (int x = x0; x <= x1; ++x)
            buckets[static_cast<std::size_t> (wrap_index (z, m_buckets_z)) *
                      m_buckets_x +
                    static_cast<std::size_t> (wrap_index (x, m_buckets_x))]
              .push_back (segment);
      }
    };
    // The kinds as segment_rows () gives them.
    constexpr float road_row = 0.0f, footpath_row = 1.0f, deck_row = 2.0f,
                    paved_row = 3.0f;
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
        heights ? std::span<const SurfaceElevation> (road.heights)
                : std::span<const SurfaceElevation> (),
        [&] (std::size_t i) {
          return road.kind != Road::Kind::Road || !heights ? footpath_row
                 : spans[i]                                ? deck_row
                 : paved[r]                                ? paved_row
                                                           : road_row;
        },
        false);
    }
    for (const RoadTurning& turning : road_turnings (network, period))
      if (!turning.heights.empty ())
        lay (
          turning.points,
          turning.heights,
          [&] (std::size_t) {
            return paved[turning.road] ? paved_row : road_row;
          },
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

  RoadSample RoadSurface::nearest (plan_position_t at) const {
    RoadSample best;
    if (empty ())
      return best;
    // Per body per frame: the search runs on the rows' own floats.
    const Vec2& period = plan_value (m_period);
    const Vec2& wrapped = plan_value (into_period (at, m_period));
    const float x = wrapped.x ();
    const float z = wrapped.z ();
    const std::size_t bucket =
      static_cast<std::size_t> (std::min (
        m_buckets_z - 1, static_cast<int> (z / meters_value (bucket_z ())))) *
        m_buckets_x +
      static_cast<std::size_t> (std::min (
        m_buckets_x - 1, static_cast<int> (x / meters_value (bucket_x ()))));
    const std::uint32_t offset = m_index[2 * bucket];
    const std::uint32_t count = m_index[2 * bucket + 1];
    const float reach_m = meters_value (reach);
    // A built road is the ground wherever it reaches, over any footpath
    // beside it; between two of a kind the nearer wins.
    const auto rank = [&] (bool road, float distance) {
      return (road ? 0.0f : 2.0f * reach_m) + distance;
    };
    float best_rank = std::numeric_limits<float>::infinity ();
    for (std::uint32_t k = 0; k < count; ++k) {
      const std::uint32_t s = m_index[offset + k];
      const auto& [ax, az, bx, bz] = m_rows[rows_per_segment * s];
      const auto& [ha, hb, along_a, kind] = m_rows[rows_per_segment * s + 1];
      const auto& [bend_a, bend_b, unused_0, unused_1] =
        m_rows[rows_per_segment * s + 2];
      const float px = minimum_image_delta (x - ax, period.x ());
      const float pz = minimum_image_delta (z - az, period.z ());
      const float dx = bx - ax;
      const float dz = bz - az;
      const float length2 = dx * dx + dz * dz;
      const float t = length2 > 0.0f
                        ? std::clamp ((px * dx + pz * dz) / length2, 0.0f, 1.0f)
                        : 0.0f;
      const float ox = px - dx * t;
      const float oz = pz - dz * t;
      const float distance = cr::hypot (ox, oz);
      const bool road = kind != 1.0f;
      if (distance > reach_m || !(rank (road, distance) < best_rank))
        continue;
      best_rank = rank (road, distance);
      const float length = std::sqrt (length2);
      const float way_x = length > 0.0f ? dx / length : 1.0f;
      const float way_z = length > 0.0f ? dz / length : 0.0f;
      best.found = true;
      best.kind = road ? Road::Kind::Road : Road::Kind::Footpath;
      best.bridge = kind == 2.0f;
      best.paved = kind == 3.0f;
      best.direction = plan_unit_direction (Vec2 (way_x, way_z));
      // Past the segment's end the distance is from the end: a road
      // ends rounded where it ends, and no further on.
      best.across = std::copysign (distance, way_x * oz - way_z * ox) * u::m;
      const float share = mitred ((px * way_x + pz * way_z) * u::m,
                                  (way_x * pz - way_z * px) * u::m,
                                  length * u::m,
                                  bend_a,
                                  bend_b);
      best.along = (along_a + share * length) * u::m;
      best.centre = surface_elevation_point ((ha + (hb - ha) * share) * u::m);
      best.grade = (length > 0.0f ? (hb - ha) / length : 0.0f) * grade[one];
    }
    return best;
  }

  SurfaceElevation RoadSurface::ground (plan_position_t at,
                                        SurfaceElevation field) const {
    const RoadSample sample = nearest (at);
    if (!sample.found || sample.kind != Road::Kind::Road)
      return field;
    const meters_t d = abs (sample.across);
    const SurfaceElevation centre = sample.centre;
    // The deck is level across, out to its kerbs; beyond them, and under
    // it, is the field. A bank that stands above the deck's end is its
    // abutment.
    if (sample.bridge)
      return d <= m_formation.deck_half_width ? std::max (centre, field)
                                              : field;
    const meters_t half = m_formation.half_width;
    const meters_t tread = half + m_formation.shoulder;
    const meters_t surfacing =
      sample.paved
        ? asphalt *
            (1.0f - road_smoothstep (half - 0.1f * u::m, half + 0.1f * u::m, d))
        : gravel * (1.0f - road_smoothstep (
                             half - 0.3f * u::m, half + 0.6f * u::m, d));
    const SurfaceElevation road =
      centre - m_formation.crown.numerical_value_in (one) * std::min (d, half) +
      surfacing;
    const float weight = 1.0f - road_smoothstep (tread, tread + blend, d);
    return field + (road - field) * weight;
  }

  void RoadSurface::place_guardrails (const RoadNetwork& network,
                                      const ElevationField& field) {
    m_guardrails.clear ();
    const meters_t tread = m_formation.half_width + m_formation.shoulder;
    // The field takes the point as it lies along the road, unwrapped.
    const auto drawn = [&] (plan_position_t at) {
      return ground (at, field (at));
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
        plan_position_t at;
        plan_direction_t left;
        SurfaceElevation edge;
        bool bridge;
      };
      std::vector<Station> stations;
      meters_t next = 0.0f * u::m, walked = 0.0f * u::m;
      for (std::size_t k = 1; k < road.points.size (); ++k) {
        const plan_position_t a = road.points[k - 1];
        const plan_position_t b = road.points[k];
        const plan_displacement_t step = b - a;
        const meters_t length = hypot (step);
        if (length < 1e-3f * u::m)
          continue;
        const Vec2 way = plan_value (step) / meters_value (length);
        const plan_direction_t left =
          plan_unit_direction (Vec2 (-way.z (), way.x ()));
        for (; next <= walked + length; next += guardrail_spacing) {
          const float t = ((next - walked) / length).numerical_value_in (one);
          stations.push_back (
            { a + plan_displacement (plan_value (step) * t),
              left,
              surface_elevation_point (
                std::lerp (surface_elevation_value (road.heights[k - 1]),
                           surface_elevation_value (road.heights[k]),
                           t) *
                u::m) -
                m_formation.crown.numerical_value_in (one) *
                  m_formation.half_width,
              spans[k - 1] || spans[k] });
        }
        walked += length;
      }
      for (const float side : { -1.0f, 1.0f }) {
        const std::size_t n = stations.size ();
        // Away from the road on this side.
        const auto out = [&] (const Station& s) {
          return plan_unit_direction (plan_value (s.left) * side);
        };
        std::vector<bool> valid (n), wanted (n);
        for (std::size_t i = 0; i < n; ++i) {
          const Station& s = stations[i];
          const plan_position_t post = s.at + along (out (s), guardrail_offset);
          // Not on a tread: inside a tight bend, or across a junction.
          const RoadSample there = nearest (post);
          valid[i] =
            !s.bridge && !(there.found && there.kind == Road::Kind::Road &&
                           abs (there.across) < tread + 0.2f * u::m);
          // Where the ground a few metres out lies well below the road.
          const plan_position_t beyond =
            s.at + along (out (s), tread + 3.0f * u::m);
          wanted[i] = valid[i] && s.edge - drawn (beyond) >= 0.7f * u::m;
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
          const plan_position_t post = s.at + along (out (s), guardrail_offset);
          run.posts.push_back (
            { post, s.edge + guardrail_height, drawn (post), out (s) });
        }
        flush ();
      }
    }
  }
}
