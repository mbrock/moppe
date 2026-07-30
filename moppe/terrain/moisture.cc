#include <moppe/terrain/moisture.hh>

#include <moppe/profile.hh>

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <queue>
#include <vector>

namespace moppe::terrain {
  MoistureMap analyze_moisture (const FloodField& flood,
                                const WaterBodyMembership& water_bodies,
                                const DrainageGraph& drainage,
                                const MoistureParameters& parameters) {
    MOPPE_PROFILE_ZONE ("analyze_moisture");
    const TerrainDomain& grid = flood.domain ();
    const int width = static_cast<int> (grid.width ());
    const int height = static_cast<int> (grid.height ());
    const std::size_t count = grid.width () * grid.height ();
    const float spacing =
      0.5f * ((grid.spacing_x ()).numerical_value_in (moppe::u::m) +
              (grid.spacing_z ()).numerical_value_in (moppe::u::m));

    // Multi-source BFS distance (in grid steps) from every standing-water
    // cell. Grid distance is close enough to metric distance here: the
    // moisture curve is an exponential falloff, not a measurement.
    constexpr std::uint16_t far_away = 0xffff;
    std::vector<std::uint16_t> steps (count, far_away);
    std::queue<std::uint32_t> frontier;
    {
      MOPPE_PROFILE_ZONE ("moisture.distance_from_water");
      for (std::uint32_t cell = 0; cell < count; ++cell)
        if (water_bodies.body_at (CellIndex { cell }) !=
              WaterBodyMembership::dry ||
            flood.ocean[cell]) {
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
            const int nx = wrap_index (x + dx, width);
            const int ny = wrap_index (y + dy, height);
            const std::uint32_t neighbor =
              static_cast<std::uint32_t> (ny) * width + nx;
            if (steps[neighbor] <= next)
              continue;
            steps[neighbor] = next;
            frontier.push (neighbor);
          }
      }
    }

    const float cell_area =
      (grid.cell_area ()).numerical_value_in (moppe::u::m * moppe::u::m);
    std::vector<SurfaceMoisture> moisture (count);
    std::vector<SoilWetness> wetness (count);
    {
      MOPPE_PROFILE_ZONE ("moisture.combine_water_and_drainage");
      for (std::size_t cell = 0; cell < count; ++cell) {
        const float distance = steps[cell] == far_away
                                 ? 4.0f * parameters.water_reach_m
                                 : steps[cell] * spacing;
        const float near_water =
          std::exp (-distance / parameters.water_reach_m);
        const float area =
          drainage.contributing_area_at (cell).numerical_value_in (u::m * u::m);
        // How many cells' worth of ground drains through this one. A cell
        // that drains only itself counts once, which is where the logarithm
        // starts.
        const float upstream = std::max (area / cell_area, 1.0f);
        const float damp = std::clamp (
          std::log2 (upstream) / parameters.drainage_span_log2, 0.0f, 1.0f);
        moisture[cell] =
          std::clamp ((1.0f - parameters.drainage_weight) * near_water +
                        parameters.drainage_weight * damp,
                      0.0f,
                      1.0f) *
          surface_moisture[one];

        const float fall =
          std::max (drainage.slope_at (cell).numerical_value_in (one),
                    parameters.flattest_believable_slope);
        const float index = std::log2 (upstream) - std::log2 (fall);
        wetness[cell] =
          std::clamp (index / parameters.wetness_span_octaves, 0.0f, 1.0f) *
          soil_wetness[one];
      }
    }
    return MoistureMap (grid, std::move (moisture), std::move (wetness));
  }
}
