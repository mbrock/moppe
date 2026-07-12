#include <moppe/terrain/moisture.hh>

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <queue>
#include <vector>

namespace moppe::terrain {
  ScalarRaster analyze_moisture (const FloodField& flood,
                                 const LakeCensus& census,
                                 const DrainageGraph& drainage,
                                 const MoistureParameters& parameters) {
    const TerrainGrid& grid = flood.source_grid;
    const int width = static_cast<int> (grid.unique_width ());
    const int height = static_cast<int> (grid.unique_height ());
    const std::size_t count = grid.unique_size ();
    const bool periodic = grid.topology == Topology::Torus;
    const float spacing = 0.5f * (grid.spacing_x_m () + grid.spacing_y_m ());

    // Multi-source BFS distance (in grid steps) from every standing-water
    // cell. Grid distance is close enough to metric distance here: the
    // moisture curve is an exponential falloff, not a measurement.
    constexpr std::uint16_t far_away = 0xffff;
    std::vector<std::uint16_t> steps (count, far_away);
    std::queue<std::uint32_t> frontier;
    for (std::uint32_t cell = 0; cell < count; ++cell)
      if (census.body[cell] != LakeCensus::dry || flood.ocean[cell]) {
        steps[cell] = 0;
        frontier.push (cell);
      }
    while (!frontier.empty ()) {
      const std::uint32_t cell = frontier.front ();
      frontier.pop ();
      const int x = static_cast<int> (cell) % width;
      const int y = static_cast<int> (cell) / width;
      const std::uint16_t next = steps[cell] + 1;
      if (next * spacing > 4.0f * parameters.water_reach_m)
        continue;
      for (int dy = -1; dy <= 1; ++dy)
        for (int dx = -1; dx <= 1; ++dx) {
          if (dx == 0 && dy == 0)
            continue;
          int nx = x + dx;
          int ny = y + dy;
          if (periodic) {
            nx = (nx + width) % width;
            ny = (ny + height) % height;
          } else if (nx < 0 || nx >= width || ny < 0 || ny >= height)
            continue;
          const std::uint32_t neighbor =
            static_cast<std::uint32_t> (ny) * width + nx;
          if (steps[neighbor] <= next)
            continue;
          steps[neighbor] = next;
          frontier.push (neighbor);
        }
    }

    const float cell_area = square_meters_value (grid.cell_area ());
    std::vector<float> moisture (count);
    for (std::size_t cell = 0; cell < count; ++cell) {
      const float distance = steps[cell] == far_away
                               ? 4.0f * parameters.water_reach_m
                               : steps[cell] * spacing;
      const float near_water = std::exp (-distance / parameters.water_reach_m);
      const float area = drainage.contributing_area.values ()[cell];
      const float damp =
        std::clamp (std::log2 (std::max (area / cell_area, 1.0f)) /
                      parameters.drainage_span_log2,
                    0.0f,
                    1.0f);
      moisture[cell] =
        std::clamp ((1.0f - parameters.drainage_weight) * near_water +
                      parameters.drainage_weight * damp,
                    0.0f,
                    1.0f);
    }
    return ScalarRaster ({ .width = static_cast<std::size_t> (width),
                           .height = static_cast<std::size_t> (height) },
                         std::move (moisture));
  }
}
