#include <moppe/terrain/flood.hh>

#include <algorithm>
#include <array>
#include <cmath>
#include <limits>
#include <queue>
#include <stdexcept>
#include <utility>

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

    struct Cell {
      float level;
      std::uint32_t index;
    };

    struct HigherCell {
      bool operator () (const Cell& left, const Cell& right) const noexcept {
	if (left.level != right.level)
	  return left.level > right.level;
	return left.index > right.index;
      }
    };
  }

  FloodField
  analyze_standing_water (const TerrainView& terrain, float sea_level)
  {
    if (!std::isfinite (sea_level))
      throw std::invalid_argument ("standing-water sea level must be finite");

    const TerrainGrid& grid = terrain.grid ();
    const std::size_t width = grid.unique_width ();
    const std::size_t height = grid.unique_height ();
    const std::size_t count = width * height;
    const bool periodic = grid.topology == Topology::Torus;
    const auto index = [width] (std::size_t x, std::size_t y) {
      return y * width + x;
    };

    std::vector<float> water
      (count, std::numeric_limits<float>::infinity ());
    std::vector<float> depth (count, 0.0f);
    std::vector<std::uint32_t> receiver (count, 0);
    std::vector<std::uint8_t> visited (count, 0);
    std::vector<std::uint32_t> outlets;
    std::priority_queue<Cell, std::vector<Cell>, HigherCell> frontier;

    for (std::size_t y = 0; y < height; ++y)
      for (std::size_t x = 0; x < width; ++x) {
	const std::uint32_t cell = static_cast<std::uint32_t> (index (x, y));
	if (terrain.at (x, y) <= sea_level) {
	  water[cell] = sea_level;
	  receiver[cell] = cell;
	  visited[cell] = 1;
	  outlets.push_back (cell);
	  frontier.push ({ sea_level, cell });
	}
      }

    // An all-land torus has no geometric boundary. Root its minimax water
    // surface at the deterministic global minimum so the analysis remains
    // defined while making that endorheic choice explicit in `outlets`.
    if (outlets.empty ()) {
      std::uint32_t minimum_cell = 0;
      float minimum = terrain.at (0, 0);
      for (std::uint32_t cell = 1; cell < count; ++cell) {
	const float value = terrain.at (cell % width, cell / width);
	if (value < minimum) {
	  minimum = value;
	  minimum_cell = cell;
	}
      }
      water[minimum_cell] = minimum;
      receiver[minimum_cell] = minimum_cell;
      visited[minimum_cell] = 1;
      outlets.push_back (minimum_cell);
      frontier.push ({ minimum, minimum_cell });
    }

    while (!frontier.empty ()) {
      const Cell current = frontier.top ();
      frontier.pop ();
      const std::size_t x = current.index % width;
      const std::size_t y = current.index / width;
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
	const std::uint32_t next = static_cast<std::uint32_t> (index (nx, ny));
	if (visited[next])
	  continue;
	visited[next] = 1;
	water[next] = std::max (terrain.at (nx, ny), current.level);
	receiver[next] = current.index;
	frontier.push ({ water[next], next });
      }
    }

    for (std::size_t y = 0; y < height; ++y)
      for (std::size_t x = 0; x < width; ++x) {
	const std::size_t cell = index (x, y);
	depth[cell] = std::max (0.0f, water[cell] - terrain.at (x, y));
      }

    const Domain2D domain {
      .width = width,
      .height = height,
      .max_x = grid.spacing_x * static_cast<float> (width),
      .max_y = grid.spacing_y * static_cast<float> (height)
    };
    return {
      .source_grid = grid,
      .sea_level = sea_level,
      .water_level = ScalarRaster (domain, std::move (water)),
      .water_depth = ScalarRaster (domain, std::move (depth)),
      .spill_receiver = std::move (receiver),
      .outlets = std::move (outlets)
    };
  }
}
