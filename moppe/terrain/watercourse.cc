#include <moppe/terrain/watercourse.hh>

#include <moppe/profile.hh>
#include <moppe/terrain/river.hh>

#include <algorithm>
#include <cmath>
#include <limits>
#include <stdexcept>
#include <vector>

namespace moppe::terrain {
  namespace {
    float rapid_signal (float slope) {
      return std::clamp ((slope - 0.035f) / 0.24f, 0.0f, 1.0f);
    }

    float wave_factor (const FloodField& flood,
                       const LakeCensus& census,
                       std::size_t cell) {
      if (flood.ocean[cell])
        return 1.0f;
      const WaterBodyId body = census.body[cell];
      if (body == LakeCensus::dry || body >= census.bodies.size ())
        return 0.0f;
      switch (census.bodies[body].classification) {
      case WaterBodyClass::Sea:
        return 1.0f;
      case WaterBodyClass::Lake:
        return 0.10f;
      case WaterBodyClass::Pond:
        return 0.04f;
      case WaterBodyClass::Puddle:
        return 0.0f;
      }
      return 0.0f;
    }
  }

  WaterSheets
  detail::paint_watercourses (const TerrainDomain& terrain_domain,
                              std::span<const SurfaceElevation> elevations,
                              const FloodField& flood,
                              const LakeCensus& census,
                              const DrainageGraph& drainage,
                              const RiverNetwork& rivers,
                              const WatercoursePaint& parameters) {
    MOPPE_PROFILE_ZONE ("paint_watercourses");
    const TerrainDomain& grid = flood.domain ();
    if (terrain_domain != grid)
      throw std::invalid_argument (
        "watercourse terrain does not match hydrology");
    const int width = static_cast<int> (grid.width ());
    const int height = static_cast<int> (grid.height ());
    const std::size_t count = grid.width () * grid.height ();
    if (census.body.size () != count || drainage.receiver.size () != count)
      throw std::invalid_argument (
        "watercourse painting inputs do not share a grid");

    // Orogeny already creates the valleys and the explicit river alignments
    // render the running surface. The raster sheet is therefore reserved for
    // actual standing bodies. This removes the old bank-probe reconstruction
    // and its lattice-shaped dry-reach shorelines.
    const ElevationMap permanent =
      permanent_water_surface (flood, census, parameters.permanence);
    const auto& permanent_elevations =
      spatial::get<surface_elevation> (permanent);
    std::vector<float> surface;
    surface.reserve (count);
    for (SurfaceElevation elevation : permanent_elevations)
      surface.push_back (surface_elevation_value (elevation));
    std::vector<float> amplitude (count);
    for (std::size_t cell = 0; cell < count; ++cell)
      amplitude[cell] = wave_factor (flood, census, cell);

    // A channel-like body a river traverses renders as ribbon pools with
    // real banks; pull it out of the sheet so the two representations never
    // overlap. Untraversed channel bodies are stagnant water with no river
    // to own them and keep their plate.
    if (!rivers.body_traversed.empty ()) {
      for (std::size_t cell = 0; cell < count; ++cell) {
        const WaterBodyId id = census.body[cell];
        if (id == LakeCensus::dry || id >= rivers.body_traversed.size ())
          continue;
        if (census.bodies[id].channel_like && rivers.body_traversed[id]) {
          surface[cell] =
            flood.water_level_m (cell) - flood.water_depth_m (cell);
          amplitude[cell] = 0.0f;
        }
      }
    }

    // Continue each vector river's current into the standing body at its
    // mouth. Dense alignment samples make this field smooth even though its
    // storage remains the shared terrain lattice.
    std::vector<float> flow_x (count, 0.0f);
    std::vector<float> flow_z (count, 0.0f);
    std::vector<float> flow_weight (count, 0.0f);
    {
      MOPPE_PROFILE_ZONE ("watercourse.paint_mouth_currents");
      for (const RiverReach& reach : rivers.reaches) {
        const auto& points = reach.alignment.points;
        for (std::size_t point = 0; point < points.size (); ++point) {
          const RiverAlignmentPoint& center = points[point];
          if (center.standing_water <= 0.01f)
            continue;
          const RiverAlignmentPoint& before = points[point ? point - 1 : point];
          const RiverAlignmentPoint& after =
            points[point + 1 < points.size () ? point + 1 : point];
          const float run_x = after.x_m - before.x_m;
          const float run_z = after.z_m - before.z_m;
          const float run = std::hypot (run_x, run_z);
          if (run <= 1e-5f)
            continue;
          const float direction_x = run_x / run;
          const float direction_z = run_z / run;
          const float speed = meters_per_second_value (
            parameters.base_speed +
            parameters.rapid_speed * rapid_signal (center.slope) +
            parameters.waterfall_speed * center.waterfall);
          const float radius =
            0.5f * meters_value (river_width (center.contributing_area_m2 *
                                              mp_units::si::metre *
                                              mp_units::si::metre)) +
            meters_value (parameters.bank_margin);
          const int cx = static_cast<int> (
            std::lround (center.x_m / meters_value (grid.spacing_x ())));
          const int cy = static_cast<int> (
            std::lround (center.z_m / meters_value (grid.spacing_z ())));
          constexpr int stamp_limit_cells = 16;
          const int reach_x =
            std::min (stamp_limit_cells,
                      static_cast<int> (
                        std::ceil (radius / meters_value (grid.spacing_x ()))));
          const int reach_y =
            std::min (stamp_limit_cells,
                      static_cast<int> (
                        std::ceil (radius / meters_value (grid.spacing_z ()))));
          for (int dy = -reach_y; dy <= reach_y; ++dy)
            for (int dx = -reach_x; dx <= reach_x; ++dx) {
              const int x = wrap_index (cx + dx, width);
              const int y = wrap_index (cy + dy, height);
              const float delta_x = std::remainder (
                x * meters_value (grid.spacing_x ()) - center.x_m,
                width * meters_value (grid.spacing_x ()));
              const float delta_z = std::remainder (
                y * meters_value (grid.spacing_z ()) - center.z_m,
                height * meters_value (grid.spacing_z ()));
              const float distance = std::hypot (delta_x, delta_z);
              if (distance >= radius)
                continue;
              const std::size_t cell = static_cast<std::size_t> (y) * width + x;
              if (surface[cell] - elevation_at (grid, elevations, x, y) <=
                  1e-6f)
                continue;
              const float weight =
                (1.0f - distance / radius) * center.standing_water;
              flow_x[cell] += weight * direction_x * speed;
              flow_z[cell] += weight * direction_z * speed;
              flow_weight[cell] += weight;
            }
        }
      }
    }

    std::vector<float> flow (2 * count, 0.0f);
    for (std::size_t cell = 0; cell < count; ++cell)
      if (flow_weight[cell] > 0.0f) {
        flow[2 * cell] = flow_x[cell] / flow_weight[cell];
        flow[2 * cell + 1] = flow_z[cell] / flow_weight[cell];
      }

    // Sign the still-water shoreline. A dry cell beside water stores a
    // neighboring level just below its ground so bilinear consumers find a
    // true sub-cell zero crossing instead of a cell-edge staircase. It
    // stores the LOWEST wet neighbor: on an ordinary shore every neighbor
    // shares one body level, while on a sill between terraced bodies the
    // higher plate then ends at its own waterline instead of extending
    // across the sill and hanging in the air over the lower body.
    {
      constexpr float dry_margin = 1e-6f;
      std::vector<float> signed_surface = surface;
      for (int y = 0; y < height; ++y)
        for (int x = 0; x < width; ++x) {
          const std::size_t cell = static_cast<std::size_t> (y) * width + x;
          const float ground = elevation_at (grid, elevations, x, y);
          if (surface[cell] - ground > dry_margin)
            continue;
          float lowest_wet = std::numeric_limits<float>::infinity ();
          for (int dy = -1; dy <= 1; ++dy)
            for (int dx = -1; dx <= 1; ++dx) {
              if (dx == 0 && dy == 0)
                continue;
              const int nx = wrap_index (x + dx, width);
              const int ny = wrap_index (y + dy, height);
              const std::size_t neighbor =
                static_cast<std::size_t> (ny) * width + nx;
              if (surface[neighbor] - elevation_at (grid, elevations, nx, ny) >
                  dry_margin)
                lowest_wet = std::min (lowest_wet, surface[neighbor]);
            }
          if (std::isfinite (lowest_wet))
            signed_surface[cell] = std::min (lowest_wet, ground - dry_margin);
        }
      surface = std::move (signed_surface);
    }

    WaterSheets sheets (grid);
    auto& sheet_elevations = spatial::get<surface_elevation> (sheets);
    auto& amplitudes = spatial::get<wave_amplitude> (sheets);
    auto& velocities = spatial::get<water_velocity> (sheets);
    for (std::size_t cell = 0; cell < count; ++cell) {
      sheet_elevations[cell] =
        SurfaceElevation (surface[cell] * surface_elevation[u::m]);
      amplitudes[cell] = amplitude[cell] * wave_amplitude[one];
      velocities[cell] = Vec3 (flow[2 * cell], 0.0f, flow[2 * cell + 1]) *
                         water_velocity[u::m / u::s];
    }
    return sheets;
  }
}
