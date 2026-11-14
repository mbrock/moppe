#include <moppe/mov/box3d_vec.hh>
#include <moppe/mov/hull_water.hh>

#include <algorithm>
#include <cmath>

namespace moppe::mov {
  void add_hull_quad (std::vector<HullPanel>& out,
                      const Vec3& a,
                      const Vec3& b,
                      const Vec3& c,
                      const Vec3& d,
                      int n,
                      int m,
                      const Vec3& inside,
                      bool transom) {
    const auto at = [&] (float u, float v) {
      return (a * (1 - u) + b * u) * (1 - v) + (d * (1 - u) + c * u) * v;
    };
    for (int i = 0; i < n; ++i)
      for (int j = 0; j < m; ++j) {
        const float u0 = float (i) / n, u1 = float (i + 1) / n;
        const float v0 = float (j) / m, v1 = float (j + 1) / m;
        const Vec3 p00 = at (u0, v0), p10 = at (u1, v0);
        const Vec3 p11 = at (u1, v1), p01 = at (u0, v1);
        Vec3 area = cross (p11 - p00, p01 - p10) * 0.5f;
        const float size = length (area);
        if (size < 1e-6f)
          continue;
        HullPanel p;
        p.centre = (p00 + p10 + p11 + p01) * 0.25f;
        p.normal = area / size;
        if (dot (p.normal, p.centre - inside) < 0.0f)
          p.normal = -p.normal;
        p.area = size;
        p.half_u = ((p10 + p11) - (p00 + p01)) * 0.25f;
        p.half_v = ((p01 + p11) - (p00 + p10)) * 0.25f;
        p.transom = transom;
        out.push_back (p);
      }
  }

  float press_hull (b3BodyId body,
                    std::span<const HullPanel> panels,
                    const BoatWater& water,
                    const HullWaterTraits& traits) {
    constexpr float rho = water_density;
    constexpr float g = water_gravity;
    const b3Quat q = b3Body_GetRotation (body);
    const Vec3 origin = from_b3 (b3Body_GetPosition (body));
    const auto axis = [&] (const Vec3& local) {
      return from_b3 (b3RotateVector (q, to_b3 (local)));
    };
    const auto point = [&] (const Vec3& local) {
      return origin + axis (local);
    };
    const Vec3 up_local = from_b3 (b3InvRotateVector (q, { 0.0f, 1.0f, 0.0f }));

    float wetted = 0.0f;
    for (const HullPanel& p : panels) {
      const Vec3 c = point (p.centre);
      const float level = water.level (c[0], c[2]);
      const float centre_depth = level - c[1];
      // How far the panel reaches up and down about its middle, and so
      // how much of it is under and how deep that part is on average.
      const float reach = std::abs (dot (p.half_u, up_local)) +
                          std::abs (dot (p.half_v, up_local));
      float wet, mean;
      if (reach < 1e-3f) {
        wet = centre_depth > 0.0f ? 1.0f : 0.0f;
        mean = centre_depth;
      } else if (centre_depth >= reach) {
        wet = 1.0f;
        mean = centre_depth;
      } else {
        wet = std::clamp ((centre_depth + reach) / (2.0f * reach), 0.0f, 1.0f);
        mean = 0.5f * (centre_depth + reach);
      }
      if (wet <= 0.0f)
        continue;
      const float area = p.area * wet;
      wetted += area;
      const Vec3 n = axis (p.normal);
      const Vec3 moving =
        from_b3 (b3Body_GetLocalPointVelocity (body, to_b3 (p.centre))) -
        water.current (c[0], c[2]);
      const float vn = dot (moving, n);
      const float through = length (moving);
      // Over the gunwale the water floods in, and pushes on the panel
      // from inside as hard as from out: the head that buoys it is at
      // most the height of the sheer over it. Without this a swamped
      // open hull, as if sealed, is pushed up the harder the deeper it
      // sinks.
      float head = std::max (mean, 0.0f);
      if (p.open) {
        const float flood = point (p.rim)[1] - c[1];
        head = std::clamp (mean, 0.0f, std::max (flood, 0.0f));
      }
      float pressure = rho * g * head;
      // The bobbing is damped up and down, not along the way.
      pressure += rho * traits.damping * std::abs (p.normal[1]) * vn;
      if (vn > 0.0f)
        pressure += 0.5f * rho * traits.ram * through * vn;
      else if (!p.transom)
        pressure += 0.5f * rho * traits.suction * through * vn;
      pressure = std::max (pressure, -0.5f * rho * g);
      Vec3 force = n * (-pressure * area);
      const Vec3 along = moving - n * vn;
      force =
        force - along * (0.5f * rho * traits.friction * length (along) * area);
      b3Body_ApplyForce (body, to_b3 (force), to_b3 (c), true);
    }
    return wetted;
  }
}
