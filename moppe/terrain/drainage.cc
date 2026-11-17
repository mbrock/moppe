#include <moppe/correct_math.hh>
#include <moppe/parallel.hh>
#include <moppe/terrain/drainage.hh>
#include <moppe/terrain/flood.hh>
#include <moppe/terrain/fractional_drainage.hh>
#include <moppe/terrain/lowest_ready.hh>

#include <moppe/profile.hh>

#include <algorithm>
#include <array>
#include <cmath>
#include <functional>
#include <numeric>
#include <queue>
#include <span>
#include <stdexcept>

namespace moppe::terrain {
  namespace {
    struct Offset {
      int x;
      int y;
    };

    constexpr std::array<Offset, 8> neighbors { { { -1, -1 },
                                                  { 0, -1 },
                                                  { 1, -1 },
                                                  { -1, 0 },
                                                  { 1, 0 },
                                                  { -1, 1 },
                                                  { 0, 1 },
                                                  { 1, 1 } } };

    std::size_t wrapped (int value, std::size_t period) {
      const int n = static_cast<int> (period);
      const int result = value % n;
      return static_cast<std::size_t> (result < 0 ? result + n : result);
    }

    meters_t receiver_distance (std::uint32_t cell,
                                std::uint32_t receiver,
                                const TerrainDomain& grid) {
      const int width = static_cast<int> (grid.width ());
      const int height = static_cast<int> (grid.height ());
      int dx =
        static_cast<int> (cell % width) - static_cast<int> (receiver % width);
      int dy =
        static_cast<int> (cell / width) - static_cast<int> (receiver / width);
      if (dx > width / 2)
        dx -= width;
      if (dx < -width / 2)
        dx += width;
      if (dy > height / 2)
        dy -= height;
      if (dy < -height / 2)
        dy += height;
      return cr::hypot (
               static_cast<float> (dx) *
                 (grid.spacing_x ()).numerical_value_in (moppe::u::m),
               static_cast<float> (dy) *
                 (grid.spacing_z ()).numerical_value_in (moppe::u::m)) *
             mp_units::si::metre;
    }

    // Optional per-cell channel readings from a D-infinity analysis. The
    // receiver graph keeps topological authority over reaches; these columns
    // refine only the continuous geometry derived from it.
    struct ChannelGeometry {
      std::span<const FractionalContributingArea> area;
      std::span<const ChannelTangent> tangent;
      const FractionalFlowDomain* routes = nullptr;

      bool empty () const noexcept {
        return area.empty ();
      }
    };

    // Turn an eight-way finite-difference knot tangent toward the carved
    // channel direction while keeping its magnitude, so the Hermite spline
    // follows the valley the erosion actually cut instead of the quantized
    // cell chain.
    void align_knot_tangent (float& tangent_x,
                             float& tangent_z,
                             const Vec3& channel) {
      const float finite_length = cr::hypot (tangent_x, tangent_z);
      const float channel_length = cr::hypot (channel[0], channel[2]);
      if (finite_length < 1e-6f || channel_length < 1e-6f)
        return;
      const float unit_x = tangent_x / finite_length;
      const float unit_z = tangent_z / finite_length;
      const float channel_x = channel[0] / channel_length;
      const float channel_z = channel[2] / channel_length;
      // An opposing or orthogonal channel reading marks a flat wet route or
      // a lake crossing; the finite difference stays authoritative there.
      if (unit_x * channel_x + unit_z * channel_z <= 0.0f)
        return;
      const float blended_x = 0.5f * (unit_x + channel_x);
      const float blended_z = 0.5f * (unit_z + channel_z);
      const float blended_length = cr::hypot (blended_x, blended_z);
      if (blended_length < 1e-6f)
        return;
      tangent_x = finite_length * blended_x / blended_length;
      tangent_z = finite_length * blended_z / blended_length;
    }

    RiverAlignmentPoint interpolate (const RiverAlignmentPoint& from,
                                     const RiverAlignmentPoint& to,
                                     float t) {
      const auto mix = [t] (auto a, auto b) { return a + (b - a) * t; };
      return { .place = mix (from.place, to.place),
               .contributing_area =
                 mix (from.contributing_area, to.contributing_area),
               .slope = mix (from.slope, to.slope),
               .waterfall = mix (from.waterfall, to.waterfall),
               .standing_water = mix (from.standing_water, to.standing_water),
               .water_level = mix (from.water_level, to.water_level),
               .pooled = mix (from.pooled, to.pooled) };
    }

    RiverAlignment
    smooth_river_alignment (const std::vector<RiverAlignmentPoint>& raw,
                            std::span<const Vec3> knot_tangents,
                            const TerrainDomain& grid) {
      RiverAlignment alignment;
      if (raw.size () < 2)
        return alignment;
      // The curve is fitted in bare metres, lane by lane.
      const auto x_m = [] (const RiverAlignmentPoint& point) {
        return plan_value (point.place).x ();
      };
      const auto z_m = [] (const RiverAlignmentPoint& point) {
        return plan_value (point.place).z ();
      };

      // Match the trail system's geometric authority: a damped cubic Hermite
      // curve stays close to the routed corridor while removing eight-way
      // corners. Sampling below source-cell spacing makes the visible banks
      // independent of the terrain lattice.
      constexpr float tangent_scale = 0.34f;
      const float sample_spacing = std::clamp (
        0.4f * std::min ((grid.spacing_x ()).numerical_value_in (moppe::u::m),
                         (grid.spacing_z ()).numerical_value_in (moppe::u::m)),
        0.75f,
        2.0f);
      for (std::size_t segment = 0; segment + 1 < raw.size (); ++segment) {
        const RiverAlignmentPoint& a = raw[segment];
        const RiverAlignmentPoint& b = raw[segment + 1];
        const RiverAlignmentPoint& before = segment ? raw[segment - 1] : a;
        const RiverAlignmentPoint& after =
          segment + 2 < raw.size () ? raw[segment + 2] : b;
        float tangent_ax = (x_m (b) - x_m (before)) * tangent_scale;
        float tangent_az = (z_m (b) - z_m (before)) * tangent_scale;
        float tangent_bx = (x_m (after) - x_m (a)) * tangent_scale;
        float tangent_bz = (z_m (after) - z_m (a)) * tangent_scale;
        if (!knot_tangents.empty ()) {
          align_knot_tangent (tangent_ax, tangent_az, knot_tangents[segment]);
          align_knot_tangent (
            tangent_bx, tangent_bz, knot_tangents[segment + 1]);
        }
        const float run = cr::hypot (x_m (b) - x_m (a), z_m (b) - z_m (a));
        const int subdivisions =
          std::max (1, static_cast<int> (std::ceil (run / sample_spacing)));
        for (int step = 0; step < subdivisions; ++step) {
          const float t = static_cast<float> (step) / subdivisions;
          const float t2 = t * t;
          const float t3 = t2 * t;
          const float h00 = 2.0f * t3 - 3.0f * t2 + 1.0f;
          const float h10 = t3 - 2.0f * t2 + t;
          const float h01 = -2.0f * t3 + 3.0f * t2;
          const float h11 = t3 - t2;
          RiverAlignmentPoint point = interpolate (a, b, t);
          point.place = plan_position (x_m (a) * h00 + tangent_ax * h10 +
                                         x_m (b) * h01 + tangent_bx * h11,
                                       z_m (a) * h00 + tangent_az * h10 +
                                         z_m (b) * h01 + tangent_bz * h11);
          alignment.points.push_back (point);
        }
      }
      alignment.points.push_back (raw.back ());

      double length_m = 0.0;
      for (std::size_t point = 1; point < alignment.points.size (); ++point) {
        const auto& before = alignment.points[point - 1];
        auto& current = alignment.points[point];
        length_m += cr::hypot (x_m (current) - x_m (before),
                               z_m (current) - z_m (before));
        current.distance = static_cast<float> (length_m) * u::m;
      }
      alignment.length = length_m * mp_units::si::metre;
      return alignment;
    }

    void build_river_alignments (RiverNetwork& network,
                                 const FloodField& flood,
                                 const LakeCensus& census,
                                 const DrainageGraph& drainage,
                                 const ChannelGeometry& channels) {
      const TerrainDomain& grid = drainage.domain ();
      const int width = static_cast<int> (grid.width ());
      const meters_t period_x = grid.period_x ();
      const meters_t period_z = grid.period_z ();
      std::vector<std::uint8_t> waterfall_cells (grid.size (), 0);
      for (const Waterfall& waterfall : network.waterfalls)
        waterfall_cells[waterfall.lip_cell] = 1;
      const auto water_cell = [&] (CellIndex cell) {
        return census.body_at (cell) != LakeCensus::dry || flood.ocean[cell];
      };
      const auto terminating_water_cell = [&] (CellIndex cell) {
        if (flood.ocean[cell])
          return true;
        const WaterBodyId id = census.body_at (cell);
        return id != LakeCensus::dry &&
               water_body_terminates_rivers (census.water_body (id));
      };

      for (RiverReach& reach : network.reaches) {
        std::vector<CellIndex> cells = reach.cells;
        if (cells.empty ())
          continue;
        // A reach born at a lake spill starts on the first dry cell. Include
        // the wet outlet immediately upstream so its flow and level join the
        // standing body instead of appearing at a hard dry cross-section.
        if (reach.upstream_body != no_water_body) {
          const WaterBody& body = census.water_body (reach.upstream_body);
          if (water_body_terminates_rivers (body) &&
              body.outlet_cell != WaterBody::no_cell &&
              body.outlet_cell != cells.front ())
            cells.insert (cells.begin (), body.outlet_cell);
        }
        CellIndex next = drainage.receiver[cells.back ()];
        if (next != cells.back ())
          cells.push_back (next);
        // Carry mouths far enough into their standing surface for current to
        // enter the receiving body. Channel-like bodies and tiny depressions
        // are crossed along their proven receiver route, so they cannot punch
        // gaps into an otherwise continuous visible river; each body crossed
        // end to end is recorded for the running-water painter.
        std::vector<WaterBodyId> crossings;
        if (terminating_water_cell (cells.back ())) {
          next = drainage.receiver[cells.back ()];
          if (next != cells.back () && terminating_water_cell (next))
            cells.push_back (next);
        } else {
          while (water_cell (cells.back ()) &&
                 !terminating_water_cell (cells.back ())) {
            const WaterBodyId id = census.body_at (cells.back ());
            if (id != LakeCensus::dry &&
                (crossings.empty () || crossings.back () != id))
              crossings.push_back (id);
            next = drainage.receiver[cells.back ()];
            if (next == cells.back ())
              break;
            cells.push_back (next);
          }
        }
        // A walk that dead-ends inside a body did not cross it: a terminal
        // pond keeps its sheet even when shaped like a channel.
        const bool exited =
          !water_cell (cells.back ()) || terminating_water_cell (cells.back ());
        if (exited)
          for (const WaterBodyId id : crossings)
            network.body_traversed[id] = 1;

        std::vector<RiverAlignmentPoint> raw;
        raw.reserve (cells.size ());
        std::vector<Vec3> knot_tangents;
        if (!channels.empty ())
          knot_tangents.reserve (cells.size ());
        for (std::size_t knot = 0; knot < cells.size (); ++knot) {
          const CellIndex cell = cells[knot];
          const auto cell_x = [&] (CellIndex at) {
            return static_cast<float> (at % width) * grid.spacing_x ();
          };
          const auto cell_z = [&] (CellIndex at) {
            return static_cast<float> (at / width) * grid.spacing_z ();
          };
          meters_t x = cell_x (cell);
          meters_t z = cell_z (cell);
          // Interior knots leave the lattice: the previous cell's D-infinity
          // route records where its continuous flow ray crosses the segment
          // between its two receivers, and pinning the knot to that crossing
          // removes cell-center quantization while staying inside the routed
          // corridor. Endpoint knots stay at their cell centers so tributary
          // and trunk alignments continue to meet exactly.
          if (channels.routes && knot > 0 && knot + 1 < cells.size ()) {
            const FractionalFlowRoute& route =
              channels.routes->route (cells[knot - 1]);
            if (route.arc_count == 2 && (route.arcs[0].receiver == cell ||
                                         route.arcs[1].receiver == cell)) {
              const CellIndex cardinal = route.arcs[0].receiver;
              const CellIndex diagonal = route.arcs[1].receiver;
              const auto t = route.receiver_interpolation;
              const meters_t cardinal_x = cell_x (cardinal);
              const meters_t cardinal_z = cell_z (cardinal);
              const meters_t diagonal_x = cell_x (diagonal);
              const meters_t diagonal_z = cell_z (diagonal);
              x = cardinal_x +
                  minimum_image_delta (diagonal_x - cardinal_x, period_x) *
                    t.numerical_value_in (one);
              z = cardinal_z +
                  minimum_image_delta (diagonal_z - cardinal_z, period_z) *
                    t.numerical_value_in (one);
            }
          }
          if (!raw.empty ()) {
            const meters_t last_x = x_of (raw.back ().place);
            const meters_t last_z = z_of (raw.back ().place);
            x = last_x + minimum_image_delta (x - last_x, period_x);
            z = last_z + minimum_image_delta (z - last_z, period_z);
          }
          const bool terminating = terminating_water_cell (cell);
          const bool pooled = !terminating && water_cell (cell);
          // Fractional contributing area varies smoothly along a reach where
          // the all-or-nothing receiver accumulation steps, so widths from
          // it taper instead of popping between cells.
          const square_meters_t area =
            channels.empty () ? drainage.contributing_area_at (cell)
                              : square_meters_t (channels.area[cell]);
          raw.push_back (
            { .place = plan_position (x, z),
              .contributing_area = area,
              .slope = drainage.slope_at (cell),
              .waterfall =
                (waterfall_cells[cell] ? 1.0f : 0.0f) * proportion[one],
              .standing_water = (terminating ? 1.0f : 0.0f) * proportion[one],
              .water_level = flood.water_level (cell),
              .pooled = (pooled ? 1.0f : 0.0f) * proportion[one] });
          if (!channels.empty ())
            knot_tangents.push_back (
              channels.tangent[cell].numerical_value_in (mp_units::one));
        }
        reach.alignment = smooth_river_alignment (raw, knot_tangents, grid);
      }

      // Give every point a confluence-continuous flow coordinate. A point's
      // coordinate is its negative remaining distance to the final mouth, so
      // all tributaries and the trunk agree exactly at their shared node.
      std::vector<double> remaining (network.reaches.size (), -1.0);
      std::vector<std::uint8_t> active (network.reaches.size (), 0);
      std::function<double (RiverReachId)> solve = [&] (RiverReachId id) {
        if (remaining[id] >= 0.0)
          return remaining[id];
        if (active[id])
          throw std::logic_error ("river reaches do not form a downstream DAG");
        active[id] = 1;
        const RiverReach& reach = network.reaches[id];
        const double downstream = reach.downstream_reach != RiverReach::no_id
                                    ? solve (reach.downstream_reach)
                                    : 0.0;
        active[id] = 0;
        remaining[id] =
          (reach.alignment.length).numerical_value_in (moppe::u::m) +
          downstream;
        return remaining[id];
      };
      for (RiverReach& reach : network.reaches) {
        const double from_start = solve (reach.id);
        for (RiverAlignmentPoint& point : reach.alignment.points)
          point.flow_distance =
            static_cast<float> (meters_value (point.distance) - from_start) *
            u::m;
      }
    }
  }

  DrainageGraph
  detail::analyze_drainage (const TerrainDomain& domain,
                            std::span<const SurfaceElevation> elevations) {
    MOPPE_PROFILE_ZONE ("analyze_drainage");
    const std::size_t width = domain.width ();
    const std::size_t height = domain.height ();
    const std::size_t count = width * height;
    const auto index = [width] (std::size_t x, std::size_t y) {
      return y * width + x;
    };

    std::vector<CellIndex> receiver (count, no_cell);
    std::vector<slope_t> slope (count, 0.0f * terrain_slope[one]);
    {
      MOPPE_PROFILE_ZONE ("drainage.choose_receivers");
      for (std::size_t y = 0; y < height; ++y)
        for (std::size_t x = 0; x < width; ++x) {
          const std::size_t cell = index (x, y);
          receiver[cell] = static_cast<std::uint32_t> (cell);
          const SurfaceElevation elevation =
            elevation_point_at (domain, elevations, x, y);
          slope_t steepest = 0.0f * terrain_slope[one];
          for (const Offset offset : neighbors) {
            const int raw_x = static_cast<int> (x) + offset.x;
            const int raw_y = static_cast<int> (y) + offset.y;
            const std::size_t nx = wrapped (raw_x, width);
            const std::size_t ny = wrapped (raw_y, height);
            const SurfaceElevation neighbor_elevation =
              elevation_point_at (domain, elevations, nx, ny);
            const meters_t distance =
              cr::hypot (
                offset.x * domain.spacing_x ().numerical_value_in (u::m),
                offset.y * domain.spacing_z ().numerical_value_in (u::m)) *
              u::m;
            const slope_t candidate = quantity_cast<terrain_slope> (
              (elevation - neighbor_elevation) / distance);
            if (candidate > steepest) {
              steepest = candidate;
              receiver[cell] = static_cast<std::uint32_t> (index (nx, ny));
            }
          }
          slope[cell] = steepest;
        }
    }

    std::vector<std::uint32_t> order (count);
    std::iota (order.begin (), order.end (), 0u);
    std::stable_sort (
      order.begin (), order.end (), [&] (std::uint32_t a, std::uint32_t b) {
        const std::size_t ax = a % width, ay = a / width;
        const std::size_t bx = b % width, by = b / width;
        return elevation_point_at (domain, elevations, ax, ay) >
               elevation_point_at (domain, elevations, bx, by);
      });

    const ContributingArea cell_area =
      quantity_cast<contributing_area> (domain.cell_area ());
    std::vector<ContributingArea> area (count, cell_area);
    for (const std::uint32_t cell : order)
      if (receiver[cell] != cell)
        area[receiver[cell]] += area[cell];

    return { .readings = make_drainage_readings (domain, slope, area),
             .receiver = std::move (receiver) };
  }

  WetDrainageRouting route_wet_drainage (const FloodField& flood,
                                         const LakeCensus& census) {
    MOPPE_PROFILE_ZONE ("route_wet_drainage");
    const TerrainDomain& grid = flood.domain ();
    const std::size_t width = grid.width ();
    const std::size_t height = grid.height ();
    const std::size_t count = width * height;
    if (census.cell_count () != count)
      throw std::invalid_argument ("lake census does not match terrain");

    const std::span<const SurfaceElevation> surface = flood.water_levels ();
    const auto index = [width] (std::size_t x, std::size_t y) {
      return y * width + x;
    };

    std::vector<CellIndex> receiver (count, no_cell);
    std::vector<slope_t> slope (count, 0.0f * terrain_slope[one]);
    {
      MOPPE_PROFILE_ZONE ("wet_drainage.choose_receivers");
      std::array<meters_t, neighbors.size ()> distance;
      for (std::size_t n = 0; n < neighbors.size (); ++n)
        distance[n] =
          cr::hypot (neighbors[n].x *
                       (grid.spacing_x ()).numerical_value_in (moppe::u::m),
                     neighbors[n].y *
                       (grid.spacing_z ()).numerical_value_in (moppe::u::m)) *
          u::m;
      // Each cell reads its neighbours and writes only itself, so the rows
      // divide among threads without changing a bit.
      parallel_for (height, 16, [&] (std::size_t first, std::size_t last) {
        for (std::size_t y = first; y < last; ++y)
          for (std::size_t x = 0; x < width; ++x) {
            const std::size_t cell = index (x, y);
            receiver[cell] = flood.spill_receiver[cell];
            slope_t steepest = 0.0f * terrain_slope[one];
            for (std::size_t n = 0; n < neighbors.size (); ++n) {
              const int raw_x = static_cast<int> (x) + neighbors[n].x;
              const int raw_y = static_cast<int> (y) + neighbors[n].y;
              const std::size_t nx = wrapped (raw_x, width);
              const std::size_t ny = wrapped (raw_y, height);
              const std::size_t next = index (nx, ny);
              const slope_t candidate = quantity_cast<terrain_slope> (
                (surface[cell] - surface[next]) / distance[n]);
              if (candidate > steepest) {
                steepest = candidate;
                receiver[cell] = static_cast<std::uint32_t> (next);
              }
            }
            slope[cell] = steepest;
          }
      });
    }

    // A flat inland body has one route-proven spill. Replace any incidental
    // priority-flood partition inside it with a deterministic breadth-first
    // tree leading to that spill, so the body's full discharge stays whole.
    {
      MOPPE_PROFILE_ZONE ("wet_drainage.route_lake_interiors");
      std::vector<std::uint8_t> routed (count, 0);
      std::queue<std::uint32_t> body_frontier;
      for (const WaterBody& body : census.water_bodies ()) {
        if (body.ocean_connected || body.outlet_cell == WaterBody::no_cell)
          continue;
        receiver[body.outlet_cell] = body.spill_cell;
        routed[body.outlet_cell] = 1;
        body_frontier.push (body.outlet_cell);
        while (!body_frontier.empty ()) {
          const std::uint32_t cell = body_frontier.front ();
          body_frontier.pop ();
          const std::size_t x = cell % width;
          const std::size_t y = cell / width;
          for (const Offset offset : neighbors) {
            const int raw_x = static_cast<int> (x) + offset.x;
            const int raw_y = static_cast<int> (y) + offset.y;
            const std::size_t nx = wrapped (raw_x, width);
            const std::size_t ny = wrapped (raw_y, height);
            const std::uint32_t next =
              static_cast<std::uint32_t> (index (nx, ny));
            if (routed[next] || census.body_at (CellIndex { next }) != body.id)
              continue;
            routed[next] = 1;
            receiver[next] = cell;
            body_frontier.push (next);
          }
        }
      }
    }

    return { .domain = grid,
             .receiver = std::move (receiver),
             .slope = std::move (slope) };
  }

  DrainageGraph analyze_wet_drainage (const FloodField& flood,
                                      const LakeCensus& census) {
    MOPPE_PROFILE_ZONE ("analyze_wet_drainage");
    WetDrainageRouting routing = route_wet_drainage (flood, census);
    const TerrainDomain& grid = routing.domain;
    const std::size_t width = grid.width ();
    const std::size_t height = grid.height ();
    const std::size_t count = width * height;
    std::vector<CellIndex>& receiver = routing.receiver;

    // Equal-height lake routes cannot be accumulated by elevation order.
    // Receiver edges either lower the filled surface or follow the acyclic
    // priority-flood forest, so a general topological pass handles both.
    const ContributingArea cell_area =
      quantity_cast<contributing_area> (grid.cell_area ());
    std::vector<ContributingArea> area (count, cell_area);
    std::vector<std::uint32_t> order;
    order.reserve (count);
    {
      MOPPE_PROFILE_ZONE ("wet_drainage.accumulate_contributing_area");
      std::vector<std::uint32_t> donors (count, 0);
      for (std::uint32_t cell = 0; cell < count; ++cell)
        if (receiver[cell] != cell)
          ++donors[receiver[cell]];

      LowestReady ready (count);
      for (std::uint32_t cell = 0; cell < count; ++cell)
        if (donors[cell] == 0)
          ready.push (cell);

      while (!ready.empty ()) {
        const std::uint32_t cell = ready.pop ();
        order.push_back (cell);
        if (receiver[cell] == cell)
          continue;
        const std::uint32_t next = receiver[cell];
        area[next] += area[cell];
        if (--donors[next] == 0)
          ready.push (next);
      }
    }
    if (order.size () != count)
      throw std::logic_error ("wet drainage routing contains a cycle");

    return { .readings = make_drainage_readings (grid, routing.slope, area),
             .receiver = std::move (receiver) };
  }

  RiverNetwork
  extract_river_network (const FloodField& flood,
                         const LakeCensus& census,
                         const DrainageGraph& drainage,
                         square_meters_t minimum_area,
                         const WaterfallParameters& waterfall_parameters) {
    MOPPE_PROFILE_ZONE ("extract_river_network");
    const std::size_t count = flood.width () * flood.height ();
    if (!std::isfinite (
          (minimum_area).numerical_value_in (moppe::u::m * moppe::u::m)) ||
        minimum_area < 0.0f * mp_units::si::metre * mp_units::si::metre)
      throw std::invalid_argument ("river area threshold must be finite");
    if (!std::isfinite ((waterfall_parameters.minimum_drop)
                          .numerical_value_in (moppe::u::m)) ||
        waterfall_parameters.minimum_drop < 0.0f * mp_units::si::metre ||
        !std::isfinite (waterfall_parameters.minimum_slope.numerical_value_in (
          mp_units::one)) ||
        waterfall_parameters.minimum_slope <
          0.0f * terrain_slope[mp_units::one])
      throw std::invalid_argument ("waterfall thresholds must be finite");
    if (census.cell_count () != count || flood.ocean.size () != count ||
        drainage.width () != flood.width () ||
        drainage.height () != flood.height ())
      throw std::invalid_argument ("river analyses do not share a domain");

    std::vector<std::uint8_t> eligible (count, 0);
    for (std::uint32_t cell = 0; cell < count; ++cell)
      eligible[cell] = census.body_at (CellIndex { cell }) == LakeCensus::dry &&
                       !flood.ocean[cell] && drainage.receiver[cell] != cell &&
                       drainage.contributing_area_at (cell) >= minimum_area;

    std::vector<std::uint32_t> river_donors (count, 0);
    for (std::uint32_t cell = 0; cell < count; ++cell) {
      const std::uint32_t next = drainage.receiver[cell];
      if (eligible[cell] && eligible[next])
        ++river_donors[next];
    }

    std::vector<WaterBodyId> body_at_spill (count, no_water_body);
    WaterBodyId ocean_body = no_water_body;
    for (const WaterBody& body : census.water_bodies ()) {
      if (body.spill_cell != WaterBody::no_cell)
        body_at_spill[body.spill_cell] = body.id;
      if (body.classification == WaterBodyClass::Sea &&
          ocean_body == no_water_body)
        ocean_body = body.id;
    }

    RiverNetwork network { .minimum_area = minimum_area,
                           .waterfall_parameters = waterfall_parameters,
                           .body_traversed = std::vector<std::uint8_t> (
                             census.domain ().size (), 0) };
    std::vector<RiverReachId> reach_by_cell (count, RiverReach::no_id);
    for (std::uint32_t start = 0; start < count; ++start) {
      if (!eligible[start] || reach_by_cell[start] != RiverReach::no_id ||
          (river_donors[start] == 1 && body_at_spill[start] == no_water_body))
        continue;
      RiverReach reach { .id =
                           static_cast<std::uint32_t> (network.reaches.size ()),
                         .upstream_body = body_at_spill[start],
                         .downstream_body = no_water_body,
                         .downstream_ocean = false,
                         .downstream_reach = RiverReach::no_id,
                         .upstream_area = drainage.contributing_area_at (start),
                         .downstream_area =
                           drainage.contributing_area_at (start),
                         .maximum_slope = 0.0f * terrain_slope[mp_units::one] };
      std::uint32_t cell = start;
      while (eligible[cell] && reach_by_cell[cell] == RiverReach::no_id) {
        reach_by_cell[cell] = reach.id;
        reach.cells.push_back (cell);
        reach.downstream_area = drainage.contributing_area_at (cell);
        reach.maximum_slope =
          std::max (reach.maximum_slope, drainage.slope_at (cell));
        const std::uint32_t next = drainage.receiver[cell];
        if (!eligible[next] || river_donors[next] != 1)
          break;
        cell = next;
      }
      const std::uint32_t next = drainage.receiver[reach.cells.back ()];
      if (census.body_at (CellIndex { next }) != LakeCensus::dry)
        reach.downstream_body = census.body_at (CellIndex { next });
      else if (flood.ocean[next]) {
        reach.downstream_ocean = true;
        if (ocean_body != no_water_body)
          reach.downstream_body = ocean_body;
      }
      network.reaches.push_back (std::move (reach));
    }

    for (RiverReach& reach : network.reaches) {
      const std::uint32_t next = drainage.receiver[reach.cells.back ()];
      if (eligible[next])
        reach.downstream_reach = reach_by_cell[next];
    }
    // A body that does not terminate rivers — a transient depression or a
    // channel-like flooded segment — is drainage structure, not a rendered
    // lake. Link its inlet reach directly to the spill reach so the
    // continuous alignment, flow coordinate, and junction surface bridge it.
    for (RiverReach& reach : network.reaches) {
      if (reach.downstream_reach != RiverReach::no_id ||
          reach.downstream_body == no_water_body)
        continue;
      const WaterBody& body = census.water_body (reach.downstream_body);
      if (water_body_terminates_rivers (body) ||
          body.spill_cell == WaterBody::no_cell || !eligible[body.spill_cell])
        continue;
      reach.downstream_reach = reach_by_cell[body.spill_cell];
    }

    for (const RiverReach& reach : network.reaches) {
      const square_meters_t reference_area =
        std::max (minimum_area, drainage.domain ().cell_area ());
      Waterfall selected {};
      bool has_selected = false;
      std::size_t last_candidate = 0;
      meters_t selected_score = 0.0f * u::m;
      const auto emit_selected = [&] {
        if (!has_selected)
          return;
        network.waterfalls.push_back (selected);
        has_selected = false;
        selected_score = 0.0f * u::m;
      };
      for (std::size_t i = 0; i < reach.cells.size (); ++i) {
        const std::uint32_t cell = reach.cells[i];
        const std::uint32_t next = drainage.receiver[cell];
        const meters_t distance =
          receiver_distance (cell, next, drainage.domain ());
        const slope_t slope = drainage.slope_at (cell);
        const meters_t drop =
          slope.numerical_value_in (mp_units::one) * distance;
        if (drop < waterfall_parameters.minimum_drop ||
            slope < waterfall_parameters.minimum_slope)
          continue;
        if (has_selected &&
            i > last_candidate +
                  count_value (waterfall_parameters.separation_cells))
          emit_selected ();
        last_candidate = i;
        const square_meters_t area = drainage.contributing_area_at (cell);
        // The drop, weighted up by the square root of how many times the
        // reference catchment feeds it.
        const meters_t score =
          drop *
          std::sqrt (std::max (
            1.0f, (area / reference_area).numerical_value_in (mp_units::one)));
        if (!has_selected || score > selected_score ||
            (score == selected_score && cell < selected.lip_cell)) {
          selected = { .reach_id = reach.id,
                       .lip_cell = cell,
                       .foot_cell = next,
                       .drop = drop,
                       .horizontal_distance = distance,
                       .slope = slope,
                       .contributing_area = area };
          has_selected = true;
          selected_score = score;
        }
      }
      emit_selected ();
    }
    build_river_alignments (network, flood, census, drainage, {});
    return network;
  }

  RiverNetwork
  extract_river_network (const FloodField& flood,
                         const LakeCensus& census,
                         const DrainageGraph& drainage,
                         const FractionalDrainage& channels,
                         square_meters_t minimum_area,
                         const WaterfallParameters& waterfall_parameters) {
    RiverNetwork network = extract_river_network (
      flood, census, drainage, minimum_area, waterfall_parameters);
    if (channels.domain ().terrain_domain () != drainage.domain ())
      throw std::invalid_argument (
        "channel geometry does not share the drainage lattice");
    // Rebuild only the continuous geometry: the receiver topology above
    // stays authoritative while the D-infinity columns supply smooth widths
    // and carved-valley knot tangents.
    build_river_alignments (
      network,
      flood,
      census,
      drainage,
      { .area = spatial::get<fractional_contributing_area> (channels),
        .tangent = spatial::get<channel_tangent> (channels),
        .routes = &channels.domain () });
    return network;
  }
}
