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

  FloodField
  detail::analyze_standing_water (const TerrainDomain& grid,
                                  std::span<const SurfaceElevation> elevations,
                                  float sea_level) {
    MOPPE_PROFILE_ZONE ("analyze_standing_water");
    if (!std::isfinite (sea_level))
      throw std::invalid_argument ("standing-water sea level must be finite");

    const std::size_t width = grid.width ();
    const std::size_t height = grid.height ();
    const std::size_t count = width * height;
    const auto index = [width] (std::size_t x, std::size_t y) {
      return y * width + x;
    };

    std::vector<float> water (count, std::numeric_limits<float>::infinity ());
    std::vector<float> depth (count, 0.0f);
    std::vector<CellIndex> receiver (count, CellIndex { 0 });
    std::vector<std::uint8_t> visited (count, 0);
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
            elevation_at (grid, elevations, origin % width, origin / width) >
              sea_level)
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
            const std::size_t nx = flood_wrapped (raw_x, width);
            const std::size_t ny = flood_wrapped (raw_y, height);
            const std::uint32_t next =
              static_cast<std::uint32_t> (index (nx, ny));
            if (submerged_seen[next] ||
                elevation_at (grid, elevations, nx, ny) > sea_level)
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
          const std::size_t nx = flood_wrapped (raw_x, width);
          const std::size_t ny = flood_wrapped (raw_y, height);
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
    // defined.
    if (!has_ocean) {
      MOPPE_PROFILE_ZONE ("flood.seed_endorheic_minimum");
      std::uint32_t minimum_cell = 0;
      float minimum = elevation_at (grid, elevations, 0, 0);
      for (std::uint32_t cell = 1; cell < count; ++cell) {
        const float value =
          elevation_at (grid, elevations, cell % width, cell / width);
        if (value < minimum) {
          minimum = value;
          minimum_cell = cell;
        }
      }
      water[minimum_cell] = minimum;
      receiver[minimum_cell] = minimum_cell;
      visited[minimum_cell] = 1;
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
          const std::size_t nx = flood_wrapped (raw_x, width);
          const std::size_t ny = flood_wrapped (raw_y, height);
          const std::uint32_t next =
            static_cast<std::uint32_t> (index (nx, ny));
          if (visited[next])
            continue;
          visited[next] = 1;
          water[next] =
            std::max (elevation_at (grid, elevations, nx, ny), current.level);
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
          depth[cell] = std::max (
            0.0f, water[cell] - elevation_at (grid, elevations, x, y));
        }
    }

    FloodSurface surface (grid);
    auto& levels = spatial::get<surface_elevation> (surface);
    auto& depths = spatial::get<standing_water_depth> (surface);
    for (std::size_t cell = 0; cell < count; ++cell) {
      levels[cell] = SurfaceElevation (water[cell] * surface_elevation[u::m]);
      depths[cell] = depth[cell] * standing_water_depth[u::m];
    }
    return { .surface = std::move (surface),
             .sea_level = sea_level,
             .has_ocean = has_ocean,
             .ocean = std::move (ocean_cell),
             .spill_receiver = std::move (receiver) };
  }

  WaterBodyMembership::WaterBodyMembership (
    std::vector<WaterBodyId> body_at_cell, WaterBodyDomain bodies)
      : m_bodies (bodies), m_body_at_cell (std::move (body_at_cell)) {
    for (const WaterBodyId body : m_body_at_cell)
      if (body != dry && !m_bodies.contains (body))
        throw std::invalid_argument (
          "water-body membership targets an unknown body");
  }

  LakeCensus::LakeCensus (std::vector<WaterBodyId> body_at_cell,
                          std::vector<WaterBody> water_bodies)
      : m_domain (water_bodies.size ()),
        m_membership (std::move (body_at_cell), m_domain),
        m_water_bodies (std::move (water_bodies)) {
    for (std::size_t offset = 0; offset < m_water_bodies.size (); ++offset)
      if (m_water_bodies[offset].id != m_domain.index (offset))
        throw std::invalid_argument (
          "water-body row identity does not match its domain");
  }

  LakeCensus census_lakes (const FloodField& flood, float wet_epsilon) {
    MOPPE_PROFILE_ZONE ("census_lakes");
    if (!std::isfinite (wet_epsilon) || wet_epsilon < 0.0f)
      throw std::invalid_argument ("lake census epsilon must be non-negative");
    const std::size_t width = flood.width ();
    const std::size_t height = flood.height ();
    const std::size_t count = width * height;
    const std::span<const StandingWaterDepth> depth = flood.water_depths ();
    const std::span<const SurfaceElevation> level = flood.water_levels ();
    std::vector<WaterBodyId> body_at_cell (count, LakeCensus::dry);
    std::vector<WaterBody> bodies;
    std::queue<std::uint32_t> frontier;
    const square_meters_t cell_area = flood.domain ().cell_area ();

    for (std::uint32_t origin = 0; origin < count; ++origin) {
      if (depth[origin].numerical_value_in (u::m) <= wet_epsilon ||
          body_at_cell[origin] != LakeCensus::dry)
        continue;
      const WaterBodyId id { static_cast<std::uint32_t> (bodies.size ()) };
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
                       .classification = WaterBodyClass::Puddle,
                       .inradius = 0.0f * mp_units::si::metre,
                       .channel_like = false };
      double surface_sum_m = 0.0;
      std::vector<std::uint32_t> members;
      body_at_cell[origin] = id;
      frontier.push (origin);
      while (!frontier.empty ()) {
        const std::uint32_t cell = frontier.front ();
        frontier.pop ();
        members.push_back (cell);
        const std::size_t x = cell % width;
        const std::size_t y = cell / width;
        const meters_t depth_m =
          depth[cell].numerical_value_in (u::m) * mp_units::si::metre;
        ++body.cells;
        body.maximum_depth = std::max (body.maximum_depth, depth_m);
        body.volume += depth_m * cell_area;
        surface_sum_m +=
          static_cast<double> (surface_elevation_value (level[cell]));
        for (const FloodOffset offset : flood_neighbors) {
          const int raw_x = static_cast<int> (x) + offset.x;
          const int raw_y = static_cast<int> (y) + offset.y;
          const std::size_t nx = flood_wrapped (raw_x, width);
          const std::size_t ny = flood_wrapped (raw_y, height);
          const std::uint32_t next =
            static_cast<std::uint32_t> (ny * width + nx);
          if (depth[next].numerical_value_in (u::m) > wet_epsilon) {
            if (body_at_cell[next] == LakeCensus::dry) {
              body_at_cell[next] = id;
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
        std::fabs (surface_elevation_value (level[members.front ()]) -
                   flood.sea_level) <= wet_epsilon;
      if (!body.ocean_connected) {
        // A priority-flood path can leave a connected flat, cross a dry
        // saddle, and re-enter the same flat.  Use its final departure as the
        // body's spill; rebuilding the body as one drainage tree at an earlier
        // departure would point that tree back into itself.
        std::uint32_t cell = members.front ();
        std::size_t steps = 0;
        while (flood.spill_receiver[cell] != cell && steps < count) {
          const std::uint32_t next = flood.spill_receiver[cell];
          if (body_at_cell[cell] == id && body_at_cell[next] != id) {
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
      else if (!water_body_is_permanent (body))
        body.classification = WaterBodyClass::Puddle;
      else if (body.area < 50000.0f * mp_units::si::metre * mp_units::si::metre)
        body.classification = WaterBodyClass::Pond;
      else
        body.classification = WaterBodyClass::Lake;
      bodies.push_back (body);
    }

    // Shape reading: a multi-source sweep from every dry cell measures each
    // wet cell's distance to shore in cell steps, and a body's largest such
    // distance is its flooded inradius. A permanent inland body no wider
    // than a few cells is a flooded channel segment, not a lake: the river
    // ribbon can own it with proper banks, where the flat sheet would
    // terrace it into plates.
    {
      MOPPE_PROFILE_ZONE ("census.measure_body_shape");
      // Two cells of inradius keeps ribbon ownership to pools four to five
      // cells wide. Wider flooded reaches can hide arms the alignment never
      // visits, which would drain visibly if the sheet yielded them.
      constexpr float channel_inradius_cells = 2.05f;
      const float cell_step_m = std::min (
        (flood.domain ().spacing_x ()).numerical_value_in (moppe::u::m),
        (flood.domain ().spacing_z ()).numerical_value_in (moppe::u::m));
      std::vector<std::int32_t> shore_distance (count, -1);
      std::queue<std::uint32_t> sweep;
      for (std::uint32_t cell = 0; cell < count; ++cell)
        if (body_at_cell[cell] == LakeCensus::dry) {
          shore_distance[cell] = 0;
          sweep.push (cell);
        }
      while (!sweep.empty ()) {
        const std::uint32_t cell = sweep.front ();
        sweep.pop ();
        const std::size_t x = cell % width;
        const std::size_t y = cell / width;
        for (const FloodOffset offset : flood_neighbors) {
          const int raw_x = static_cast<int> (x) + offset.x;
          const int raw_y = static_cast<int> (y) + offset.y;
          const std::size_t nx = flood_wrapped (raw_x, width);
          const std::size_t ny = flood_wrapped (raw_y, height);
          const std::uint32_t next =
            static_cast<std::uint32_t> (ny * width + nx);
          if (shore_distance[next] < 0) {
            shore_distance[next] = shore_distance[cell] + 1;
            sweep.push (next);
          }
        }
      }
      for (std::uint32_t cell = 0; cell < count; ++cell) {
        const WaterBodyId id = body_at_cell[cell];
        if (id == LakeCensus::dry || shore_distance[cell] < 0)
          continue;
        WaterBody& body = bodies[id.value];
        body.inradius = std::max (body.inradius,
                                  static_cast<float> (shore_distance[cell]) *
                                    cell_step_m * mp_units::si::metre);
      }
      for (WaterBody& body : bodies)
        body.channel_like =
          !body.ocean_connected && water_body_is_permanent (body) &&
          body.inradius <=
            channel_inradius_cells * cell_step_m * mp_units::si::metre;
    }
    return LakeCensus (std::move (body_at_cell), std::move (bodies));
  }

  bool water_body_is_permanent (const WaterBody& body,
                                const WaterPermanence& permanence) {
    return body.classification == WaterBodyClass::Sea ||
           (body.area >= permanence.minimum_area &&
            body.maximum_depth >= permanence.minimum_depth &&
            body.volume >= permanence.minimum_volume);
  }

  bool water_body_terminates_rivers (const WaterBody& body,
                                     const WaterPermanence& permanence) {
    return body.classification == WaterBodyClass::Sea ||
           (water_body_is_permanent (body, permanence) && !body.channel_like);
  }

  ElevationMap permanent_water_surface (const FloodField& flood,
                                        const LakeCensus& census,
                                        const WaterPermanence& permanence) {
    const std::size_t count = flood.width () * flood.height ();
    if (census.cell_count () != count)
      throw std::invalid_argument ("lake census does not match flood field");
    if (permanence.minimum_area <
          0.0f * mp_units::si::metre * mp_units::si::metre ||
        permanence.minimum_depth < 0.0f * mp_units::si::metre ||
        permanence.minimum_volume < 0.0f * mp_units::si::metre *
                                      mp_units::si::metre * mp_units::si::metre)
      throw std::invalid_argument ("water permanence must be non-negative");
    const std::span<const SurfaceElevation> level = flood.water_levels ();
    const std::span<const StandingWaterDepth> depth = flood.water_depths ();
    ElevationMap surface (flood.domain ());
    auto& elevations = spatial::get<surface_elevation> (surface);
    for (std::size_t cell = 0; cell < count; ++cell) {
      const WaterBodyId id =
        census.body_at (CellIndex { static_cast<std::uint32_t> (cell) });
      const bool permanent =
        id != LakeCensus::dry &&
        water_body_is_permanent (census.water_body (id), permanence);
      elevations[cell] =
        permanent ? level[cell]
                  : SurfaceElevation ((surface_elevation_value (level[cell]) -
                                       depth[cell].numerical_value_in (u::m)) *
                                      surface_elevation[u::m]);
    }
    return surface;
  }
}
