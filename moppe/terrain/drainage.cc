#include <moppe/terrain/drainage.hh>

#include <algorithm>
#include <array>
#include <cmath>
#include <numeric>
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
}
