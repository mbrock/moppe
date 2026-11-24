#include <moppe/terrain/roads.hh>

#include <moppe/correct_math.hh>
#include <moppe/gfx/signal.hh>
#include <moppe/parallel.hh>
#include <moppe/profile.hh>
#include <moppe/terrain/river.hh>
#include <moppe/terrain/torus.hh>

#include <algorithm>
#include <array>
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
    // No distance yet: nothing claims the cell, no culvert is near.
    constexpr meters_t far = std::numeric_limits<float>::infinity () * u::m;
    // Over and under every height: no pin, no floor.
    constexpr SurfaceElevation above_all { far };
    constexpr SurfaceElevation below_all { -far };

    bool finite (SurfaceElevation height) {
      return std::isfinite (surface_elevation_value (height));
    }

    // Overloads moppe::lerp rather than hiding it: unity builds put this
    // file before others in moppe::terrain that call the general one.
    using moppe::lerp;

    SurfaceElevation
    lerp (SurfaceElevation from, SurfaceElevation to, blend_weight_t t) {
      return surface_elevation_point (std::lerp (surface_elevation_value (from),
                                                 surface_elevation_value (to),
                                                 t.numerical_value_in (one)) *
                                      u::m);
    }

    meters_t lerp (meters_t from, meters_t to, blend_weight_t t) {
      return std::lerp (meters_value (from),
                        meters_value (to),
                        t.numerical_value_in (one)) *
             u::m;
    }

    // Half way between two heights, as 0.5 (a + b).
    SurfaceElevation midpoint (SurfaceElevation a, SurfaceElevation b) {
      return SurfaceElevation (0.5f *
                               (a.delta_from_zero () + b.delta_from_zero ()));
    }

    // How far a grade climbs over a run: a grade is height over length,
    // so over a length it is a height, here in the ground's frame.
    ElevationChange rise (grade_t grade, meters_t run) {
      return quantity_cast<surface_elevation> (isq::height (grade * run));
    }

    struct FormationGround {
      const TerrainDomain& domain;
      std::span<const SurfaceElevation> elevations;
      int width;
      int height;
      meters_t spacing_x;
      meters_t spacing_z;

      std::size_t cell (int x, int z) const {
        return static_cast<std::size_t> (wrap_index (z, height)) * width +
               static_cast<std::size_t> (wrap_index (x, width));
      }

      // A lane's length in lattice steps, the boundary into cells.
      float steps_x (meters_t x) const {
        return (x / spacing_x).numerical_value_in (one);
      }

      float steps_z (meters_t z) const {
        return (z / spacing_z).numerical_value_in (one);
      }

      // Bilinear, as the renderer and physics read it.
      SurfaceElevation at (plan_position_t point) const {
        const float gx = steps_x (x_of (point));
        const float gz = steps_z (z_of (point));
        const float fx = std::floor (gx);
        const float fz = std::floor (gz);
        const int x = static_cast<int> (fx);
        const int z = static_cast<int> (fz);
        const float tx = gx - fx;
        const float tz = gz - fz;
        const auto value = [&] (int cx, int cz) {
          return surface_elevation_value (elevations[cell (cx, cz)]);
        };
        const float north = std::lerp (value (x, z), value (x + 1, z), tx);
        const float south =
          std::lerp (value (x, z + 1), value (x + 1, z + 1), tx);
        return surface_elevation_point (std::lerp (north, south, tz) * u::m);
      }

      // The lattice point nearest a point, column then row.
      std::array<int, 2> nearest (plan_position_t point) const {
        return { static_cast<int> (std::lround (steps_x (x_of (point)))),
                 static_cast<int> (std::lround (steps_z (z_of (point)))) };
      }

      std::size_t nearest_cell (plan_position_t point) const {
        const auto [x, z] = nearest (point);
        return cell (x, z);
      }
    };

    // Bounds every value within `grade` per metre of each bound given:
    // below by the highest of `lower`'s cones, above by the lowest of
    // `upper`'s. Two passes, as each cone only widens along the line.
    void cones (std::span<const meters_t> along,
                std::vector<SurfaceElevation>& lower,
                std::vector<SurfaceElevation>& upper,
                grade_t grade) {
      const std::size_t n = along.size ();
      for (std::size_t i = 1; i < n; ++i) {
        const ElevationChange climb = rise (grade, along[i] - along[i - 1]);
        lower[i] = std::max (lower[i], lower[i - 1] - climb);
        upper[i] = std::min (upper[i], upper[i - 1] + climb);
      }
      for (std::size_t i = n - 1; i-- > 0;) {
        const ElevationChange climb = rise (grade, along[i + 1] - along[i]);
        lower[i] = std::max (lower[i], lower[i + 1] - climb);
        upper[i] = std::min (upper[i], upper[i + 1] + climb);
      }
    }

    // A mean over `half` either way along the line, nearer the ends over
    // less. Averaging keeps any bound on the grade, so the eased profile
    // climbs no steeper than the one it eases.
    std::vector<SurfaceElevation>
    ease_profile (std::span<const meters_t> along,
                  std::span<const SurfaceElevation> values,
                  meters_t half) {
      const std::size_t n = values.size ();
      std::vector<SurfaceElevation> result (n);
      std::size_t first = 0;
      std::size_t last = 0;
      // Metres above the datum, summed in double.
      double sum = 0.0;
      for (std::size_t i = 0; i < n; ++i) {
        const meters_t reach = std::min (
          { half, along[i] - along.front (), along.back () - along[i] });
        while (last < n && along[last] <= along[i] + reach + 1e-3f * u::m)
          sum += surface_elevation_value (values[last++]);
        while (first < last && along[first] < along[i] - reach - 1e-3f * u::m)
          sum -= surface_elevation_value (values[first++]);
        result[i] = surface_elevation_point (
          static_cast<float> (sum / static_cast<double> (last - first)) * u::m);
      }
      return result;
    }

    // The profile of one cart road over its ground: as near the ground as
    // a grade of `maximum_grade` allows (the midpoint of the tightest
    // bounds from above and below, which is the nearest such profile in
    // its worst point), eased over the vertical curves, held within the
    // cut and fill allowed, no lower than its floors, and through every
    // pinned height.
    std::vector<SurfaceElevation>
    solve_profile (std::span<const meters_t> along,
                   std::span<const SurfaceElevation> ground,
                   std::span<const SurfaceElevation> pins,
                   std::span<const SurfaceElevation> floors,
                   const RoadFormation& parameters) {
      const std::size_t n = ground.size ();
      const grade_t grade = parameters.maximum_grade;
      std::vector<SurfaceElevation> lower (ground.begin (), ground.end ());
      std::vector<SurfaceElevation> upper (ground.begin (), ground.end ());
      cones (along, lower, upper, grade);
      std::vector<SurfaceElevation> profile (n);
      for (std::size_t i = 0; i < n; ++i)
        profile[i] = midpoint (lower[i], upper[i]);
      profile = ease_profile (along, profile, 0.5f * parameters.vertical_curve);
      // The cut and fill allowed, as bounds that themselves keep the
      // grade: clamping into them keeps it too. Where a hump or a hollow
      // asks for more than they allow, the two cross, and the road cuts or
      // fills between them, deeper than allowed but at its grade.
      std::vector<SurfaceElevation> deepest (n);
      std::vector<SurfaceElevation> highest (n);
      for (std::size_t i = 0; i < n; ++i) {
        deepest[i] = ground[i] - parameters.maximum_cut;
        highest[i] = ground[i] + parameters.maximum_fill;
      }
      cones (along, deepest, highest, grade);
      for (std::size_t i = 0; i < n; ++i)
        profile[i] = std::clamp (profile[i],
                                 std::min (deepest[i], highest[i]),
                                 std::max (deepest[i], highest[i]));

      // No lower than its floors, come up to at the road's grade.
      {
        std::vector<SurfaceElevation> least (floors.begin (), floors.end ());
        std::vector<SurfaceElevation> unbounded (n, above_all);
        cones (along, least, unbounded, grade);
        for (std::size_t i = 0; i < n; ++i)
          profile[i] = std::max (profile[i], least[i]);
      }

      std::vector<SurfaceElevation> pin_lower (n, below_all);
      std::vector<SurfaceElevation> pin_upper (n, above_all);
      bool pinned = false;
      for (std::size_t i = 0; i < n; ++i)
        if (finite (pins[i])) {
          pin_lower[i] = pin_upper[i] = pins[i];
          pinned = true;
        }
      if (pinned) {
        cones (along, pin_lower, pin_upper, grade);
        for (std::size_t i = 0; i < n; ++i)
          profile[i] = pin_lower[i] <= pin_upper[i]
                         ? std::clamp (profile[i], pin_lower[i], pin_upper[i])
                         : midpoint (pin_lower[i], pin_upper[i]);
      }
      return profile;
    }

    // Where along a segment, from 0 at its start to 1 at its end, lies
    // its point nearest a point, given the segment's start from it.
    blend_weight_t nearest_along (plan_displacement_t start,
                                  plan_displacement_t step) {
      const square_meters_t length2 = dot (step, step);
      return (length2 > 0.0f * u::m * u::m
                ? std::clamp (
                    (-dot (start, step) / length2).numerical_value_in (one),
                    0.0f,
                    1.0f)
                : 0.0f) *
             blend_weight[one];
    }

    // An earlier road's tread height at the point of it nearest `at`, if
    // it passes within `reach`.
    SurfaceElevation height_on (const Road& road,
                                plan_position_t at,
                                plan_extent_t period,
                                meters_t reach) {
      meters_t best = far;
      SurfaceElevation height = above_all;
      for (std::size_t i = 1; i < road.points.size (); ++i) {
        const plan_displacement_t start =
          minimum_image (road.points[i - 1] - at, period);
        const plan_displacement_t step = road.points[i] - road.points[i - 1];
        const blend_weight_t t = nearest_along (start, step);
        const meters_t distance =
          hypot (start + step * t.numerical_value_in (one));
        if (distance < best) {
          best = distance;
          height = lerp (road.heights[i - 1], road.heights[i], t);
        }
      }
      return best <= reach ? height : above_all;
    }

    // How near a road's line passes a point.
    meters_t
    offset_from (const Road& road, plan_position_t at, plan_extent_t period) {
      meters_t best = far;
      for (std::size_t i = 1; i < road.points.size (); ++i) {
        const plan_displacement_t start =
          minimum_image (road.points[i - 1] - at, period);
        const plan_displacement_t step = road.points[i] - road.points[i - 1];
        const blend_weight_t t = nearest_along (start, step);
        best =
          std::min (best, hypot (start + step * t.numerical_value_in (one)));
      }
      return best;
    }

    // What the nearest road makes of one cell.
    struct FormationClaim {
      meters_t distance = far;
      SurfaceElevation height {};
      bool built = false;
      // Under a bridge's span, which leaves the ground as it is.
      bool bridge = false;
      // How far along the road from its nearest culvert's line.
      meters_t culvert = far;
    };
  }

  RoadFormationResult form_roads (const TerrainDomain& domain,
                                  std::span<const SurfaceElevation> elevations,
                                  RoadNetwork& network,
                                  const FloodField& flood,
                                  const DrainageGraph& drainage,
                                  const RoadFormation& parameters) {
    MOPPE_PROFILE_ZONE ("form_roads");
    const FormationGround ground { domain,
                                   elevations,
                                   static_cast<int> (domain.width ()),
                                   static_cast<int> (domain.height ()),
                                   domain.spacing_x (),
                                   domain.spacing_z () };
    const plan_extent_t period = plan_extent (ground.width * ground.spacing_x,
                                              ground.height * ground.spacing_z);
    // The water standing in a cell, as a depth.
    const auto standing = [&] (std::size_t cell) -> meters_t {
      return flood.water_depths ()[cell];
    };
    const auto crosses_water = [&] (std::size_t cell) {
      return standing (cell) > 1e-3f * u::m ||
             drainage.contributing_area_at (cell) >= parameters.culvert_area;
    };

    RoadFormationResult result;
    // A cart road ends on dry ground. One planned to a lake's outlet ends
    // on the stream that leaves it, where it would stand as a stub of
    // embankment out in the water: it is cut back to the bank, unless
    // another way meets it there.
    for (std::size_t r = 0; r < network.roads.size (); ++r) {
      Road& road = network.roads[r];
      if (road.kind != Road::Kind::Road || road.loop)
        continue;
      for (const bool back : { false, true }) {
        constexpr std::size_t shortest = 8, most = 15;
        for (std::size_t cut = 0; cut < most && road.points.size () > shortest;
             ++cut) {
          const plan_position_t end =
            back ? road.points.back () : road.points.front ();
          if (!crosses_water (ground.nearest_cell (end)))
            break;
          if (cut == 0) {
            bool met = false;
            for (std::size_t q = 0; q < network.roads.size () && !met; ++q)
              met = q != r &&
                    offset_from (network.roads[q], end, period) < 6.0f * u::m;
            if (met)
              break;
          }
          if (back)
            road.points.pop_back ();
          else
            road.points.erase (road.points.begin ());
        }
      }
    }
    // The ends that will be turning places, whose last few metres are
    // made level.
    std::vector<std::array<bool, 2>> turns (network.roads.size (),
                                            { false, false });
    for (const RoadTurning& turning : road_turnings (network, period))
      turns[turning.road][turning.back ? 1 : 0] = true;
    const meters_t tread = parameters.half_width + parameters.shoulder;
    // The profiles, in order.
    for (std::size_t r = 0; r < network.roads.size (); ++r) {
      Road& road = network.roads[r];
      const std::size_t n = road.points.size ();
      std::vector<meters_t> along (n, 0.0f * u::m);
      std::vector<SurfaceElevation> natural (n);
      for (std::size_t i = 0; i < n; ++i) {
        if (i > 0)
          along[i] = along[i - 1] + hypot (road.points[i] - road.points[i - 1]);
        natural[i] = ground.at (road.points[i]);
      }
      if (road.kind == Road::Kind::Footpath || n < 2) {
        road.heights = std::move (natural);
        continue;
      }
      std::vector<SurfaceElevation> pins (n, above_all);

      // The rivers it bridges. A river's line is a cell wide and the
      // points four metres apart, so each point reads the most any cell
      // within two of it drains.
      std::vector<square_meters_t> drains (n, 0.0f * u::m * u::m);
      for (std::size_t i = 0; i < n; ++i) {
        const auto [cx, cz] = ground.nearest (road.points[i]);
        for (int dz = -2; dz <= 2; ++dz)
          for (int dx = -2; dx <= 2; ++dx)
            drains[i] = std::max (
              drains[i],
              drainage.contributing_area_at (ground.cell (cx + dx, cz + dz)));
      }
      road.bridges.clear ();
      for (std::size_t i = 0; i < n;) {
        if (drains[i] < parameters.bridge_area) {
          ++i;
          continue;
        }
        std::size_t j = i;
        while (j + 1 < n && drains[j + 1] >= parameters.bridge_area)
          ++j;
        square_meters_t area = 0.0f * u::m * u::m;
        SurfaceElevation bed = above_all;
        for (std::size_t k = i; k <= j; ++k) {
          area = std::max (area, drains[k]);
          bed = std::min (bed, natural[k]);
        }
        // A road that keeps beside a river for longer than it takes to
        // cross it runs along its bank, not over it.
        if (along[j] - along[i] >
            river_width (area) + 16.0f * u::m +
              4.0f * std::max (ground.spacing_x, ground.spacing_z)) {
          i = j + 1;
          continue;
        }
        // Clear of the spate (with boat headroom over navigable rivers), its
        // banks a little beyond its bed's width and wherever the ground still
        // lies below the deck.
        const auto bridge_clearance = [&] (square_meters_t catchment) {
          const proportion_t navigable = band (parameters.navigable_area,
                                               3.0f * parameters.navigable_area,
                                               catchment);
          return moppe::lerp (
            parameters.clearance, parameters.navigable_clearance, navigable);
        };
        const SurfaceElevation deck =
          bed + river_depth (area) + bridge_clearance (area);
        const meters_t bank = 0.5f * river_width (area) + 2.0f * u::m;
        // Over a floodplain lower than the deck, too wide for one span,
        // the bridge keeps to the river and its banks, and the road comes
        // to it on embankments.
        const auto abutments = [&] (bool low_ground) {
          const auto reaches = [&] (std::size_t k, std::size_t from) {
            return abs (along[k] - along[from]) < bank ||
                   (low_ground && natural[k] < deck - 0.3f * u::m);
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
      // spate but never below a bank that stands higher; and where the
      // ground at an end already stands at the deck, the bank is reached
      // there and the road is cut on through it, so no hill stands up
      // through the planks.
      for (RoadBridge& bridge : road.bridges) {
        SurfaceElevation lowest = above_all;
        for (std::size_t k = bridge.first; k <= bridge.last; ++k)
          lowest = std::min (lowest, natural[k]);
        square_meters_t widest = 0.0f * u::m * u::m;
        for (std::size_t k = bridge.first; k <= bridge.last; ++k)
          widest = std::max (widest, drains[k]);
        const proportion_t navigable = band (
          parameters.navigable_area, 3.0f * parameters.navigable_area, widest);
        const meters_t headroom = moppe::lerp (
          parameters.clearance, parameters.navigable_clearance, navigable);
        const SurfaceElevation deck = lowest + river_depth (widest) + headroom;
        while (bridge.last > bridge.first + 1 &&
               natural[bridge.last - 1] >= deck - 0.3f * u::m)
          --bridge.last;
        while (bridge.first + 1 < bridge.last &&
               natural[bridge.first + 1] >= deck - 0.3f * u::m)
          ++bridge.first;
        const SurfaceElevation from = std::max (deck, natural[bridge.first]);
        const SurfaceElevation to = std::max (deck, natural[bridge.last]);
        const meters_t span = along[bridge.last] - along[bridge.first];
        for (std::size_t k = bridge.first; k <= bridge.last; ++k)
          pins[k] = lerp (from,
                          to,
                          (span > 0.0f * u::m
                             ? ((along[k] - along[bridge.first]) / span)
                                 .numerical_value_in (one)
                             : 0.0f) *
                            blend_weight[one]);
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
        const auto [cx, cz] = ground.nearest (road.points[k]);
        const std::size_t here = ground.cell (cx, cz);
        meters_t depth = standing (here);
        square_meters_t area = drainage.contributing_area_at (here);
        for (int dz = -1; dz <= 1; ++dz)
          for (int dx = -1; dx <= 1; ++dx) {
            const square_meters_t a =
              drainage.contributing_area_at (ground.cell (cx + dx, cz + dz));
            if (a >= parameters.culvert_area && depth <= 1e-3f * u::m)
              depth = std::max (depth, river_depth (a));
            area = std::max (area, a);
          }
        return std::pair (depth, area);
      };
      // Points lie four metres apart on a two-metre lattice, so a
      // one-cell stream can pass between two points without landing on
      // either's nearest cell; check the cell and its eight neighbours
      // for streams, while standing water beside the road is handled by
      // `floors` below.
      const auto crosses_near = [&] (std::size_t k) {
        const auto [cx, cz] = ground.nearest (road.points[k]);
        if (standing (ground.cell (cx, cz)) > 1e-3f * u::m)
          return true;
        for (int dz = -1; dz <= 1; ++dz)
          for (int dx = -1; dx <= 1; ++dx)
            if (drainage.contributing_area_at (
                  ground.cell (cx + dx, cz + dz)) >= parameters.culvert_area)
              return true;
        return false;
      };
      for (std::size_t i = 0; i < n;) {
        if (spanned[i] || !crosses_near (i)) {
          ++i;
          continue;
        }
        std::size_t j = i;
        while (j + 1 < n && !spanned[j + 1] && crosses_near (j + 1))
          ++j;
        std::size_t lowest = i;
        SurfaceElevation surface = below_all;
        square_meters_t drains_most = 0.0f * u::m * u::m;
        for (std::size_t k = i; k <= j; ++k) {
          const auto [depth, area] = water_at (k);
          if (natural[k] < natural[lowest])
            lowest = k;
          surface = std::max (surface, natural[k] + depth);
          drains_most = std::max (drains_most, area);
        }
        const meters_t diameter = std::clamp (
          1.2f * river_depth (drains_most), 0.6f * u::m, 1.8f * u::m);
        const SurfaceElevation pipe_top =
          natural[lowest] + diameter + 0.3f * u::m;
        const bool level = along[j] - along[i] <= 40.0f * u::m;
        for (std::size_t k = i; k <= j; ++k)
          pins[k] = level ? std::max (surface + parameters.freeboard, pipe_top)
                          : std::max (natural[k] + water_at (k).first +
                                        parameters.freeboard,
                                      natural[k] + 0.9f * u::m);
        road.culverts.push_back (
          { static_cast<std::uint32_t> (lowest), natural[lowest], diameter });
        i = j + 1;
      }
      for (const std::size_t end : { std::size_t { 0 }, n - 1 })
        for (std::size_t earlier = 0; earlier < r; ++earlier)
          if (network.roads[earlier].kind == Road::Kind::Road) {
            const SurfaceElevation height = height_on (
              network.roads[earlier], road.points[end], period, 2.0f * u::m);
            if (finite (height)) {
              pins[end] = height;
              break;
            }
          }
      // The loop's ends are one point, at one height.
      if (road.loop)
        pins[0] = pins[n - 1] = finite (pins[0])       ? pins[0]
                                : finite (pins[n - 1]) ? pins[n - 1]
                                                       : natural[0];
      // The tread keeps a freeboard over standing water beside it too: a
      // lake a few metres off, behind a bank no higher than the road, is
      // let in when the formation cuts that bank down.
      std::vector<SurfaceElevation> floors (n, below_all);
      {
        const meters_t reach = tread + 6.0f * u::m;
        const int rx = static_cast<int> (std::ceil (ground.steps_x (reach)));
        const int rz = static_cast<int> (std::ceil (ground.steps_z (reach)));
        for (std::size_t i = 0; i < n; ++i) {
          if (spanned[i])
            continue;
          const auto [cx, cz] = ground.nearest (road.points[i]);
          for (int dz = -rz; dz <= rz; ++dz)
            for (int dx = -rx; dx <= rx; ++dx) {
              if (hypot (plan_displacement (dx * ground.spacing_x,
                                            dz * ground.spacing_z)) > reach)
                continue;
              const std::size_t cell = ground.cell (cx + dx, cz + dz);
              const meters_t depth = standing (cell);
              const SurfaceElevation level = elevations[cell] + depth;
              // Water standing well above the road is held by its own
              // ground, and is no business of the road's.
              if (depth > 1e-3f * u::m && level < natural[i] + 1.5f * u::m)
                floors[i] = std::max (floors[i], level + parameters.freeboard);
            }
        }
      }
      // A turning place is level, and so is the road for the pad's length
      // before it.
      for (const bool back : { false, true }) {
        if (!turns[r][back ? 1 : 0] || n < 3)
          continue;
        const std::array<std::size_t, 3> last =
          back ? std::array<std::size_t, 3> { n - 1, n - 2, n - 3 }
               : std::array<std::size_t, 3> { 0, 1, 2 };
        SurfaceElevation level = below_all;
        for (const std::size_t k : last)
          if (finite (pins[k]))
            level = std::max (level, pins[k]);
        if (!finite (level)) {
          level = SurfaceElevation ((natural[last[0]].delta_from_zero () +
                                     natural[last[1]].delta_from_zero () +
                                     natural[last[2]].delta_from_zero ()) /
                                    3.0f);
          for (const std::size_t k : last)
            level = std::max (level, floors[k]);
        }
        for (const std::size_t k : last)
          if (!finite (pins[k]))
            pins[k] = level;
      }
      road.heights = solve_profile (along, natural, pins, floors, parameters);
      for (std::size_t i = 1; i < n; ++i) {
        ++result.points;
        if (abs (road.heights[i] - road.heights[i - 1]) >
            rise (parameters.maximum_grade + 1e-3f * grade[one],
                  along[i] - along[i - 1]))
          ++result.steep_points;
      }
    }

    // Each cell takes the cross-section of the nearest road at its
    // distance from that road's line. On a coarse lattice the formation
    // keeps at least a cell's width, so the tread stays continuous.
    const std::size_t count = domain.size ();
    std::vector<FormationClaim> claims (count);
    const meters_t spacing = std::max (ground.spacing_x, ground.spacing_z);
    const meters_t formation = std::max (tread, 1.01f * spacing);
    const meters_t path_reach =
      std::max (parameters.footpath_half_width, 0.75f * spacing) + 1.2f * u::m;
    constexpr meters_t batter_reach = 24.0f * u::m;
    constexpr meters_t blend = 4.0f * u::m;
    // One line's claims: its points and their heights, whether it is
    // built, which of its points end a segment of a bridge's span, and
    // how far along it its culverts lie.
    const auto claim_line = [&] (std::span<const plan_position_t> points,
                                 std::span<const SurfaceElevation> heights,
                                 bool built,
                                 const std::vector<bool>& spans,
                                 std::span<const meters_t> culverts) {
      const meters_t reach = built ? formation + batter_reach : path_reach;
      std::vector<meters_t> along (points.size (), 0.0f * u::m);
      for (std::size_t i = 1; i < points.size (); ++i)
        along[i] = along[i - 1] + hypot (points[i] - points[i - 1]);
      for (std::size_t i = 1; i < points.size (); ++i) {
        const plan_position_t a = points[i - 1];
        const plan_position_t b = points[i];
        const plan_displacement_t step = b - a;
        const square_meters_t length2 = dot (step, step);
        const int x0 = static_cast<int> (
          std::floor (ground.steps_x (std::min (x_of (a), x_of (b)) - reach)));
        const int x1 = static_cast<int> (
          std::ceil (ground.steps_x (std::max (x_of (a), x_of (b)) + reach)));
        const int z0 = static_cast<int> (
          std::floor (ground.steps_z (std::min (z_of (a), z_of (b)) - reach)));
        const int z1 = static_cast<int> (
          std::ceil (ground.steps_z (std::max (z_of (a), z_of (b)) + reach)));
        for (int z = z0; z <= z1; ++z)
          for (int x = x0; x <= x1; ++x) {
            const plan_displacement_t from =
              plan_position (x * ground.spacing_x, z * ground.spacing_z) - a;
            const blend_weight_t t =
              (length2 > 0.0f * u::m * u::m
                 ? std::clamp (
                     (dot (from, step) / length2).numerical_value_in (one),
                     0.0f,
                     1.0f)
                 : 0.0f) *
              blend_weight[one];
            const meters_t distance =
              hypot (from - step * t.numerical_value_in (one));
            if (distance > reach)
              continue;
            FormationClaim& claim = claims[ground.cell (x, z)];
            // A built road's claim outranks a footpath's however near.
            if ((built && !claim.built) ||
                (built == claim.built && distance < claim.distance)) {
              const meters_t here = lerp (along[i - 1], along[i], t);
              meters_t culvert = far;
              for (const meters_t at : culverts)
                culvert = std::min (culvert, abs (here - at));
              claim = { distance,
                        lerp (heights[i - 1], heights[i], t),
                        built,
                        spans[i],
                        culvert };
            }
          }
      }
    };
    for (const Road& road : network.roads) {
      std::vector<bool> spans (road.points.size (), false);
      for (const RoadBridge& bridge : road.bridges)
        for (std::size_t k = bridge.first + 1; k <= bridge.last; ++k)
          spans[k] = true;
      std::vector<meters_t> along (road.points.size (), 0.0f * u::m);
      for (std::size_t i = 1; i < road.points.size (); ++i)
        along[i] = along[i - 1] + hypot (road.points[i] - road.points[i - 1]);
      std::vector<meters_t> culverts;
      for (const RoadCulvert& c : road.culverts)
        culverts.push_back (along[c.at]);
      claim_line (road.points,
                  road.heights,
                  road.kind == Road::Kind::Road,
                  spans,
                  culverts);
    }
    // The turning places at the roads' ends, built as the roads are.
    for (const RoadTurning& turning : road_turnings (network, period))
      if (turning.heights.size () == turning.points.size ())
        claim_line (turning.points,
                    turning.heights,
                    true,
                    std::vector<bool> (turning.points.size (), false),
                    {});

    const std::span<const SurfaceElevation> original = elevations;
    result.heights.assign (elevations.begin (), elevations.end ());
    result.change.assign (count, ElevationChange (0.0f * u::m));
    result.influence.assign (count, 0.0f * proportion[one]);
    const square_meters_t cell_area = ground.spacing_x * ground.spacing_z;
    // Smoothstep over a span's fraction, clamped to it.
    const auto smooth = [] (quantity<one, float> fraction) {
      const float t =
        std::clamp (fraction.numerical_value_in (one), 0.0f, 1.0f);
      return t * t * (3.0f - 2.0f * t) * blend_weight[one];
    };
    // How much of a cell the tread covers, as its edge fades out.
    const auto covered = [] (blend_weight_t fade) {
      return (1.0f - fade.numerical_value_in (one)) * proportion[one];
    };
    const auto shape = [&] (std::size_t cell, SurfaceElevation shaped) {
      const ElevationChange change = shaped - original[cell];
      if (abs (change) < 1e-4f * u::m)
        return;
      result.heights[cell] = shaped;
      result.change[cell] = change;
      (change < 0.0f * u::m ? result.cut : result.fill) +=
        isq::height (abs (change)) * cell_area;
    };
    for (std::size_t cell = 0; cell < count; ++cell) {
      const FormationClaim& claim = claims[cell];
      if (!std::isfinite (meters_value (claim.distance)))
        continue;
      const meters_t d = claim.distance;
      // Under a bridge's span the ground is left as it is, but for a knoll
      // of the floodplain that would stand up through the planks: out to
      // a lattice step past the tread it is cut down clear of the deck's
      // underside, so the deck rides level over it.
      if (claim.bridge) {
        if (claim.built && d <= tread + spacing)
          shape (
            cell,
            std::min (original[cell], claim.height - parameters.deck_depth));
        // Under the deck the ground is trodden bare: nothing grows up
        // through the planks.
        if (claim.built)
          result.influence[cell] = covered (smooth (
            (d - (parameters.deck_half_width + 0.5f * u::m)) / (1.2f * u::m)));
        continue;
      }
      if (!claim.built && standing (cell) > 1e-3f * u::m)
        continue;
      if (!claim.built) {
        const meters_t half =
          std::max (parameters.footpath_half_width, 0.75f * spacing);
        result.influence[cell] = covered (smooth ((d - half) / (1.2f * u::m)));
        continue;
      }
      result.influence[cell] = covered (
        smooth ((d - std::max (tread, 0.75f * spacing)) / (1.5f * u::m)));
      // The embankment is built through standing water and over a
      // stream, but along a culvert's line and on any stream cell the
      // ground is left as it was, so the water passes under the tread
      // without being dammed by the embankment.
      if (claim.culvert < 0.75f * spacing ||
          drainage.contributing_area_at (cell) >= parameters.culvert_area)
        continue;
      const SurfaceElevation g = original[cell];
      const SurfaceElevation edge =
        claim.height - parameters.crown * parameters.half_width;
      SurfaceElevation shaped;
      if (d <= formation) {
        shaped =
          claim.height - parameters.crown * std::min (d, parameters.half_width);
      } else {
        const meters_t out = d - formation;
        const SurfaceElevation fill = edge - parameters.fill_slope * out;
        // On a cut side, drop from the shoulder into a roadside ditch
        // before climbing the cut batter, so hillside runoff stays off
        // the tread.
        const meters_t ditch_drop =
          parameters.ditch_depth > 0.0f * u::m
            ? meters_t (parameters.ditch_depth / parameters.fill_slope)
            : 0.0f * u::m;
        const SurfaceElevation ditch_floor = edge - parameters.ditch_depth;
        const SurfaceElevation cut =
          out <= ditch_drop ? fill
          : out <= ditch_drop + parameters.ditch_width
            ? ditch_floor
            : ditch_floor + parameters.cut_slope *
                              (out - ditch_drop - parameters.ditch_width);
        shaped = std::clamp (g, fill, cut);
        // The last few metres of reach ease_profile back to the ground, so
        // where a batter has not met it no step is left.
        shaped =
          lerp (shaped, g, smooth ((out - (batter_reach - blend)) / blend));
      }
      shape (cell, shaped);
    }
    return result;
  }
}
