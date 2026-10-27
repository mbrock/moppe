#include <moppe/terrain/roads.hh>

#include <algorithm>
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
    for (const Road& road : network.roads) {
      const bool heights = road.heights.size () == road.points.size ();
      std::vector<bool> spans (road.points.size (), false);
      for (const RoadBridge& bridge : road.bridges)
        for (std::size_t k = bridge.first + 1;
             k <= bridge.last && k < spans.size ();
             ++k)
          spans[k] = true;
      float along = 0.0f;
      for (std::size_t i = 1; i < road.points.size (); ++i) {
        const RoadPoint a = road.points[i - 1];
        const RoadPoint b = road.points[i];
        const float length = std::hypot (b.x_m - a.x_m, b.z_m - a.z_m);
        const float ax = a.x_m - period_x * std::floor (a.x_m / period_x);
        const float az = a.z_m - period_z * std::floor (a.z_m / period_z);
        const float bx = ax + (b.x_m - a.x_m);
        const float bz = az + (b.z_m - a.z_m);
        const auto segment = static_cast<std::uint32_t> (m_rows.size () / 2);
        m_rows.push_back ({ ax, az, bx, bz });
        m_rows.push_back ({ heights ? road.heights[i - 1] : 0.0f,
                            heights ? road.heights[i] : 0.0f,
                            along,
                            road.kind != Road::Kind::Road || !heights ? 1.0f
                            : spans[i]                                ? 2.0f
                                                                      : 0.0f });
        along += length;
        const int x0 = static_cast<int> (
          std::floor ((std::min (ax, bx) - reach_m) / bucket_m));
        const int x1 = static_cast<int> (
          std::floor ((std::max (ax, bx) + reach_m) / bucket_m));
        const int z0 = static_cast<int> (
          std::floor ((std::min (az, bz) - reach_m) / bucket_m));
        const int z1 = static_cast<int> (
          std::floor ((std::max (az, bz) + reach_m) / bucket_m));
        for (int z = z0; z <= z1; ++z)
          for (int x = x0; x <= x1; ++x)
            buckets[static_cast<std::size_t> (wrap_index (z, m_buckets_z)) *
                      m_buckets_x +
                    static_cast<std::size_t> (wrap_index (x, m_buckets_x))]
              .push_back (segment);
      }
    }
    m_segment_count = m_rows.size () / 2;
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
        std::min (m_buckets_z - 1, static_cast<int> (z / bucket_m))) *
        m_buckets_x +
      static_cast<std::size_t> (
        std::min (m_buckets_x - 1, static_cast<int> (x / bucket_m)));
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
      const auto& [ax, az, bx, bz] = m_rows[2 * s];
      const auto& [ha, hb, along_a, kind] = m_rows[2 * s + 1];
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
      best.direction_x = length > 0.0f ? dx / length : 1.0f;
      best.direction_z = length > 0.0f ? dz / length : 0.0f;
      best.across = best.direction_x * oz - best.direction_z * ox;
      best.along = along_a + t * length;
      best.centre_m = ha + (hb - ha) * t;
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
    const float road =
      at.centre_m - m_formation.crown * std::min (d, m_formation.half_width);
    const float weight = 1.0f - smoothstep (tread, tread + blend_m, d);
    return field_m + (road - field_m) * weight;
  }
}
