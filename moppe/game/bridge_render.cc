#include <moppe/game/bridge_render.hh>

#include <moppe/gfx/mat4.hh>

#include <algorithm>
#include <cmath>
#include <cstdint>

namespace moppe::game {
  namespace {
    // Weathered timber, tarred stringers, and the abutments' granite.
    constexpr float plank[3] = { 0.47f, 0.40f, 0.32f };
    constexpr float tarred[3] = { 0.20f, 0.17f, 0.14f };
    constexpr float rail[3] = { 0.52f, 0.46f, 0.38f };
    constexpr float stone[3] = { 0.50f, 0.49f, 0.46f };

    float jitter (std::uint32_t k) {
      k ^= k >> 16;
      k *= 0x7feb352dU;
      k ^= k >> 15;
      k *= 0x846ca68bU;
      k ^= k >> 16;
      return static_cast<float> (k & 0xffffU) / 65535.0f;
    }

    // A box about `centre` with the given unit axes and half sizes.
    void box (render::DrawList& dl,
              const Vec3& centre,
              const Vec3& u,
              const Vec3& v,
              const Vec3& w,
              float hu,
              float hv,
              float hw,
              const float (&colour)[3],
              float shade = 1.0f) {
      dl.color (colour[0] * shade, colour[1] * shade, colour[2] * shade);
      const auto face = [&] (const Vec3& n, const Vec3& a, const Vec3& b) {
        const Vec3 c = centre + n;
        dl.normal (normalized (n));
        dl.vertex (c - a - b);
        dl.vertex (c + a - b);
        dl.vertex (c + a + b);
        dl.vertex (c - a - b);
        dl.vertex (c + a + b);
        dl.vertex (c - a + b);
      };
      face (u * hu, v * hv, w * hw);
      face (u * -hu, w * hw, v * hv);
      face (v * hv, w * hw, u * hu);
      face (v * -hv, u * hu, w * hw);
      face (w * hw, u * hu, v * hv);
      face (w * -hw, v * hv, u * hu);
    }

    // A beam from `a` to `b`, `half` across and `depth` high.
    void beam (render::DrawList& dl,
               const Vec3& a,
               const Vec3& b,
               float half,
               float depth,
               const float (&colour)[3],
               float shade = 1.0f) {
      const Vec3 along = b - a;
      const float length = std::max (moppe::length (along), 1e-3f);
      const Vec3 u = along / length;
      const Vec3 side = normalized (cross (Vec3 (0.0f, 1.0f, 0.0f), u));
      const Vec3 up = cross (u, side);
      box (dl,
           (a + b) * 0.5f,
           u,
           side,
           up,
           0.5f * length,
           half,
           0.5f * depth,
           colour,
           shade);
    }
  }

  void BridgeSurface::rebuild (render::Renderer& renderer,
                               const map::SurfaceGeometry& surface,
                               const terrain::RoadNetwork& roads,
                               const terrain::RoadFormation& formation) {
    m_bridges.clear ();
    m_period_x = surface.domain ().period_x ().numerical_value_in (u::m);
    m_period_z = surface.domain ().period_z ().numerical_value_in (u::m);
    const auto ground = [&] (float x, float z) {
      x -= m_period_x * std::floor (x / m_period_x);
      z -= m_period_z * std::floor (z / m_period_z);
      return terrain::surface_elevation_value (
        spatial::sample<terrain::surface_elevation> (
          surface, position (Vec3 (x, 0.0f, z))));
    };
    const float half = formation.deck_half_width;
    std::uint32_t seed = 0;
    for (const terrain::Road& road : roads.roads)
      for (const terrain::RoadBridge& bridge : road.bridges) {
        const std::size_t first = bridge.first;
        const std::size_t last = bridge.last;
        if (last <= first || last >= road.points.size () ||
            road.heights.size () != road.points.size ())
          continue;
        const auto at = [&] (std::size_t k) {
          return Vec3 (road.points[k].x_m, road.heights[k], road.points[k].z_m);
        };
        const Vec3 origin = at (first);
        // Unwrapped, the line runs on past the period; the mesh is built
        // about its first point and placed by its image inside the period.
        const Vec3 centre (
          origin[0] - m_period_x * std::floor (origin[0] / m_period_x),
          origin[1],
          origin[2] - m_period_z * std::floor (origin[2] / m_period_z));
        const auto local = [&] (const Vec3& p) {
          return Vec3 (p[0] - origin[0], p[1], p[2] - origin[2]);
        };
        const auto ground_under = [&] (const Vec3& p) {
          return ground (p[0], p[2]);
        };
        // The way across at each point, left of the way along.
        const auto side_at = [&] (std::size_t k) {
          const Vec3 a = at (k > first ? k - 1 : k);
          const Vec3 b = at (k < last ? k + 1 : k);
          Vec3 d (b[0] - a[0], 0.0f, b[2] - a[2]);
          d = normalized (d);
          return Vec3 (-d[2], 0.0f, d[0]);
        };

        render::DrawList dl;
        dl.lit (true);
        dl.fogged (true);
        dl.material (0.0f, 0.9f, 0.0f);
        dl.begin (render::Prim::Triangles);
        const Vec3 up (0.0f, 1.0f, 0.0f);
        float along = 0.0f;
        float next_post = 0.0f;
        float next_trestle = 12.0f;
        const float span = [&] {
          float s = 0.0f;
          for (std::size_t k = first + 1; k <= last; ++k)
            s += length (local (at (k)) - local (at (k - 1)));
          return s;
        }();
        for (std::size_t k = first + 1; k <= last; ++k) {
          const Vec3 a = local (at (k - 1));
          const Vec3 b = local (at (k));
          const Vec3 sa = side_at (k - 1);
          const Vec3 sb = side_at (k);
          const float length = moppe::length (b - a);
          const Vec3 u = (b - a) / std::max (length, 1e-3f);
          const Vec3 side = normalized ((sa + sb) * 0.5f);
          // Planks across the way, a little more than a third of a metre
          // each, laid on the stringers; their top is the deck the
          // physics drives on.
          const int planks = std::max (1, static_cast<int> (length / 0.36f));
          for (int p = 0; p < planks; ++p) {
            const float t = (static_cast<float> (p) + 0.5f) / planks;
            const Vec3 c = a + (b - a) * t - up * 0.04f;
            box (dl,
                 c,
                 u,
                 side,
                 up,
                 0.5f * length / planks - 0.015f,
                 half + 0.15f,
                 0.04f,
                 plank,
                 0.82f + 0.3f * jitter (++seed));
          }
          // Two stringers under the deck, and the kerbs along its edges.
          for (const float s : { -1.5f, 1.5f })
            beam (dl,
                  a + sa * s - up * 0.38f,
                  b + sb * s - up * 0.38f,
                  0.16f,
                  0.6f,
                  tarred);
          for (const float s : { -1.0f, 1.0f }) {
            const float e = s * (half + 0.12f);
            beam (dl,
                  a + sa * e + up * 0.1f,
                  b + sb * e + up * 0.1f,
                  0.12f,
                  0.2f,
                  rail,
                  0.9f);
            // The railing: a top rail and a mid rail.
            const float r = s * (half + 0.3f);
            beam (dl,
                  a + sa * r + up * 1.05f,
                  b + sb * r + up * 1.05f,
                  0.05f,
                  0.12f,
                  rail);
            beam (dl,
                  a + sa * r + up * 0.55f,
                  b + sb * r + up * 0.55f,
                  0.04f,
                  0.09f,
                  rail,
                  0.92f);
          }
          // Posts every two metres or so.
          while (next_post <= along + length) {
            const float t =
              std::clamp ((next_post - along) / length, 0.0f, 1.0f);
            const Vec3 c = a + (b - a) * t;
            for (const float s : { -1.0f, 1.0f })
              box (dl,
                   c + side * (s * (half + 0.3f)) + up * 0.5f,
                   u,
                   side,
                   up,
                   0.06f,
                   0.06f,
                   0.6f,
                   rail,
                   0.85f);
            next_post += span / std::max (1.0f, std::round (span / 2.2f));
          }
          // A trestle standing in the river: two raked legs down to the
          // bed under a cap beam.
          while (next_trestle < std::min (along + length, span - 6.0f)) {
            const float t = (next_trestle - along) / length;
            const Vec3 c = a + (b - a) * t;
            const Vec3 cap = c - up * 0.85f;
            beam (
              dl, cap - side * 2.4f, cap + side * 2.4f, 0.18f, 0.3f, tarred);
            for (const float s : { -1.0f, 1.0f }) {
              const Vec3 top = cap + side * (s * 1.7f);
              const Vec3 foot_xz = c + side * (s * 2.3f);
              const Vec3 world_foot (
                foot_xz[0] + origin[0], 0.0f, foot_xz[2] + origin[2]);
              const float bed = ground_under (world_foot) - 0.6f;
              beam (
                dl,
                Vec3 (top[0], top[1], top[2]),
                Vec3 (foot_xz[0], std::min (bed, top[1] - 0.5f), foot_xz[2]),
                0.16f,
                0.32f,
                tarred,
                0.9f);
            }
            next_trestle += span / std::max (1.0f, std::round (span / 12.0f));
          }
          along += length;
        }
        // The abutments: granite blocks at each end, from the deck down
        // into the bank.
        for (const std::size_t k : { first, last }) {
          const Vec3 c = local (at (k));
          const Vec3 inward = k == first
                                ? normalized (local (at (first + 1)) - c)
                                : normalized (local (at (last - 1)) - c);
          const Vec3 flat = normalized (Vec3 (inward[0], 0.0f, inward[2]));
          const Vec3 side = side_at (k);
          const float bank =
            ground_under (Vec3 (at (k)[0], 0.0f, at (k)[2])) - 1.5f;
          const float bottom = std::min (bank, c[1] - 1.0f);
          const float height = c[1] - 0.06f - bottom;
          box (dl,
               Vec3 (c[0], bottom + 0.5f * height, c[2]) + flat * 0.6f,
               flat,
               side,
               up,
               1.0f,
               half + 0.6f,
               0.5f * height,
               stone);
        }
        dl.end ();
        m_bridges.push_back ({ renderer.create_mesh (dl), centre });
      }
  }

  void BridgeSurface::clear () {
    m_bridges.clear ();
  }

  void BridgeSurface::draw (render::Renderer& renderer,
                            const Vec3& camera) const {
    std::uint64_t motion = 0x62726964000ULL;
    for (const Bridge& bridge : m_bridges) {
      const float x =
        bridge.centre[0] +
        m_period_x * std::round ((camera[0] - bridge.centre[0]) / m_period_x);
      const float z =
        bridge.centre[2] +
        m_period_z * std::round ((camera[2] - bridge.centre[2]) / m_period_z);
      if (std::hypot (x - camera[0], z - camera[2]) > 2500.0f)
        continue;
      renderer.draw_mesh (
        *bridge.mesh, Mat4::translation (Vec3 (x, 0.0f, z)), ++motion);
    }
  }
}
