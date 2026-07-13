#include <moppe/terrain/flood.hh>

#include <moppe/profile.hh>

#include <algorithm>
#include <array>
#include <cmath>
#include <limits>
#include <queue>
#include <stdexcept>
#include <utility>

namespace moppe::terrain {
  namespace {
    struct FloodOffset {
      int x;
      int y;
    };

    constexpr std::array<FloodOffset, 8> flood_neighbors { { { -1, -1 },
                                                             { 0, -1 },
                                                             { 1, -1 },
                                                             { -1, 0 },
                                                             { 1, 0 },
                                                             { -1, 1 },
                                                             { 0, 1 },
                                                             { 1, 1 } } };

    std::size_t flood_wrapped (int value, std::size_t period) {
      const int n = static_cast<int> (period);
      const int result = value % n;
      return static_cast<std::size_t> (result < 0 ? result + n : result);
    }

    struct Cell {
      float level;
      std::uint32_t index;
    };

    struct HigherCell {
      bool operator() (const Cell& left, const Cell& right) const noexcept {
        if (left.level != right.level)
          return left.level > right.level;
        return left.index > right.index;
      }
    };
  }

  FloodField analyze_standing_water (const TerrainView& terrain,
                                     float sea_level) {
    MOPPE_PROFILE_ZONE ("analyze_standing_water");
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

    std::vector<float> water (count, std::numeric_limits<float>::infinity ());
    std::vector<float> depth (count, 0.0f);
    std::vector<CellIndex> receiver (count, CellIndex { 0 });
    std::vector<std::uint8_t> visited (count, 0);
    std::vector<CellIndex> outlets;
    std::priority_queue<Cell, std::vector<Cell>, HigherCell> frontier;

    // A torus has no exterior boundary that identifies the ocean. Treat the
    // largest connected below-sea component as the global ocean; enclosed
    // low components must earn their own higher spill level. Scan order
    // breaks equal-size ties deterministically.
    std::queue<std::uint32_t> sea_frontier;
    std::vector<std::uint8_t> submerged_seen (count, 0);
    std::vector<std::uint32_t> component;
    std::vector<std::uint32_t> global_ocean;
    {
      MOPPE_PROFILE_ZONE ("flood.find_ocean_components");
      for (std::uint32_t origin = 0; origin < count; ++origin) {
        if (submerged_seen[origin] ||
            terrain.at (origin % width, origin / width) > sea_level)
          continue;
        component.clear ();
        submerged_seen[origin] = 1;
        sea_frontier.push (origin);
        while (!sea_frontier.empty ()) {
          const std::uint32_t cell = sea_frontier.front ();
          sea_frontier.pop ();
          component.push_back (cell);
          const std::size_t x = cell % width;
          const std::size_t y = cell / width;
          for (const FloodOffset offset : flood_neighbors) {
            const int raw_x = static_cast<int> (x) + offset.x;
            const int raw_y = static_cast<int> (y) + offset.y;
            if (!periodic &&
                (raw_x < 0 || raw_y < 0 || raw_x >= static_cast<int> (width) ||
                 raw_y >= static_cast<int> (height)))
              continue;
            const std::size_t nx = periodic ? flood_wrapped (raw_x, width)
                                            : static_cast<std::size_t> (raw_x);
            const std::size_t ny = periodic ? flood_wrapped (raw_y, height)
                                            : static_cast<std::size_t> (raw_y);
            const std::uint32_t next =
              static_cast<std::uint32_t> (index (nx, ny));
            if (submerged_seen[next] || terrain.at (nx, ny) > sea_level)
              continue;
            submerged_seen[next] = 1;
            sea_frontier.push (next);
          }
        }
        if (component.size () > global_ocean.size ())
          global_ocean = component;
      }
    }

    std::vector<std::uint8_t> ocean_cell (count, 0);
    if (!global_ocean.empty ()) {
      MOPPE_PROFILE_ZONE ("flood.seed_global_ocean");
      for (const std::uint32_t cell : global_ocean)
        ocean_cell[cell] = 1;
      const std::uint32_t root = global_ocean.front ();
      water[root] = sea_level;
      receiver[root] = root;
      visited[root] = 1;
      outlets.push_back (root);
      sea_frontier.push (root);
      while (!sea_frontier.empty ()) {
        const std::uint32_t cell = sea_frontier.front ();
        sea_frontier.pop ();
        frontier.push ({ sea_level, cell });
        const std::size_t x = cell % width;
        const std::size_t y = cell / width;
        for (const FloodOffset offset : flood_neighbors) {
          const int raw_x = static_cast<int> (x) + offset.x;
          const int raw_y = static_cast<int> (y) + offset.y;
          if (!periodic &&
              (raw_x < 0 || raw_y < 0 || raw_x >= static_cast<int> (width) ||
               raw_y >= static_cast<int> (height)))
            continue;
          const std::size_t nx = periodic ? flood_wrapped (raw_x, width)
                                          : static_cast<std::size_t> (raw_x);
          const std::size_t ny = periodic ? flood_wrapped (raw_y, height)
                                          : static_cast<std::size_t> (raw_y);
          const std::uint32_t next =
            static_cast<std::uint32_t> (index (nx, ny));
          if (!ocean_cell[next] || visited[next])
            continue;
          water[next] = sea_level;
          receiver[next] = cell;
          visited[next] = 1;
          sea_frontier.push (next);
        }
      }
    }
    const bool has_ocean = !global_ocean.empty ();

    // An all-land torus has no geometric boundary. Root its minimax water
    // surface at the deterministic global minimum so the analysis remains
    // defined while making that endorheic choice explicit in `outlets`.
    if (outlets.empty ()) {
      MOPPE_PROFILE_ZONE ("flood.seed_endorheic_minimum");
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

    {
      MOPPE_PROFILE_ZONE ("flood.priority_flood");
      while (!frontier.empty ()) {
        const Cell current = frontier.top ();
        frontier.pop ();
        const std::size_t x = current.index % width;
        const std::size_t y = current.index / width;
        for (const FloodOffset offset : flood_neighbors) {
          const int raw_x = static_cast<int> (x) + offset.x;
          const int raw_y = static_cast<int> (y) + offset.y;
          if (!periodic &&
              (raw_x < 0 || raw_y < 0 || raw_x >= static_cast<int> (width) ||
               raw_y >= static_cast<int> (height)))
            continue;
          const std::size_t nx = periodic ? flood_wrapped (raw_x, width)
                                          : static_cast<std::size_t> (raw_x);
          const std::size_t ny = periodic ? flood_wrapped (raw_y, height)
                                          : static_cast<std::size_t> (raw_y);
          const std::uint32_t next =
            static_cast<std::uint32_t> (index (nx, ny));
          if (visited[next])
            continue;
          visited[next] = 1;
          water[next] = std::max (terrain.at (nx, ny), current.level);
          receiver[next] = current.index;
          frontier.push ({ water[next], next });
        }
      }
    }

    {
      MOPPE_PROFILE_ZONE ("flood.compute_water_depth");
      for (std::size_t y = 0; y < height; ++y)
        for (std::size_t x = 0; x < width; ++x) {
          const std::size_t cell = index (x, y);
          depth[cell] = std::max (0.0f, water[cell] - terrain.at (x, y));
        }
    }

    const FieldSamplingGrid2D domain {
      .width = width,
      .height = height,
      .max_x = grid.spacing_x_m () * static_cast<float> (width),
      .max_y = grid.spacing_y_m () * static_cast<float> (height)
    };
    return { .source_grid = grid,
             .sea_level = sea_level,
             .has_ocean = has_ocean,
             .water_level = ScalarRaster (domain, std::move (water)),
             .water_depth = ScalarRaster (domain, std::move (depth)),
             .ocean = std::move (ocean_cell),
             .spill_receiver = std::move (receiver),
             .outlets = std::move (outlets) };
  }

  LakeCensus census_lakes (const FloodField& flood, float wet_epsilon) {
    MOPPE_PROFILE_ZONE ("census_lakes");
    if (!std::isfinite (wet_epsilon) || wet_epsilon < 0.0f)
      throw std::invalid_argument ("lake census epsilon must be non-negative");
    const std::size_t width = flood.width ();
    const std::size_t height = flood.height ();
    const std::size_t count = width * height;
    const bool periodic = flood.source_grid.topology == Topology::Torus;
    const std::span<const float> depth = flood.water_depth.values ();
    const std::span<const float> level = flood.water_level.values ();
    LakeCensus census { .body =
                          std::vector<WaterBodyId> (count, LakeCensus::dry) };
    std::queue<std::uint32_t> frontier;
    const square_meters_t cell_area = flood.source_grid.cell_area ();
    const meters_t height_scale = flood.source_grid.height_scale;

    for (std::uint32_t origin = 0; origin < count; ++origin) {
      if (depth[origin] <= wet_epsilon ||
          census.body[origin] != LakeCensus::dry)
        continue;
      const WaterBodyId id { static_cast<std::uint32_t> (
        census.bodies.size ()) };
      WaterBody body { .id = id,
                       .cells = cell_count (0),
                       .area = 0.0f * mp_units::si::metre * mp_units::si::metre,
                       .maximum_depth = 0.0f * mp_units::si::metre,
                       .mean_depth = 0.0f * mp_units::si::metre,
                       .volume = 0.0f * mp_units::si::metre *
                                 mp_units::si::metre * mp_units::si::metre,
                       .surface_level = 0.0f * mp_units::si::metre,
                       .ocean_connected = false,
                       .outlet_cell = WaterBody::no_cell,
                       .spill_cell = WaterBody::no_cell,
                       .classification = WaterBodyClass::Puddle };
      double surface_sum_m = 0.0;
      std::vector<std::uint32_t> members;
      census.body[origin] = id;
      frontier.push (origin);
      while (!frontier.empty ()) {
        const std::uint32_t cell = frontier.front ();
        frontier.pop ();
        members.push_back (cell);
        const std::size_t x = cell % width;
        const std::size_t y = cell / width;
        const meters_t depth_m = depth[cell] * height_scale;
        ++body.cells;
        body.maximum_depth = std::max (body.maximum_depth, depth_m);
        body.volume += depth_m * cell_area;
        surface_sum_m +=
          static_cast<double> (level[cell]) * meters_value (height_scale);
        for (const FloodOffset offset : flood_neighbors) {
          const int raw_x = static_cast<int> (x) + offset.x;
          const int raw_y = static_cast<int> (y) + offset.y;
          if (!periodic &&
              (raw_x < 0 || raw_y < 0 || raw_x >= static_cast<int> (width) ||
               raw_y >= static_cast<int> (height)))
            continue;
          const std::size_t nx = periodic ? flood_wrapped (raw_x, width)
                                          : static_cast<std::size_t> (raw_x);
          const std::size_t ny = periodic ? flood_wrapped (raw_y, height)
                                          : static_cast<std::size_t> (raw_y);
          const std::uint32_t next =
            static_cast<std::uint32_t> (ny * width + nx);
          if (depth[next] > wet_epsilon) {
            if (census.body[next] == LakeCensus::dry) {
              census.body[next] = id;
              frontier.push (next);
            }
          }
        }
      }
      body.area = static_cast<float> (body.cells) * cell_area;
      body.mean_depth = body.volume / body.area;
      body.surface_level =
        static_cast<float> (surface_sum_m / static_cast<double> (body.cells)) *
        mp_units::si::metre;
      body.ocean_connected =
        flood.has_ocean &&
        std::fabs (level[members.front ()] - flood.sea_level) <= wet_epsilon;
      if (!body.ocean_connected) {
        // A priority-flood path can leave a connected flat, cross a dry
        // saddle, and re-enter the same flat.  Use its final departure as the
        // body's spill; rebuilding the body as one drainage tree at an earlier
        // departure would point that tree back into itself.
        std::uint32_t cell = members.front ();
        std::size_t steps = 0;
        while (flood.spill_receiver[cell] != cell && steps < count) {
          const std::uint32_t next = flood.spill_receiver[cell];
          if (census.body[cell] == id && census.body[next] != id) {
            body.outlet_cell = cell;
            body.spill_cell = next;
          }
          cell = next;
          ++steps;
        }
        if (steps == count)
          throw std::logic_error (
            "standing-water spill routing contains a cycle");
      }
      if (body.ocean_connected)
        body.classification = WaterBodyClass::Sea;
      else if (body.area < 600.0f * mp_units::si::metre * mp_units::si::metre ||
               body.maximum_depth < 0.25f * mp_units::si::metre ||
               body.volume < 100.0f * mp_units::si::metre *
                               mp_units::si::metre * mp_units::si::metre)
        body.classification = WaterBodyClass::Puddle;
      else if (body.area < 50000.0f * mp_units::si::metre * mp_units::si::metre)
        body.classification = WaterBodyClass::Pond;
      else
        body.classification = WaterBodyClass::Lake;
      census.bodies.push_back (body);
    }
    return census;
  }

  ScalarRaster permanent_water_surface (const FloodField& flood,
                                        const LakeCensus& census,
                                        const WaterPermanence& permanence) {
    const std::size_t count = flood.width () * flood.height ();
    if (census.body.size () != count)
      throw std::invalid_argument ("lake census does not match flood field");
    if (permanence.minimum_area <
          0.0f * mp_units::si::metre * mp_units::si::metre ||
        permanence.minimum_depth < 0.0f * mp_units::si::metre ||
        permanence.minimum_volume < 0.0f * mp_units::si::metre *
                                      mp_units::si::metre * mp_units::si::metre)
      throw std::invalid_argument ("water permanence must be non-negative");
    const std::span<const float> level = flood.water_level.values ();
    const std::span<const float> depth = flood.water_depth.values ();
    std::vector<float> surface (count);
    for (std::size_t cell = 0; cell < count; ++cell) {
      const WaterBodyId id = census.body[cell];
      bool permanent = false;
      if (id != LakeCensus::dry) {
        const WaterBody& body = census.bodies[id];
        permanent = body.classification == WaterBodyClass::Sea ||
                    (body.area >= permanence.minimum_area &&
                     body.maximum_depth >= permanence.minimum_depth &&
                     body.volume >= permanence.minimum_volume);
      }
      surface[cell] = permanent ? level[cell] : level[cell] - depth[cell];
    }
    return ScalarRaster (flood.water_level.domain (), std::move (surface));
  }
}
