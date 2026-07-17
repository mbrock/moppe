#include <moppe/terrain/drainage.hh>
#include <moppe/terrain/flood.hh>
#include <moppe/terrain/fractional_drainage.hh>

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
                                const TerrainGrid& grid) {
      const int width = static_cast<int> (grid.unique_width ());
      const int height = static_cast<int> (grid.unique_height ());
      int dx =
        static_cast<int> (cell % width) - static_cast<int> (receiver % width);
      int dy =
        static_cast<int> (cell / width) - static_cast<int> (receiver / width);
      if (grid.topology == Topology::Torus) {
        if (dx > width / 2)
          dx -= width;
        if (dx < -width / 2)
          dx += width;
        if (dy > height / 2)
          dy -= height;
        if (dy < -height / 2)
          dy += height;
      }
      return std::hypot (static_cast<float> (dx) * grid.spacing_x_m (),
                         static_cast<float> (dy) * grid.spacing_y_m ()) *
             mp_units::si::metre;
    }

    float river_wrapped_delta (float delta, float period) {
      return std::remainder (delta, period);
    }

    // Optional per-cell channel readings from a D-infinity analysis. The D8
    // graph keeps topological authority over reaches; these columns refine
    // only the continuous geometry derived from it.
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
      const float finite_length = std::hypot (tangent_x, tangent_z);
      const float channel_length = std::hypot (channel[0], channel[2]);
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
      const float blended_length = std::hypot (blended_x, blended_z);
      if (blended_length < 1e-6f)
        return;
      tangent_x = finite_length * blended_x / blended_length;
      tangent_z = finite_length * blended_z / blended_length;
    }

    RiverAlignmentPoint interpolate (const RiverAlignmentPoint& from,
                                     const RiverAlignmentPoint& to,
                                     float t) {
      const auto mix = [t] (float a, float b) { return a + (b - a) * t; };
      return { .x_m = mix (from.x_m, to.x_m),
               .z_m = mix (from.z_m, to.z_m),
               .contributing_area_m2 =
                 mix (from.contributing_area_m2, to.contributing_area_m2),
               .slope = mix (from.slope, to.slope),
               .waterfall = mix (from.waterfall, to.waterfall),
               .standing_water = mix (from.standing_water, to.standing_water),
               .water_level_m = mix (from.water_level_m, to.water_level_m),
               .pooled = mix (from.pooled, to.pooled) };
    }

    RiverAlignment
    smooth_river_alignment (const std::vector<RiverAlignmentPoint>& raw,
                            std::span<const Vec3> knot_tangents,
                            const TerrainGrid& grid) {
      RiverAlignment alignment;
      if (raw.size () < 2)
        return alignment;

      // Match the trail system's geometric authority: a damped cubic Hermite
      // curve stays close to the routed corridor while removing eight-way
      // corners. Sampling below source-cell spacing makes the visible banks
      // independent of the terrain lattice.
      constexpr float tangent_scale = 0.34f;
      const float sample_spacing =
        std::clamp (0.4f * std::min (grid.spacing_x_m (), grid.spacing_y_m ()),
                    0.75f,
                    2.0f);
      for (std::size_t segment = 0; segment + 1 < raw.size (); ++segment) {
        const RiverAlignmentPoint& a = raw[segment];
        const RiverAlignmentPoint& b = raw[segment + 1];
        const RiverAlignmentPoint& before = segment ? raw[segment - 1] : a;
        const RiverAlignmentPoint& after =
          segment + 2 < raw.size () ? raw[segment + 2] : b;
        float tangent_ax = (b.x_m - before.x_m) * tangent_scale;
        float tangent_az = (b.z_m - before.z_m) * tangent_scale;
        float tangent_bx = (after.x_m - a.x_m) * tangent_scale;
        float tangent_bz = (after.z_m - a.z_m) * tangent_scale;
        if (!knot_tangents.empty ()) {
          align_knot_tangent (tangent_ax, tangent_az, knot_tangents[segment]);
          align_knot_tangent (
            tangent_bx, tangent_bz, knot_tangents[segment + 1]);
        }
        const float run = std::hypot (b.x_m - a.x_m, b.z_m - a.z_m);
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
          point.x_m =
            a.x_m * h00 + tangent_ax * h10 + b.x_m * h01 + tangent_bx * h11;
          point.z_m =
            a.z_m * h00 + tangent_az * h10 + b.z_m * h01 + tangent_bz * h11;
          alignment.points.push_back (point);
        }
      }
      alignment.points.push_back (raw.back ());

      double length_m = 0.0;
      for (std::size_t point = 1; point < alignment.points.size (); ++point) {
        const auto& before = alignment.points[point - 1];
        auto& current = alignment.points[point];
        length_m +=
          std::hypot (current.x_m - before.x_m, current.z_m - before.z_m);
        current.distance_m = static_cast<float> (length_m);
      }
      alignment.length = length_m * mp_units::si::metre;
      return alignment;
    }

    void build_river_alignments (RiverNetwork& network,
                                 const FloodField& flood,
                                 const LakeCensus& census,
                                 const DrainageGraph& drainage,
                                 const ChannelGeometry& channels) {
      const TerrainGrid& grid = drainage.source_grid;
      const int width = static_cast<int> (grid.unique_width ());
      const int height = static_cast<int> (grid.unique_height ());
      const bool periodic = grid.topology == Topology::Torus;
      const float period_x = width * grid.spacing_x_m ();
      const float period_z = height * grid.spacing_y_m ();
      const auto water_cell = [&] (CellIndex cell) {
        return census.body[cell] != LakeCensus::dry || flood.ocean[cell];
      };
      const auto terminating_water_cell = [&] (CellIndex cell) {
        if (flood.ocean[cell])
          return true;
        const WaterBodyId id = census.body[cell];
        return id != LakeCensus::dry &&
               water_body_terminates_rivers (census.bodies[id]);
      };

      for (RiverReach& reach : network.reaches) {
        std::vector<CellIndex> cells = reach.cells;
        if (cells.empty ())
          continue;
        CellIndex next = drainage.receiver[cells.back ()];
        if (next != cells.back ())
          cells.push_back (next);
        // Carry sheet-rendered mouths far enough into their standing surface
        // for the ribbon to dissolve below it. Channel-like bodies and tiny
        // non-rendered depressions are instead crossed along their proven
        // receiver route, so they cannot punch dry gaps into an otherwise
        // continuous visible river; each body crossed end to end is recorded
        // so the sheet can yield it to the ribbon.
        std::vector<WaterBodyId> crossings;
        if (terminating_water_cell (cells.back ())) {
          next = drainage.receiver[cells.back ()];
          if (next != cells.back () && terminating_water_cell (next))
            cells.push_back (next);
        } else {
          while (water_cell (cells.back ()) &&
                 !terminating_water_cell (cells.back ())) {
            const WaterBodyId id = census.body[cells.back ()];
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
          float x = static_cast<float> (cell % width) * grid.spacing_x_m ();
          float z = static_cast<float> (cell / width) * grid.spacing_y_m ();
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
              const float t =
                route.receiver_interpolation.numerical_value_in (mp_units::one);
              const float cardinal_x =
                static_cast<float> (cardinal % width) * grid.spacing_x_m ();
              const float cardinal_z =
                static_cast<float> (cardinal / width) * grid.spacing_y_m ();
              const float diagonal_x =
                static_cast<float> (diagonal % width) * grid.spacing_x_m ();
              const float diagonal_z =
                static_cast<float> (diagonal / width) * grid.spacing_y_m ();
              x = cardinal_x +
                  (periodic
                     ? river_wrapped_delta (diagonal_x - cardinal_x, period_x)
                     : diagonal_x - cardinal_x) *
                    t;
              z = cardinal_z +
                  (periodic
                     ? river_wrapped_delta (diagonal_z - cardinal_z, period_z)
                     : diagonal_z - cardinal_z) *
                    t;
            }
          }
          if (periodic && !raw.empty ()) {
            x = raw.back ().x_m +
                river_wrapped_delta (x - raw.back ().x_m, period_x);
            z = raw.back ().z_m +
                river_wrapped_delta (z - raw.back ().z_m, period_z);
          }
          const bool terminating = terminating_water_cell (cell);
          const bool pooled = !terminating && water_cell (cell);
          // Fractional contributing area varies smoothly along a reach where
          // the all-or-nothing D8 accumulation steps, so widths derived from
          // it taper instead of popping between cells.
          const float area_m2 =
            channels.empty () ? drainage.contributing_area.values ()[cell]
                              : channels.area[cell].numerical_value_in (
                                  mp_units::si::metre * mp_units::si::metre);
          raw.push_back (
            { .x_m = x,
              .z_m = z,
              .contributing_area_m2 = area_m2,
              .slope = drainage.slope.values ()[cell],
              .waterfall = network.waterfall_by_cell[cell] != Waterfall::no_id
                             ? 1.0f
                             : 0.0f,
              .standing_water = terminating ? 1.0f : 0.0f,
              .water_level_m =
                flood.water_level.values ()[cell] * grid.height_scale_m (),
              .pooled = pooled ? 1.0f : 0.0f });
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
        remaining[id] = meters_value (reach.alignment.length) + downstream;
        return remaining[id];
      };
      for (RiverReach& reach : network.reaches) {
        const double from_start = solve (reach.id);
        for (RiverAlignmentPoint& point : reach.alignment.points)
          point.flow_distance_m =
            static_cast<float> (point.distance_m - from_start);
      }
    }
  }

  DrainageGraph analyze_drainage (const TerrainView& terrain,
                                  const DrainageParameters& parameters) {
    MOPPE_PROFILE_ZONE ("analyze_drainage");
    if (parameters.routing != DrainageRouting::D8)
      throw std::invalid_argument ("unsupported drainage routing");

    const TerrainGrid& source_grid = terrain.grid ();
    const std::size_t width = source_grid.unique_width ();
    const std::size_t height = source_grid.unique_height ();
    const std::size_t count = width * height;
    const bool periodic = source_grid.topology == Topology::Torus;
    const auto index = [width] (std::size_t x, std::size_t y) {
      return y * width + x;
    };

    std::vector<CellIndex> receiver (count, no_cell);
    std::vector<float> slope (count, 0.0f);
    {
      MOPPE_PROFILE_ZONE ("drainage.choose_receivers");
      for (std::size_t y = 0; y < height; ++y)
        for (std::size_t x = 0; x < width; ++x) {
          const std::size_t cell = index (x, y);
          receiver[cell] = static_cast<std::uint32_t> (cell);
          const meters_t elevation = terrain.elevation_at (grid_point (
            static_cast<std::ptrdiff_t> (x), static_cast<std::ptrdiff_t> (y)));
          auto steepest = 0.0f * mp_units::one;
          for (const Offset offset : neighbors) {
            const int raw_x = static_cast<int> (x) + offset.x;
            const int raw_y = static_cast<int> (y) + offset.y;
            if (!periodic &&
                (raw_x < 0 || raw_y < 0 || raw_x >= static_cast<int> (width) ||
                 raw_y >= static_cast<int> (height)))
              continue;
            const std::size_t nx = periodic ? wrapped (raw_x, width)
                                            : static_cast<std::size_t> (raw_x);
            const std::size_t ny = periodic ? wrapped (raw_y, height)
                                            : static_cast<std::size_t> (raw_y);
            const meters_t neighbor_elevation = terrain.elevation_at (
              grid_point (static_cast<std::ptrdiff_t> (nx),
                          static_cast<std::ptrdiff_t> (ny)));
            const meters_t distance =
              std::hypot (offset.x * source_grid.spacing_x_m (),
                          offset.y * source_grid.spacing_y_m ()) *
              mp_units::si::metre;
            const auto candidate = (elevation - neighbor_elevation) / distance;
            if (candidate > steepest * mp_units::one) {
              steepest = candidate;
              receiver[cell] = static_cast<std::uint32_t> (index (nx, ny));
            }
          }
          slope[cell] = steepest.numerical_value_in (mp_units::one);
        }
    }

    std::vector<std::uint32_t> order (count);
    std::iota (order.begin (), order.end (), 0u);
    std::stable_sort (
      order.begin (), order.end (), [&] (std::uint32_t a, std::uint32_t b) {
        const std::size_t ax = a % width, ay = a / width;
        const std::size_t bx = b % width, by = b / width;
        return terrain.at (ax, ay) > terrain.at (bx, by);
      });

    const float cell_area = square_meters_value (source_grid.cell_area ());
    std::vector<float> area (count, cell_area);
    for (const std::uint32_t cell : order)
      if (receiver[cell] != cell)
        area[receiver[cell]] += area[cell];

    std::vector<CellIndex> basin (count, no_cell);
    std::vector<CellIndex> sinks;
    for (auto i = order.rbegin (); i != order.rend (); ++i) {
      const std::uint32_t cell = *i;
      if (receiver[cell] == cell) {
        basin[cell] = cell;
        sinks.push_back (cell);
      } else {
        basin[cell] = basin[receiver[cell]];
      }
    }
    std::sort (sinks.begin (), sinks.end ());

    const FieldSamplingGrid2D domain {
      .width = width,
      .height = height,
      .max_x = source_grid.spacing_x_m () * static_cast<float> (width),
      .max_y = source_grid.spacing_y_m () * static_cast<float> (height)
    };
    return { .source_grid = source_grid,
             .receiver = std::move (receiver),
             .slope = SlopeRaster (ScalarRaster (domain, std::move (slope))),
             .contributing_area =
               ContributingAreaRaster (ScalarRaster (domain, std::move (area))),
             .basin = std::move (basin),
             .sinks = std::move (sinks) };
  }

  DrainageGraph analyze_wet_drainage (const TerrainView& terrain,
                                      const FloodField& flood,
                                      const LakeCensus& census,
                                      const DrainageParameters& parameters) {
    MOPPE_PROFILE_ZONE ("analyze_wet_drainage");
    if (parameters.routing != DrainageRouting::D8)
      throw std::invalid_argument ("unsupported drainage routing");

    const TerrainGrid& grid = terrain.grid ();
    const std::size_t width = grid.unique_width ();
    const std::size_t height = grid.unique_height ();
    const std::size_t count = width * height;
    if (flood.width () != width || flood.height () != height ||
        flood.source_grid.topology != grid.topology ||
        flood.source_grid.spacing_x != grid.spacing_x ||
        flood.source_grid.spacing_y != grid.spacing_y ||
        flood.source_grid.height_scale != grid.height_scale)
      throw std::invalid_argument ("flood field does not match terrain");
    if (census.body.size () != count)
      throw std::invalid_argument ("lake census does not match terrain");

    const bool periodic = grid.topology == Topology::Torus;
    const std::span<const float> surface = flood.water_level.values ();
    const auto index = [width] (std::size_t x, std::size_t y) {
      return y * width + x;
    };

    std::vector<CellIndex> receiver (count, no_cell);
    std::vector<float> slope (count, 0.0f);
    {
      MOPPE_PROFILE_ZONE ("wet_drainage.choose_receivers");
      for (std::size_t y = 0; y < height; ++y)
        for (std::size_t x = 0; x < width; ++x) {
          const std::size_t cell = index (x, y);
          receiver[cell] = flood.spill_receiver[cell];
          float steepest = 0.0f;
          for (const Offset offset : neighbors) {
            const int raw_x = static_cast<int> (x) + offset.x;
            const int raw_y = static_cast<int> (y) + offset.y;
            if (!periodic &&
                (raw_x < 0 || raw_y < 0 || raw_x >= static_cast<int> (width) ||
                 raw_y >= static_cast<int> (height)))
              continue;
            const std::size_t nx = periodic ? wrapped (raw_x, width)
                                            : static_cast<std::size_t> (raw_x);
            const std::size_t ny = periodic ? wrapped (raw_y, height)
                                            : static_cast<std::size_t> (raw_y);
            const std::size_t next = index (nx, ny);
            const meters_t distance =
              std::hypot (offset.x * grid.spacing_x_m (),
                          offset.y * grid.spacing_y_m ()) *
              mp_units::si::metre;
            const auto candidate =
              (surface[cell] - surface[next]) * grid.height_scale / distance;
            if (candidate > steepest) {
              steepest = candidate.numerical_value_in (mp_units::one);
              receiver[cell] = static_cast<std::uint32_t> (next);
            }
          }
          slope[cell] = steepest;
        }
    }

    // A flat inland body has one route-proven spill. Replace any incidental
    // priority-flood partition inside it with a deterministic breadth-first
    // tree leading to that spill, so the body's full discharge stays whole.
    {
      MOPPE_PROFILE_ZONE ("wet_drainage.route_lake_interiors");
      std::vector<std::uint8_t> routed (count, 0);
      std::queue<std::uint32_t> body_frontier;
      for (const WaterBody& body : census.bodies) {
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
            if (!periodic &&
                (raw_x < 0 || raw_y < 0 || raw_x >= static_cast<int> (width) ||
                 raw_y >= static_cast<int> (height)))
              continue;
            const std::size_t nx = periodic ? wrapped (raw_x, width)
                                            : static_cast<std::size_t> (raw_x);
            const std::size_t ny = periodic ? wrapped (raw_y, height)
                                            : static_cast<std::size_t> (raw_y);
            const std::uint32_t next =
              static_cast<std::uint32_t> (index (nx, ny));
            if (routed[next] || census.body[next] != body.id)
              continue;
            routed[next] = 1;
            receiver[next] = cell;
            body_frontier.push (next);
          }
        }
      }
    }

    // Equal-height lake routes cannot be accumulated by elevation order.
    // Receiver edges either lower the filled surface or follow the acyclic
    // priority-flood forest, so a general topological pass handles both.
    const float cell_area = square_meters_value (grid.cell_area ());
    std::vector<float> area (count, cell_area);
    std::vector<std::uint32_t> order;
    order.reserve (count);
    {
      MOPPE_PROFILE_ZONE ("wet_drainage.accumulate_contributing_area");
      std::vector<std::uint32_t> donors (count, 0);
      for (std::uint32_t cell = 0; cell < count; ++cell)
        if (receiver[cell] != cell)
          ++donors[receiver[cell]];

      std::priority_queue<std::uint32_t,
                          std::vector<std::uint32_t>,
                          std::greater<std::uint32_t>>
        ready;
      for (std::uint32_t cell = 0; cell < count; ++cell)
        if (donors[cell] == 0)
          ready.push (cell);

      while (!ready.empty ()) {
        const std::uint32_t cell = ready.top ();
        ready.pop ();
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

    std::vector<CellIndex> basin (count, no_cell);
    std::vector<CellIndex> sinks;
    {
      MOPPE_PROFILE_ZONE ("wet_drainage.assign_basins");
      for (auto i = order.rbegin (); i != order.rend (); ++i) {
        const std::uint32_t cell = *i;
        if (receiver[cell] == cell) {
          basin[cell] = cell;
          sinks.push_back (cell);
        } else {
          basin[cell] = basin[receiver[cell]];
        }
      }
      std::sort (sinks.begin (), sinks.end ());
    }

    const FieldSamplingGrid2D domain {
      .width = width,
      .height = height,
      .max_x = grid.spacing_x_m () * static_cast<float> (width),
      .max_y = grid.spacing_y_m () * static_cast<float> (height)
    };
    return { .source_grid = grid,
             .receiver = std::move (receiver),
             .slope = SlopeRaster (ScalarRaster (domain, std::move (slope))),
             .contributing_area =
               ContributingAreaRaster (ScalarRaster (domain, std::move (area))),
             .basin = std::move (basin),
             .sinks = std::move (sinks) };
  }

  WaterNetwork analyze_water_network (const FloodField& flood,
                                      const LakeCensus& census,
                                      const DrainageGraph& drainage) {
    MOPPE_PROFILE_ZONE ("analyze_water_network");
    const std::size_t count = flood.width () * flood.height ();
    if (census.body.size () != count || drainage.width () != flood.width () ||
        drainage.height () != flood.height ())
      throw std::invalid_argument ("water analyses do not share a domain");

    WaterNetwork network;
    network.bodies.reserve (census.bodies.size ());
    for (const WaterBody& body : census.bodies) {
      WaterBodyFlow flow { .body_id = body.id,
                           .inflow_area =
                             0.0f * mp_units::si::metre * mp_units::si::metre,
                           .outlet_cell = body.outlet_cell,
                           .spill_cell = body.spill_cell,
                           .downstream_cell = WaterBodyFlow::no_cell,
                           .outflow_area =
                             0.0f * mp_units::si::metre * mp_units::si::metre };
      if (body.spill_cell != WaterBody::no_cell) {
        flow.downstream_cell = drainage.receiver[body.spill_cell];
        flow.outflow_area = drainage.contributing_area.sample (
          body.spill_cell % drainage.width (),
          body.spill_cell / drainage.width ());
      }
      network.bodies.push_back (std::move (flow));
    }

    for (std::uint32_t cell = 0; cell < count; ++cell) {
      const std::uint32_t next = drainage.receiver[cell];
      if (next == cell || census.body[cell] != LakeCensus::dry ||
          census.body[next] == LakeCensus::dry)
        continue;
      WaterBodyFlow& flow = network.bodies[census.body[next]];
      const square_meters_t area = drainage.contributing_area.sample (
        cell % drainage.width (), cell / drainage.width ());
      flow.inlets.push_back ({ .upstream_cell = cell,
                               .water_cell = next,
                               .contributing_area = area });
      flow.inflow_area += area;
    }
    return network;
  }

  RiverNetwork
  extract_river_network (const FloodField& flood,
                         const LakeCensus& census,
                         const DrainageGraph& drainage,
                         square_meters_t minimum_area,
                         const WaterfallParameters& waterfall_parameters) {
    MOPPE_PROFILE_ZONE ("extract_river_network");
    const std::size_t count = flood.width () * flood.height ();
    if (!std::isfinite (square_meters_value (minimum_area)) ||
        minimum_area < 0.0f * mp_units::si::metre * mp_units::si::metre)
      throw std::invalid_argument ("river area threshold must be finite");
    if (!std::isfinite (meters_value (waterfall_parameters.minimum_drop)) ||
        waterfall_parameters.minimum_drop < 0.0f * mp_units::si::metre ||
        !std::isfinite (waterfall_parameters.minimum_slope.numerical_value_in (
          mp_units::one)) ||
        waterfall_parameters.minimum_slope <
          0.0f * terrain_slope[mp_units::one])
      throw std::invalid_argument ("waterfall thresholds must be finite");
    if (census.body.size () != count || flood.ocean.size () != count ||
        drainage.width () != flood.width () ||
        drainage.height () != flood.height ())
      throw std::invalid_argument ("river analyses do not share a domain");

    std::vector<std::uint8_t> eligible (count, 0);
    for (std::uint32_t cell = 0; cell < count; ++cell)
      eligible[cell] =
        census.body[cell] == LakeCensus::dry && !flood.ocean[cell] &&
        drainage.receiver[cell] != cell &&
        drainage.contributing_area.sample (
          cell % drainage.width (), cell / drainage.width ()) >= minimum_area;

    std::vector<std::uint32_t> river_donors (count, 0);
    for (std::uint32_t cell = 0; cell < count; ++cell) {
      const std::uint32_t next = drainage.receiver[cell];
      if (eligible[cell] && eligible[next])
        ++river_donors[next];
    }

    std::vector<WaterBodyId> body_at_spill (count, no_water_body);
    WaterBodyId ocean_body = no_water_body;
    for (const WaterBody& body : census.bodies) {
      if (body.spill_cell != WaterBody::no_cell)
        body_at_spill[body.spill_cell] = body.id;
      if (body.classification == WaterBodyClass::Sea &&
          ocean_body == no_water_body)
        ocean_body = body.id;
    }

    RiverNetwork network {
      .minimum_area = minimum_area,
      .waterfall_parameters = waterfall_parameters,
      .reach_by_cell = std::vector<RiverReachId> (count, RiverReach::no_id),
      .waterfall_by_cell = std::vector<WaterfallId> (count, Waterfall::no_id),
      .body_traversed = std::vector<std::uint8_t> (census.bodies.size (), 0)
    };
    for (std::uint32_t start = 0; start < count; ++start) {
      if (!eligible[start] ||
          network.reach_by_cell[start] != RiverReach::no_id ||
          (river_donors[start] == 1 && body_at_spill[start] == no_water_body))
        continue;
      RiverReach reach {
        .id = static_cast<std::uint32_t> (network.reaches.size ()),
        .upstream_body = body_at_spill[start],
        .downstream_body = no_water_body,
        .downstream_ocean = false,
        .downstream_reach = RiverReach::no_id,
        .upstream_area = drainage.contributing_area.sample (
          start % drainage.width (), start / drainage.width ()),
        .downstream_area = drainage.contributing_area.sample (
          start % drainage.width (), start / drainage.width ()),
        .maximum_slope = 0.0f * terrain_slope[mp_units::one]
      };
      std::uint32_t cell = start;
      while (eligible[cell] &&
             network.reach_by_cell[cell] == RiverReach::no_id) {
        network.reach_by_cell[cell] = reach.id;
        reach.cells.push_back (cell);
        reach.downstream_area = drainage.contributing_area.sample (
          cell % drainage.width (), cell / drainage.width ());
        reach.maximum_slope =
          std::max (reach.maximum_slope,
                    drainage.slope.sample (cell % drainage.width (),
                                           cell / drainage.width ()));
        const std::uint32_t next = drainage.receiver[cell];
        if (!eligible[next] || river_donors[next] != 1)
          break;
        cell = next;
      }
      const std::uint32_t next = drainage.receiver[reach.cells.back ()];
      if (census.body[next] != LakeCensus::dry)
        reach.downstream_body = census.body[next];
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
        reach.downstream_reach = network.reach_by_cell[next];
    }
    // A body that does not terminate rivers — a transient depression or a
    // channel-like flooded segment — is drainage structure, not a rendered
    // lake. Link its inlet reach directly to the spill reach so the
    // continuous alignment, flow coordinate, and junction surface bridge it.
    for (RiverReach& reach : network.reaches) {
      if (reach.downstream_reach != RiverReach::no_id ||
          reach.downstream_body == no_water_body)
        continue;
      const WaterBody& body = census.bodies[reach.downstream_body];
      if (water_body_terminates_rivers (body) ||
          body.spill_cell == WaterBody::no_cell || !eligible[body.spill_cell])
        continue;
      reach.downstream_reach = network.reach_by_cell[body.spill_cell];
    }

    for (const RiverReach& reach : network.reaches) {
      const square_meters_t reference_area =
        std::max (minimum_area, drainage.source_grid.cell_area ());
      Waterfall selected {};
      bool has_selected = false;
      std::size_t last_candidate = 0;
      float selected_score = 0.0f;
      const auto emit_selected = [&] {
        if (!has_selected)
          return;
        selected.id = static_cast<std::uint32_t> (network.waterfalls.size ());
        network.waterfall_by_cell[selected.lip_cell] = selected.id;
        network.waterfalls.push_back (selected);
        has_selected = false;
        selected_score = 0.0f;
      };
      for (std::size_t i = 0; i < reach.cells.size (); ++i) {
        const std::uint32_t cell = reach.cells[i];
        const std::uint32_t next = drainage.receiver[cell];
        const meters_t distance =
          receiver_distance (cell, next, drainage.source_grid);
        const slope_t slope = drainage.slope.sample (cell % drainage.width (),
                                                     cell / drainage.width ());
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
        const square_meters_t area = drainage.contributing_area.sample (
          cell % drainage.width (), cell / drainage.width ());
        const float score =
          meters_value (drop) *
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
    if (channels.domain ().grid () != drainage.source_grid)
      throw std::invalid_argument (
        "channel geometry does not share the drainage lattice");
    // Rebuild only the continuous geometry: the D8 reach topology above
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
