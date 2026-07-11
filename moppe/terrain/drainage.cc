#include <moppe/terrain/drainage.hh>
#include <moppe/terrain/flood.hh>

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

    constexpr std::array<Offset, 8> neighbors {{
      { -1, -1 }, { 0, -1 }, { 1, -1 }, { -1, 0 },
      { 1, 0 }, { -1, 1 }, { 0, 1 }, { 1, 1 }
    }};

    std::size_t wrapped (int value, std::size_t period) {
      const int n = static_cast<int> (period);
      const int result = value % n;
      return static_cast<std::size_t> (result < 0 ? result + n : result);
    }
  }

  DrainageGraph
  analyze_drainage (const TerrainView& terrain,
		    const DrainageParameters& parameters)
  {
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

    std::vector<std::uint32_t> receiver (count);
    std::vector<float> slope (count, 0.0f);
    for (std::size_t y = 0; y < height; ++y)
      for (std::size_t x = 0; x < width; ++x) {
        const std::size_t cell = index (x, y);
        receiver[cell] = static_cast<std::uint32_t> (cell);
        const float elevation = terrain.at (x, y) * source_grid.height_scale;
        float steepest = 0.0f;
        for (const Offset offset : neighbors) {
          const int raw_x = static_cast<int> (x) + offset.x;
          const int raw_y = static_cast<int> (y) + offset.y;
          if (!periodic && (raw_x < 0 || raw_y < 0
			    || raw_x >= static_cast<int> (width)
			    || raw_y >= static_cast<int> (height)))
            continue;
          const std::size_t nx = periodic ? wrapped (raw_x, width)
					  : static_cast<std::size_t> (raw_x);
          const std::size_t ny = periodic ? wrapped (raw_y, height)
					  : static_cast<std::size_t> (raw_y);
          const float neighbor_elevation =
		    terrain.at (nx, ny) * source_grid.height_scale;
          const float distance = std::hypot
		    (offset.x * source_grid.spacing_x,
		     offset.y * source_grid.spacing_y);
          const float candidate = (elevation - neighbor_elevation) / distance;
          if (candidate > steepest) {
            steepest = candidate;
            receiver[cell] = static_cast<std::uint32_t> (index (nx, ny));
          }
        }
        slope[cell] = steepest;
      }

    std::vector<std::uint32_t> order (count);
    std::iota (order.begin (), order.end (), 0u);
    std::stable_sort
      (order.begin (), order.end (), [&] (std::uint32_t a, std::uint32_t b) {
        const std::size_t ax = a % width, ay = a / width;
        const std::size_t bx = b % width, by = b / width;
        return terrain.at (ax, ay) > terrain.at (bx, by);
      });

    const float cell_area = source_grid.spacing_x * source_grid.spacing_y;
    std::vector<float> area (count, cell_area);
    for (const std::uint32_t cell : order)
      if (receiver[cell] != cell)
        area[receiver[cell]] += area[cell];

    std::vector<std::uint32_t> basin (count);
    std::vector<std::uint32_t> sinks;
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

    const Domain2D domain {
      .width = width,
      .height = height,
      .max_x = source_grid.spacing_x * static_cast<float> (width),
      .max_y = source_grid.spacing_y * static_cast<float> (height)
    };
    return {
      .source_grid = source_grid,
      .receiver = std::move (receiver),
      .slope = ScalarRaster (domain, std::move (slope)),
      .contributing_area = ScalarRaster (domain, std::move (area)),
      .basin = std::move (basin),
      .sinks = std::move (sinks)
    };
  }

  DrainageGraph
  analyze_wet_drainage (const TerrainView& terrain,
			const FloodField& flood,
			const LakeCensus& census,
			const DrainageParameters& parameters)
  {
    if (parameters.routing != DrainageRouting::D8)
      throw std::invalid_argument ("unsupported drainage routing");

    const TerrainGrid& grid = terrain.grid ();
    const std::size_t width = grid.unique_width ();
    const std::size_t height = grid.unique_height ();
    const std::size_t count = width * height;
    if (flood.width () != width || flood.height () != height
	|| flood.source_grid.topology != grid.topology
	|| flood.source_grid.spacing_x != grid.spacing_x
	|| flood.source_grid.spacing_y != grid.spacing_y
	|| flood.source_grid.height_scale != grid.height_scale)
      throw std::invalid_argument ("flood field does not match terrain");
    if (census.body.size () != count)
      throw std::invalid_argument ("lake census does not match terrain");

    const bool periodic = grid.topology == Topology::Torus;
    const std::span<const float> surface = flood.water_level.values ();
    const auto index = [width] (std::size_t x, std::size_t y) {
      return y * width + x;
    };

    std::vector<std::uint32_t> receiver (count);
    std::vector<float> slope (count, 0.0f);
    for (std::size_t y = 0; y < height; ++y)
      for (std::size_t x = 0; x < width; ++x) {
	const std::size_t cell = index (x, y);
	receiver[cell] = flood.spill_receiver[cell];
	float steepest = 0.0f;
	for (const Offset offset : neighbors) {
	  const int raw_x = static_cast<int> (x) + offset.x;
	  const int raw_y = static_cast<int> (y) + offset.y;
	  if (!periodic && (raw_x < 0 || raw_y < 0
			      || raw_x >= static_cast<int> (width)
			      || raw_y >= static_cast<int> (height)))
	    continue;
	  const std::size_t nx = periodic ? wrapped (raw_x, width)
				    : static_cast<std::size_t> (raw_x);
	  const std::size_t ny = periodic ? wrapped (raw_y, height)
				    : static_cast<std::size_t> (raw_y);
	  const std::size_t next = index (nx, ny);
	  const float distance = std::hypot
	    (offset.x * grid.spacing_x, offset.y * grid.spacing_y);
	  const float candidate = (surface[cell] - surface[next])
	    * grid.height_scale / distance;
	  if (candidate > steepest) {
	    steepest = candidate;
	    receiver[cell] = static_cast<std::uint32_t> (next);
	  }
	}
	slope[cell] = steepest;
      }

    // A flat inland body has one route-proven spill. Replace any incidental
    // priority-flood partition inside it with a deterministic breadth-first
    // tree leading to that spill, so the body's full discharge stays whole.
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
	  if (!periodic && (raw_x < 0 || raw_y < 0
			      || raw_x >= static_cast<int> (width)
			      || raw_y >= static_cast<int> (height)))
	    continue;
	  const std::size_t nx = periodic ? wrapped (raw_x, width)
				    : static_cast<std::size_t> (raw_x);
	  const std::size_t ny = periodic ? wrapped (raw_y, height)
				    : static_cast<std::size_t> (raw_y);
	  const std::uint32_t next = static_cast<std::uint32_t>
	    (index (nx, ny));
	  if (routed[next] || census.body[next] != body.id)
	    continue;
	  routed[next] = 1;
	  receiver[next] = cell;
	  body_frontier.push (next);
	}
      }
    }

    // Equal-height lake routes cannot be accumulated by elevation order.
    // Receiver edges either lower the filled surface or follow the acyclic
    // priority-flood forest, so a general topological pass handles both.
    std::vector<std::uint32_t> donors (count, 0);
    for (std::uint32_t cell = 0; cell < count; ++cell)
      if (receiver[cell] != cell)
	++donors[receiver[cell]];

    std::priority_queue<std::uint32_t, std::vector<std::uint32_t>,
			std::greater<std::uint32_t>> ready;
    for (std::uint32_t cell = 0; cell < count; ++cell)
      if (donors[cell] == 0)
	ready.push (cell);

    const float cell_area = grid.spacing_x * grid.spacing_y;
    std::vector<float> area (count, cell_area);
    std::vector<std::uint32_t> order;
    order.reserve (count);
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
    if (order.size () != count)
      throw std::logic_error ("wet drainage routing contains a cycle");

    std::vector<std::uint32_t> basin (count);
    std::vector<std::uint32_t> sinks;
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

    const Domain2D domain {
      .width = width,
      .height = height,
      .max_x = grid.spacing_x * static_cast<float> (width),
      .max_y = grid.spacing_y * static_cast<float> (height)
    };
    return {
      .source_grid = grid,
      .receiver = std::move (receiver),
      .slope = ScalarRaster (domain, std::move (slope)),
      .contributing_area = ScalarRaster (domain, std::move (area)),
      .basin = std::move (basin),
      .sinks = std::move (sinks)
    };
  }

  WaterNetwork
  analyze_water_network (const FloodField& flood,
			 const LakeCensus& census,
			 const DrainageGraph& drainage)
  {
    const std::size_t count = flood.width () * flood.height ();
    if (census.body.size () != count
	|| drainage.width () != flood.width ()
	|| drainage.height () != flood.height ())
      throw std::invalid_argument ("water analyses do not share a domain");

    WaterNetwork network;
    network.bodies.reserve (census.bodies.size ());
    for (const WaterBody& body : census.bodies) {
      WaterBodyFlow flow {
	.body_id = body.id,
	.inflow_area_m2 = 0.0f,
	.outlet_cell = body.outlet_cell,
	.spill_cell = body.spill_cell,
	.downstream_cell = WaterBodyFlow::no_cell,
	.outflow_area_m2 = 0.0f
      };
      if (body.spill_cell != WaterBody::no_cell) {
	flow.downstream_cell = drainage.receiver[body.spill_cell];
	flow.outflow_area_m2 =
	  drainage.contributing_area.values ()[body.spill_cell];
      }
      network.bodies.push_back (std::move (flow));
    }

    for (std::uint32_t cell = 0; cell < count; ++cell) {
      const std::uint32_t next = drainage.receiver[cell];
      if (next == cell || census.body[cell] != LakeCensus::dry
	  || census.body[next] == LakeCensus::dry)
	continue;
      WaterBodyFlow& flow = network.bodies[census.body[next]];
      const float area = drainage.contributing_area.values ()[cell];
      flow.inlets.push_back ({
	.upstream_cell = cell,
	.water_cell = next,
	.contributing_area_m2 = area
      });
      flow.inflow_area_m2 += area;
    }
    return network;
  }
}
