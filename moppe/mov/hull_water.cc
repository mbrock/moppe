#include <moppe/mov/box3d_vec.hh>
#include <moppe/mov/hull_water.hh>

#include <algorithm>
#include <cmath>

namespace moppe::mov {
  void add_hull_quad (std::vector<HullPanel>& out,
                      const displacement_t& a,
                      const displacement_t& b,
                      const displacement_t& c,
                      const displacement_t& d,
                      int n,
                      int m,
                      const displacement_t& inside,
                      bool transom) {
    const proportion_t whole = 1.0f * proportion[one];
    const auto share = [] (const displacement_t& x, proportion_t s) {
      return quantity_cast<isq::displacement> (x * s);
    };
    const auto at = [&] (proportion_t u, proportion_t v) {
      return share (share (a, whole - u) + share (b, u), whole - v) +
             share (share (d, whole - u) + share (c, u), v);
    };
    for (int i = 0; i < n; ++i)
      for (int j = 0; j < m; ++j) {
        const proportion_t u0 = float (i) / n * proportion[one];
        const proportion_t u1 = float (i + 1) / n * proportion[one];
        const proportion_t v0 = float (j) / m * proportion[one];
        const proportion_t v1 = float (j + 1) / m * proportion[one];
        const displacement_t p00 = at (u0, v0), p10 = at (u1, v0);
        const displacement_t p11 = at (u1, v1), p01 = at (u0, v1);
        const auto area = cross (p11 - p00, p01 - p10) * 0.5f;
        const square_meters_t size =
          length (area.numerical_value_in (u::m2)) * u::m2;
        if (size < 1e-6f * u::m2)
          continue;
        HullPanel p;
        p.centre = (p00 + p10 + p11 + p01) * 0.25f;
        p.normal = quantity_cast<direction> (area / size);
        if (dot (p.normal, p.centre - inside) < 0.0f * u::m)
          p.normal = -p.normal;
        p.area = size;
        p.half_u = ((p10 + p11) - (p00 + p01)) * 0.25f;
        p.half_v = ((p01 + p11) - (p00 + p10)) * 0.25f;
        p.transom = transom;
        out.push_back (p);
      }
  }

  square_meters_t press_hull (b3BodyId body,
                              std::span<const HullPanel> panels,
                              const BoatWater& water,
                              const HullWaterTraits& traits) {
    constexpr auto rho = water_density;
    constexpr auto g = standard_gravity;
    constexpr meters_t dry = 0.0f * u::m;
    constexpr proportion_t none = 0.0f * proportion[one];
    constexpr proportion_t whole = 1.0f * proportion[one];
    const b3Quat q = b3Body_GetRotation (body);
    const position_t origin = body_position (body);
    const auto point = [&] (const displacement_t& local) {
      return origin + quantity_cast<isq::position_vector> (turned (q, local));
    };
    const direction_t up_local =
      unturned (q, unit_direction (Vec3 (0.0f, 1.0f, 0.0f)));

    square_meters_t wetted = 0.0f * u::m * u::m;
    for (const HullPanel& p : panels) {
      const position_t c = point (p.centre);
      const meters_t level = water.level (c);
      const meters_t centre_depth = level - height_of (c);
      // How far the panel reaches up and down about its middle, and so
      // how much of it is under and how deep that part is on average.
      const meters_t reach =
        abs (quantity_cast<isq::length> (dot (p.half_u, up_local))) +
        abs (quantity_cast<isq::length> (dot (p.half_v, up_local)));
      proportion_t wet;
      meters_t mean;
      if (reach < 1e-3f * u::m) {
        wet = centre_depth > dry ? whole : none;
        mean = centre_depth;
      } else if (centre_depth >= reach) {
        wet = whole;
        mean = centre_depth;
      } else {
        wet = std::clamp (
          quantity_cast<proportion> ((centre_depth + reach) / (2.0f * reach)),
          none,
          whole);
        mean = 0.5f * (centre_depth + reach);
      }
      if (wet <= none)
        continue;
      const square_meters_t area = quantity_cast<isq::area> (p.area * wet);
      wetted += area;
      const direction_t n = turned (q, p.normal);
      const velocity_t moving =
        point_velocity (body, p.centre) - water.current (c);
      const speed_t vn = quantity_cast<isq::speed> (dot (moving, n));
      const speed_t through = length (velocity_value (moving)) * u::m / u::s;
      // Over the gunwale the water floods in, and pushes on the panel
      // from inside as hard as from out: the head that buoys it is at
      // most the height of the sheer over it. Without this a swamped
      // open hull, as if sealed, is pushed up the harder the deeper it
      // sinks.
      meters_t head = std::max (mean, dry);
      if (p.open) {
        const meters_t flood = height_of (point (p.rim)) - height_of (c);
        head = std::clamp (mean, dry, std::max (flood, dry));
      }
      pascals_t pressure = rho * g * head;
      // The bobbing is damped up and down, not along the way.
      pressure += rho * traits.damping * std::abs (direction_value (n)[1]) * vn;
      if (vn > 0.0f * u::m / u::s)
        pressure +=
          quantity_cast<isq::pressure> (0.5f * rho * traits.ram * through * vn);
      else if (!p.transom)
        pressure += quantity_cast<isq::pressure> (0.5f * rho * traits.suction *
                                                  through * vn);
      // The water lets go of a panel drawing away from it before it
      // pulls harder than half a metre's head.
      pressure = std::max (pressure, pascals_t (-0.5f * rho * g * one_meter));
      force_t force = quantity_cast<isq::force> (n * (-pressure * area));
      const velocity_t along =
        moving - quantity_cast<isq::velocity> (n * dot (moving, n));
      force =
        force -
        quantity_cast<isq::force> (
          along * (0.5f * rho * traits.friction *
                   (length (velocity_value (along)) * u::m / u::s) * area));
      apply_force (body, force, c);
    }
    return wetted;
  }
}
