#include <moppe/terrain/drainage.hh>
#include <moppe/terrain/flood.hh>

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
      .waterfall_by_cell = std::vector<WaterfallId> (count, Waterfall::no_id)
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
    return network;
  }
}
