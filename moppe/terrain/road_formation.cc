#include <moppe/terrain/roads.hh>

#include <moppe/parallel.hh>
#include <moppe/profile.hh>
#include <moppe/terrain/river.hh>

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <limits>
#include <span>
#include <tuple>
#include <utility>
#include <vector>

// Building the planned roads into the ground. Each road's tread follows a
// height profile solved along its line, and the ground round it is set by
// its cross-section at the distance from the line: the formation where the
// tread is, the batters beyond, the natural ground where they meet it. The
// river beds (channel.cc) are cut the same way.
namespace moppe::terrain {
  namespace {
    constexpr float no_height = std::numeric_limits<float>::infinity ();

    float seam_delta (float delta, float period) {
      return delta - period * std::round (delta / period);
    }

    struct FormationGround {
      const TerrainDomain& domain;
      std::span<const float> heights;
      int width;
      int height;
      float spacing_x;
      float spacing_z;

      std::size_t cell (int x, int z) const {
        return static_cast<std::size_t> (wrap_index (z, height)) * width +
               static_cast<std::size_t> (wrap_index (x, width));
      }

      // Bilinear, as the renderer and physics read it.
      float at (float x_m, float z_m) const {
        const float gx = x_m / spacing_x;
        const float gz = z_m / spacing_z;
        const float fx = std::floor (gx);
        const float fz = std::floor (gz);
        const int x = static_cast<int> (fx);
        const int z = static_cast<int> (fz);
        const float tx = gx - fx;
        const float tz = gz - fz;
        const float north =
          std::lerp (heights[cell (x, z)], heights[cell (x + 1, z)], tx);
        const float south = std::lerp (
          heights[cell (x, z + 1)], heights[cell (x + 1, z + 1)], tx);
        return std::lerp (north, south, tz);
      }

      std::size_t nearest_cell (float x_m, float z_m) const {
        return cell (static_cast<int> (std::lround (x_m / spacing_x)),
                     static_cast<int> (std::lround (z_m / spacing_z)));
      }
    };

    // Bounds every value within `grade` per metre of each bound given:
    // below by the highest of `lower`'s cones, above by the lowest of
    // `upper`'s. Two passes, as each cone only widens along the line.
    void cones (std::span<const float> along,
                std::vector<float>& lower,
                std::vector<float>& upper,
                float grade) {
      const std::size_t n = along.size ();
      for (std::size_t i = 1; i < n; ++i) {
        const float rise = grade * (along[i] - along[i - 1]);
        lower[i] = std::max (lower[i], lower[i - 1] - rise);
        upper[i] = std::min (upper[i], upper[i - 1] + rise);
      }
      for (std::size_t i = n - 1; i-- > 0;) {
        const float rise = grade * (along[i + 1] - along[i]);
        lower[i] = std::max (lower[i], lower[i + 1] - rise);
        upper[i] = std::min (upper[i], upper[i + 1] + rise);
      }
    }

    // A mean over `half` metres either way along the line, nearer the ends
    // over less. Averaging keeps any bound on the grade, so the eased
    // profile climbs no steeper than the one it eases.
    std::vector<float> ease_profile (std::span<const float> along,
                                     std::span<const float> values,
                                     float half) {
      const std::size_t n = values.size ();
      std::vector<float> result (n);
      std::size_t first = 0;
      std::size_t last = 0;
      double sum = 0.0;
      for (std::size_t i = 0; i < n; ++i) {
        const float reach = std::min (
          { half, along[i] - along.front (), along.back () - along[i] });
        while (last < n && along[last] <= along[i] + reach + 1e-3f)
          sum += values[last++];
        while (first < last && along[first] < along[i] - reach - 1e-3f)
          sum -= values[first++];
        result[i] =
          static_cast<float> (sum / static_cast<double> (last - first));
      }
      return result;
    }

    // The profile of one cart road over its ground: as near the ground as
    // a grade of `maximum_grade` allows (the midpoint of the tightest
    // bounds from above and below, which is the nearest such profile in
    // its worst point), eased over the vertical curves, held within the
    // cut and fill allowed, and through every pinned height.
    std::vector<float> solve_profile (std::span<const float> along,
                                      std::span<const float> ground,
                                      std::span<const float> pins,
                                      const RoadFormation& parameters) {
      const std::size_t n = ground.size ();
      const float grade = parameters.maximum_grade;
      std::vector<float> lower (ground.begin (), ground.end ());
      std::vector<float> upper (ground.begin (), ground.end ());
      cones (along, lower, upper, grade);
      std::vector<float> profile (n);
      for (std::size_t i = 0; i < n; ++i)
        profile[i] = 0.5f * (lower[i] + upper[i]);
      profile = ease_profile (along, profile, 0.5f * parameters.vertical_curve);
      // The cut and fill allowed, as bounds that themselves keep the
      // grade: clamping into them keeps it too. Where a hump or a hollow
      // asks for more than they allow, the two cross, and the road cuts or
      // fills between them, deeper than allowed but at its grade.
      std::vector<float> deepest (n);
      std::vector<float> highest (n);
      for (std::size_t i = 0; i < n; ++i) {
        deepest[i] = ground[i] - parameters.maximum_cut;
        highest[i] = ground[i] + parameters.maximum_fill;
      }
      cones (along, deepest, highest, grade);
      for (std::size_t i = 0; i < n; ++i)
        profile[i] = std::clamp (profile[i],
                                 std::min (deepest[i], highest[i]),
                                 std::max (deepest[i], highest[i]));

      std::vector<float> pin_lower (n, -no_height);
      std::vector<float> pin_upper (n, no_height);
      bool pinned = false;
      for (std::size_t i = 0; i < n; ++i)
        if (std::isfinite (pins[i])) {
          pin_lower[i] = pin_upper[i] = pins[i];
          pinned = true;
        }
      if (pinned) {
        cones (along, pin_lower, pin_upper, grade);
        for (std::size_t i = 0; i < n; ++i)
          profile[i] = pin_lower[i] <= pin_upper[i]
                         ? std::clamp (profile[i], pin_lower[i], pin_upper[i])
                         : 0.5f * (pin_lower[i] + pin_upper[i]);
      }
      return profile;
    }

    // An earlier road's tread height at the point of it nearest `at`, if
    // it passes within `reach`.
    float height_on (const Road& road,
                     RoadPoint at,
                     float period_x,
                     float period_z,
                     float reach) {
      float best = no_height;
      float height = no_height;
      for (std::size_t i = 1; i < road.points.size (); ++i) {
        const float ax = seam_delta (road.points[i - 1].x_m - at.x_m, period_x);
        const float az = seam_delta (road.points[i - 1].z_m - at.z_m, period_z);
        const float dx = road.points[i].x_m - road.points[i - 1].x_m;
        const float dz = road.points[i].z_m - road.points[i - 1].z_m;
        const float length2 = dx * dx + dz * dz;
        const float t =
          length2 > 0.0f
            ? std::clamp (-(ax * dx + az * dz) / length2, 0.0f, 1.0f)
            : 0.0f;
        const float distance = std::hypot (ax + dx * t, az + dz * t);
        if (distance < best) {
          best = distance;
          height = std::lerp (road.heights[i - 1], road.heights[i], t);
        }
      }
      return best <= reach ? height : no_height;
    }

    // What the nearest road makes of one cell.
    struct FormationClaim {
      float distance = no_height;
      float height = 0.0f;
      bool built = false;
      // Under a bridge's span, which leaves the ground as it is.
      bool bridge = false;
      // How far along the road from its nearest culvert's line.
      float culvert = no_height;
    };
  }

  RoadFormationResult form_roads (const TerrainDomain& domain,
                                  std::span<const SurfaceElevation> elevations,
                                  RoadNetwork& network,
                                  const FloodField& flood,
                                  const DrainageGraph& drainage,
                                  const RoadFormation& parameters) {
    MOPPE_PROFILE_ZONE ("form_roads");
    const FormationGround ground {
      domain,
      surface_elevation_values (elevations),
      static_cast<int> (domain.width ()),
      static_cast<int> (domain.height ()),
      domain.spacing_x ().numerical_value_in (u::m),
      domain.spacing_z ().numerical_value_in (u::m)
    };
    const float period_x = ground.width * ground.spacing_x;
    const float period_z = ground.height * ground.spacing_z;
    const auto crosses_water = [&] (std::size_t cell) {
      return flood.water_depth_m (cell) > 1e-3f ||
             drainage.contributing_area_at (cell).numerical_value_in (
               u::m * u::m) >= parameters.culvert_area_m2;
    };

    RoadFormationResult result;
    // The profiles, in order.
    for (std::size_t r = 0; r < network.roads.size (); ++r) {
      Road& road = network.roads[r];
      const std::size_t n = road.points.size ();
      std::vector<float> along (n, 0.0f);
      std::vector<float> natural (n);
      for (std::size_t i = 0; i < n; ++i) {
        if (i > 0)
          along[i] = along[i - 1] +
                     std::hypot (road.points[i].x_m - road.points[i - 1].x_m,
                                 road.points[i].z_m - road.points[i - 1].z_m);
        natural[i] = ground.at (road.points[i].x_m, road.points[i].z_m);
      }
      if (road.kind == Road::Kind::Footpath || n < 2) {
        road.heights = std::move (natural);
        continue;
      }
      std::vector<float> pins (n, no_height);

      // The rivers it bridges. A river's line is a cell wide and the
      // points four metres apart, so each point reads the most any cell
      // within two of it drains.
      std::vector<float> drains (n, 0.0f);
      for (std::size_t i = 0; i < n; ++i) {
        const int cx = static_cast<int> (
          std::lround (road.points[i].x_m / ground.spacing_x));
        const int cz = static_cast<int> (
          std::lround (road.points[i].z_m / ground.spacing_z));
        for (int dz = -2; dz <= 2; ++dz)
          for (int dx = -2; dx <= 2; ++dx)
            drains[i] = std::max (
              drains[i],
              drainage.contributing_area_at (ground.cell (cx + dx, cz + dz))
                .numerical_value_in (u::m * u::m));
      }
      road.bridges.clear ();
      for (std::size_t i = 0; i < n;) {
        if (drains[i] < parameters.bridge_area_m2) {
          ++i;
          continue;
        }
        std::size_t j = i;
        while (j + 1 < n && drains[j + 1] >= parameters.bridge_area_m2)
          ++j;
        float widest = 0.0f;
        float bed = no_height;
        for (std::size_t k = i; k <= j; ++k) {
          widest = std::max (widest, drains[k]);
          bed = std::min (bed, natural[k]);
        }
        const square_meters_t area = widest * u::m * u::m;
        // A road that keeps beside a river for longer than it takes to
        // cross it runs along its bank, not over it.
        if (along[j] - along[i] >
            river_width (area).numerical_value_in (u::m) + 16.0f +
              4.0f * std::max (ground.spacing_x, ground.spacing_z)) {
          i = j + 1;
          continue;
        }
        // Clear of the spate, its banks a little beyond its bed's width
        // and wherever the ground still lies below the deck.
        const float deck = bed + river_depth (area).numerical_value_in (u::m) +
                           parameters.clearance;
        const float bank =
          0.5f * river_width (area).numerical_value_in (u::m) + 2.0f;
        // Over a floodplain lower than the deck, too wide for one span,
        // the bridge keeps to the river and its banks, and the road comes
        // to it on embankments.
        const auto abutments = [&] (bool low_ground) {
          const auto reaches = [&] (std::size_t k, std::size_t from) {
            return std::fabs (along[k] - along[from]) < bank ||
                   (low_ground && natural[k] < deck - 0.3f);
          };
          std::size_t a = i;
          while (a > 0 && along[i] - along[a - 1] <= parameters.bridge_reach &&
                 reaches (a, i))
            --a;
          std::size_t b = j;
          while (b + 1 < n &&
                 along[b + 1] - along[j] <= parameters.bridge_reach &&
                 reaches (b, j))
            ++b;
          return std::pair (a, b);
        };
        auto [a, b] = abutments (true);
        const bool chained =
          !road.bridges.empty () && a <= road.bridges.back ().last;
        if (along[b] - along[a] > parameters.longest_bridge ||
            (chained && along[b] - along[road.bridges.back ().first] >
                          parameters.longest_bridge))
          std::tie (a, b) = abutments (false);
        if (a == 0 || b + 1 == n) {
          // A river at the road's very end is the place it leads to, not
          // a crossing.
          i = b + 1;
          continue;
        }
        // Two crossings close together (a meander's) share one span, if
        // one span can reach over both.
        if (!road.bridges.empty () && a <= road.bridges.back ().last &&
            along[b] - along[road.bridges.back ().first] <=
              parameters.longest_bridge)
          road.bridges.back ().last = static_cast<std::uint32_t> (b);
        else if (!road.bridges.empty () && a <= road.bridges.back ().last)
          road.bridges.push_back (
            { road.bridges.back ().last, static_cast<std::uint32_t> (b) });
        else
          road.bridges.push_back (
            { static_cast<std::uint32_t> (a), static_cast<std::uint32_t> (b) });
        i = b + 1;
      }
      // Short stretches beside a river joined into one span are the road
      // keeping to its bank; no cart road bridges further than the widest
      // river and its banks.
      std::erase_if (road.bridges, [&] (const RoadBridge& bridge) {
        return along[bridge.last] - along[bridge.first] >
               parameters.longest_bridge;
      });
      // Each deck runs straight between its abutments, held clear of the
      // spate but never below a bank that stands higher.
      for (const RoadBridge& bridge : road.bridges) {
        float lowest = no_height;
        for (std::size_t k = bridge.first; k <= bridge.last; ++k)
          lowest = std::min (lowest, natural[k]);
        float widest = 0.0f;
        for (std::size_t k = bridge.first; k <= bridge.last; ++k)
          widest = std::max (widest, drains[k]);
        const float deck =
          lowest +
          river_depth (widest * u::m * u::m).numerical_value_in (u::m) +
          parameters.clearance;
        const float from = std::max (deck, natural[bridge.first]);
        const float to = std::max (deck, natural[bridge.last]);
        const float span = along[bridge.last] - along[bridge.first];
        for (std::size_t k = bridge.first; k <= bridge.last; ++k)
          pins[k] = std::lerp (
            from,
            to,
            span > 0.0f ? (along[k] - along[bridge.first]) / span : 0.0f);
        ++result.bridges;
      }
      // Each stretch over smaller water, a stream or a pond, is an
      // embankment over a culvert: the tread held a freeboard over the
      // water in spate, and over the pipe laid on the lowest of its bed.
      // A short crossing is level over its water; a long one follows it.
      road.culverts.clear ();
      std::vector<bool> spanned (n, false);
      for (const RoadBridge& bridge : road.bridges)
        for (std::size_t k = bridge.first; k <= bridge.last; ++k)
          spanned[k] = true;
      const auto water_at = [&] (std::size_t k) {
        const std::size_t cell =
          ground.nearest_cell (road.points[k].x_m, road.points[k].z_m);
        const float still = flood.water_depth_m (cell);
        const square_meters_t area = drainage.contributing_area_at (cell);
        return std::pair (
          still > 1e-3f ? still : river_depth (area).numerical_value_in (u::m),
          area.numerical_value_in (u::m * u::m));
      };
      for (std::size_t i = 0; i < n;) {
        if (spanned[i] || !crosses_water (ground.nearest_cell (
                            road.points[i].x_m, road.points[i].z_m))) {
          ++i;
          continue;
        }
        std::size_t j = i;
        while (j + 1 < n && !spanned[j + 1] &&
               crosses_water (ground.nearest_cell (road.points[j + 1].x_m,
                                                   road.points[j + 1].z_m)))
          ++j;
        std::size_t lowest = i;
        float surface = -no_height;
        float drains_most = 0.0f;
        for (std::size_t k = i; k <= j; ++k) {
          const auto [depth, area] = water_at (k);
          if (natural[k] < natural[lowest])
            lowest = k;
          surface = std::max (surface, natural[k] + depth);
          drains_most = std::max (drains_most, area);
        }
        const float diameter = std::clamp (
          1.2f * river_depth (drains_most * u::m * u::m).numerical_value_in (u::m),
          0.6f,
          1.8f);
        const float pipe_top = natural[lowest] + diameter + 0.3f;
        const bool level = along[j] - along[i] <= 40.0f;
        for (std::size_t k = i; k <= j; ++k)
          pins[k] = level ? std::max (surface + parameters.freeboard, pipe_top)
                          : std::max (natural[k] + water_at (k).first +
                                        parameters.freeboard,
                                      natural[k] + 0.9f);
        road.culverts.push_back (
          { static_cast<std::uint32_t> (lowest), natural[lowest], diameter });
        i = j + 1;
      }
      for (const std::size_t end : { std::size_t { 0 }, n - 1 })
        for (std::size_t earlier = 0; earlier < r; ++earlier)
          if (network.roads[earlier].kind == Road::Kind::Road) {
            const float height = height_on (network.roads[earlier],
                                            road.points[end],
                                            period_x,
                                            period_z,
                                            2.0f);
            if (std::isfinite (height)) {
              pins[end] = height;
              break;
            }
          }
      road.heights = solve_profile (along, natural, pins, parameters);
      for (std::size_t i = 1; i < n; ++i) {
        ++result.points;
        if (std::fabs (road.heights[i] - road.heights[i - 1]) >
            (parameters.maximum_grade + 1e-3f) * (along[i] - along[i - 1]))
          ++result.steep_points;
      }
    }

    // Each cell takes the cross-section of the nearest road at its
    // distance from that road's line. On a coarse lattice the formation
    // keeps at least a cell's width, so the tread stays continuous.
    const std::size_t count = domain.size ();
    std::vector<FormationClaim> claims (count);
    const float tread = parameters.half_width + parameters.shoulder;
    const float spacing = std::max (ground.spacing_x, ground.spacing_z);
    const float formation = std::max (tread, 1.01f * spacing);
    const float path_reach =
      std::max (parameters.footpath_half_width, 0.75f * spacing) + 1.2f;
    constexpr float batter_reach = 24.0f;
    constexpr float blend = 4.0f;
    for (const Road& road : network.roads) {
      const bool built = road.kind == Road::Kind::Road;
      const float reach = built ? formation + batter_reach : path_reach;
      std::vector<bool> spans (road.points.size (), false);
      for (const RoadBridge& bridge : road.bridges)
        for (std::size_t k = bridge.first + 1; k <= bridge.last; ++k)
          spans[k] = true;
      std::vector<float> along (road.points.size (), 0.0f);
      for (std::size_t i = 1; i < road.points.size (); ++i)
        along[i] = along[i - 1] +
                   std::hypot (road.points[i].x_m - road.points[i - 1].x_m,
                               road.points[i].z_m - road.points[i - 1].z_m);
      for (std::size_t i = 1; i < road.points.size (); ++i) {
        const RoadPoint a = road.points[i - 1];
        const RoadPoint b = road.points[i];
        const float dx = b.x_m - a.x_m;
        const float dz = b.z_m - a.z_m;
        const float length2 = dx * dx + dz * dz;
        const int x0 = static_cast<int> (
          std::floor ((std::min (a.x_m, b.x_m) - reach) / ground.spacing_x));
        const int x1 = static_cast<int> (
          std::ceil ((std::max (a.x_m, b.x_m) + reach) / ground.spacing_x));
        const int z0 = static_cast<int> (
          std::floor ((std::min (a.z_m, b.z_m) - reach) / ground.spacing_z));
        const int z1 = static_cast<int> (
          std::ceil ((std::max (a.z_m, b.z_m) + reach) / ground.spacing_z));
        for (int z = z0; z <= z1; ++z)
          for (int x = x0; x <= x1; ++x) {
            const float px = x * ground.spacing_x - a.x_m;
            const float pz = z * ground.spacing_z - a.z_m;
            const float t =
              length2 > 0.0f
                ? std::clamp ((px * dx + pz * dz) / length2, 0.0f, 1.0f)
                : 0.0f;
            const float distance = std::hypot (px - dx * t, pz - dz * t);
            if (distance > reach)
              continue;
            FormationClaim& claim = claims[ground.cell (x, z)];
            // A built road's claim outranks a footpath's however near.
            if ((built && !claim.built) ||
                (built == claim.built && distance < claim.distance))
            {
              const float here = std::lerp (along[i - 1], along[i], t);
              float culvert = no_height;
              for (const RoadCulvert& c : road.culverts)
                culvert = std::min (culvert, std::fabs (here - along[c.at]));
              claim = { distance,
                        std::lerp (road.heights[i - 1], road.heights[i], t),
                        built,
                        spans[i],
                        culvert };
            }
          }
      }
    }

    const std::span<const float> original = ground.heights;
    result.heights.assign (elevations.begin (), elevations.end ());
    result.change_m.assign (count, 0.0f);
    result.influence.assign (count, 0.0f);
    const float cell_area = ground.spacing_x * ground.spacing_z;
    const auto smooth = [] (float t) {
      t = std::clamp (t, 0.0f, 1.0f);
      return t * t * (3.0f - 2.0f * t);
    };
    for (std::size_t cell = 0; cell < count; ++cell) {
      const FormationClaim& claim = claims[cell];
      if (!std::isfinite (claim.distance) || claim.bridge)
        continue;
      const float d = claim.distance;
      if (!claim.built && flood.water_depth_m (cell) > 1e-3f)
        continue;
      if (!claim.built) {
        const float half =
          std::max (parameters.footpath_half_width, 0.75f * spacing);
        result.influence[cell] = 1.0f - smooth ((d - half) / 1.2f);
        continue;
      }
      result.influence[cell] =
        1.0f - smooth ((d - std::max (tread, 0.75f * spacing)) / 1.5f);
      // The embankment is built through standing water and over a
      // stream, but along a culvert's line the ground is left as it was,
      // for the water to pass under the tread; and a stream beside the
      // tread keeps its channel through the batters.
      if (claim.culvert < 0.75f * spacing)
        continue;
      if (d > formation &&
          drainage.contributing_area_at (cell).numerical_value_in (
            u::m * u::m) >= parameters.culvert_area_m2)
        continue;
      const float g = original[cell];
      const float edge =
        claim.height - parameters.crown * parameters.half_width;
      float shaped = 0.0f;
      if (d <= formation) {
        shaped =
          claim.height - parameters.crown * std::min (d, parameters.half_width);
      } else {
        const float out = d - formation;
        shaped = std::clamp (g,
                             edge - parameters.fill_slope * out,
                             edge + parameters.cut_slope * out);
        // The last few metres of reach ease_profile back to the ground, so
        // where a batter has not met it no step is left.
        shaped = std::lerp (
          shaped, g, smooth ((out - (batter_reach - blend)) / blend));
      }
      const float change = shaped - g;
      if (std::fabs (change) < 1e-4f)
        continue;
      result.heights[cell] = surface_elevation_point (shaped * u::m);
      result.change_m[cell] = change;
      (change < 0.0f ? result.cut_m3 : result.fill_m3) +=
        std::fabs (change) * cell_area;
    }
    return result;
  }
}
