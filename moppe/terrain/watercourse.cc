#include <moppe/terrain/watercourse.hh>

#include <moppe/profile.hh>

#include <moppe/terrain/carve.hh>

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <limits>
#include <stdexcept>
#include <vector>

namespace moppe::terrain {
  namespace {
    int minimum_image_delta (int delta, int period) {
      if (delta > period / 2)
        delta -= period;
      else if (delta < -period / 2)
        delta += period;
      return delta;
    }

    float rapid_signal (float slope) {
      return std::clamp ((slope - 0.035f) / 0.24f, 0.0f, 1.0f);
    }

    struct PaintPoint {
      std::uint32_t cell;
      meters_t level;
      meters_t width;
      meters_per_second_t speed;
      float direction_x;
      float direction_z;
      bool water;
    };

    float wave_amplitude (const FloodField& flood,
                          const LakeCensus& census,
                          std::size_t cell) {
      if (flood.ocean[cell])
        return 1.0f;
      const std::uint32_t body = census.body[cell];
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

  WaterSheets paint_watercourses (const TerrainView& terrain,
                                  const FloodField& flood,
                                  const LakeCensus& census,
                                  const DrainageGraph& drainage,
                                  const RiverNetwork& rivers,
                                  const WatercoursePaint& parameters) {
    MOPPE_PROFILE_ZONE ("paint_watercourses");
    const TerrainGrid& grid = flood.source_grid;
    const int width = static_cast<int> (grid.unique_width ());
    const int height = static_cast<int> (grid.unique_height ());
    const std::size_t count = grid.unique_size ();
    const bool periodic = grid.topology == Topology::Torus;
    const float height_scale = grid.height_scale_m ();
    if (census.body.size () != count || drainage.receiver.size () != count)
      throw std::invalid_argument (
        "watercourse painting inputs do not share a grid");

    const auto ground_m = [&] (std::uint32_t cell) {
      return terrain.at (cell % width, cell / width) * height_scale;
    };
    const auto water_cell = [&] (std::uint32_t cell) {
      return census.body[cell] != LakeCensus::dry || flood.ocean[cell];
    };

    // The lakes and the sea come first: the permanent standing surface,
    // with ground height in every dry cell so the sheet is continuous.
    const ScalarRaster permanent =
      permanent_water_surface (flood, census, parameters.permanence);
    std::vector<float> surface (permanent.values ().begin (),
                                permanent.values ().end ());
    std::vector<float> amplitude (count);
    for (std::size_t cell = 0; cell < count; ++cell)
      amplitude[cell] = wave_amplitude (flood, census, cell);

    // Flow stamps accumulate weighted arrows; overlapping stamps at a
    // confluence blend into the averaged current when normalized.
    std::vector<float> flow_x (count, 0.0f);
    std::vector<float> flow_z (count, 0.0f);
    std::vector<float> flow_weight (count, 0.0f);

    // River levels take the nearest centerline point, not the highest
    // overlapping stamp: on a cascade an upstream stamp reaches cells
    // whose own channel level is meters lower, and keeping the nearer
    // point's level is what keeps the sheet on the falling bed.
    constexpr float far_away = std::numeric_limits<float>::infinity ();
    std::vector<float> river_level (count, -far_away);
    std::vector<float> river_distance (count, far_away);

    std::vector<PaintPoint> points;
    {
      MOPPE_PROFILE_ZONE ("watercourse.paint_river_reaches");
      for (const RiverReach& reach : rivers.reaches) {
        if (reach.cells.empty ())
          continue;

        // The painted path is the reach plus its receiver, plus one step
        // into standing water so the mouth current carries into the body.
        std::vector<CellIndex> cells = reach.cells;
        const std::uint32_t receiver = drainage.receiver[cells.back ()];
        if (receiver != cells.back ())
          cells.push_back (receiver);
        if (water_cell (cells.back ())) {
          const std::uint32_t next = drainage.receiver[cells.back ()];
          if (next != cells.back () && water_cell (next)) {
            int dx = static_cast<int> (next % width) -
                     static_cast<int> (cells.back () % width);
            int dz = static_cast<int> (next / width) -
                     static_cast<int> (cells.back () / width);
            if (periodic) {
              dx = minimum_image_delta (dx, width);
              dz = minimum_image_delta (dz, height);
            }
            if (std::abs (dx) <= 1 && std::abs (dz) <= 1)
              cells.push_back (next);
          }
        }

        points.clear ();
        for (std::size_t i = 0; i < cells.size (); ++i) {
          const CellIndex cell = cells[i];
          const float area = drainage.contributing_area.values ()[cell];
          const CellIndex slope_cell =
            i + 1 == cells.size () && i > 0 ? cells[i - 1] : cell;
          const float rapid =
            rapid_signal (drainage.slope.values ()[slope_cell]);
          const float fall =
            rivers.waterfall_by_cell[cell] != Waterfall::no_id ? 1.0f : 0.0f;
          points.push_back (
            { .cell = cell,
              .level = 0.0f * mp_units::si::metre,
              .width = channel_width_m (area) * mp_units::si::metre,
              .speed = parameters.base_speed + parameters.rapid_speed * rapid +
                       parameters.waterfall_speed * fall,
              .direction_x = 0.0f,
              .direction_z = 0.0f,
              .water = water_cell (cell) });
        }

        // Downstream direction from central differences over the path,
        // in world meters, unwrapped across the periodic seam.
        for (std::size_t i = 0; i < points.size (); ++i) {
          const std::uint32_t before = points[i == 0 ? 0 : i - 1].cell;
          const std::uint32_t after =
            points[i + 1 == points.size () ? i : i + 1].cell;
          int dx = static_cast<int> (after % width) -
                   static_cast<int> (before % width);
          int dz = static_cast<int> (after / width) -
                   static_cast<int> (before / width);
          if (periodic) {
            dx = minimum_image_delta (dx, width);
            dz = minimum_image_delta (dz, height);
          }
          const float run_x = dx * grid.spacing_x_m ();
          const float run_z = dz * grid.spacing_y_m ();
          const float run = std::hypot (run_x, run_z);
          if (run > 1e-6f) {
            points[i].direction_x = run_x / run;
            points[i].direction_z = run_z / run;
          }
        }

        // Surface levels, needing the directions: the fill is bounded by
        // the banks actually standing beside the channel. Where the
        // carve's backwater floor kept a mouth reach shallow, the width
        // law's full depth would stand meters over the flats around it
        // and flood them; the channel that is really there is the
        // channel the water fills. A river surface never rises
        // downstream, and a mouth cell takes its body's own level.
        const auto bank_m = [&] (const PaintPoint& point, float side) {
          // The bank is the crest along the ray, not one sample: a probe
          // landing in a neighboring channel's excavation near a
          // confluence must not convince us the river has no banks.
          const float probe =
            meters_value (0.5f * point.width + parameters.bank_probe);
          float crest = -std::numeric_limits<float>::infinity ();
          for (const float extent : { 0.7f, 1.0f, 1.3f }) {
            int x =
              static_cast<int> (point.cell) % width +
              static_cast<int> (std::lround (side * point.direction_z * extent *
                                             probe / grid.spacing_x_m ()));
            int y = static_cast<int> (point.cell) / width +
                    static_cast<int> (
                      std::lround (-side * point.direction_x * extent * probe /
                                   grid.spacing_y_m ()));
            if (periodic) {
              x = (x % width + width) % width;
              y = (y % height + height) % height;
            } else {
              x = std::clamp (x, 0, width - 1);
              y = std::clamp (y, 0, height - 1);
            }
            crest = std::max (
              crest, ground_m (static_cast<std::uint32_t> (y * width + x)));
          }
          return crest;
        };
        float level = std::numeric_limits<float>::infinity ();
        for (PaintPoint& point : points) {
          float target;
          if (point.water)
            target = flood.water_level.values ()[point.cell] * height_scale;
          else {
            const float ground = ground_m (point.cell);
            const float carved =
              std::min (bank_m (point, 1.0f), bank_m (point, -1.0f)) - ground;
            const float law = channel_depth_m (
              drainage.contributing_area.values ()[point.cell]);
            target = ground +
                     parameters.channel_fill *
                       std::clamp (
                         carved, meters_value (parameters.minimum_depth), law);
          }
          level = std::min (level, target);
          point.level = level * mp_units::si::metre;
        }

        for (const PaintPoint& point : points) {
          const int cx = static_cast<int> (point.cell) % width;
          const int cy = static_cast<int> (point.cell) / width;
          const float half_width = meters_value (0.5f * point.width);
          const float radius =
            half_width + meters_value (parameters.bank_margin);
          // Metric widths on degenerate sub-metre grids must not stamp
          // arbitrarily large neighborhoods.
          constexpr int stamp_limit_cells = 16;
          const int reach_x = std::min (
            stamp_limit_cells,
            static_cast<int> (std::ceil (radius / grid.spacing_x_m ())));
          const int reach_y = std::min (
            stamp_limit_cells,
            static_cast<int> (std::ceil (radius / grid.spacing_y_m ())));
          for (int dy = -reach_y; dy <= reach_y; ++dy)
            for (int dx = -reach_x; dx <= reach_x; ++dx) {
              int x = cx + dx;
              int y = cy + dy;
              if (periodic) {
                x = (x % width + width) % width;
                y = (y % height + height) % height;
              } else if (x < 0 || x >= width || y < 0 || y >= height)
                continue;
              const float distance =
                std::hypot (dx * grid.spacing_x_m (), dy * grid.spacing_y_m ());
              if (distance >= radius)
                continue;
              const std::size_t cell = static_cast<std::size_t> (y) * width + x;

              // The level stamp is flat across the channel; the banks
              // clip it, and the depth limit keeps a cascade's sheet on
              // the falling bed instead of bridging the drop.
              if (!point.water && distance < river_distance[cell]) {
                river_distance[cell] = distance;
                river_level[cell] =
                  std::min (meters_value (point.level),
                            ground_m (static_cast<std::uint32_t> (cell)) +
                              meters_value (parameters.depth_limit));
              }

              const float weight =
                (1.0f - distance / radius) * (point.water ? 0.5f : 1.0f);
              flow_x[cell] += weight * point.direction_x *
                              meters_per_second_value (point.speed);
              flow_z[cell] += weight * point.direction_z *
                              meters_per_second_value (point.speed);
              flow_weight[cell] += weight;
            }
        }
      }
    }

    std::vector<float> flow (2 * count, 0.0f);
    {
      MOPPE_PROFILE_ZONE ("watercourse.resolve_sheets");
      for (std::size_t cell = 0; cell < count; ++cell)
        if (river_distance[cell] < far_away)
          surface[cell] =
            std::max (surface[cell], river_level[cell] / height_scale);

      for (std::size_t cell = 0; cell < count; ++cell)
        if (flow_weight[cell] > 0.0f) {
          flow[2 * cell] = flow_x[cell] / flow_weight[cell];
          flow[2 * cell + 1] = flow_z[cell] / flow_weight[cell];
        }
    }

    // Sign the shoreline: a dry cell beside water stores its wettest
    // neighbor's level (held a hair below its own ground so it stays
    // dry) instead of bare ground.  The bilinear water-minus-ground
    // difference then crosses zero at the true sub-cell waterline
    // inside every mixed cell, so the fragment discard, the swash and
    // foam bands, the conforming mesh, and the CPU extraction all see
    // the same smooth curve -- a sheet clamped to ground has no
    // gradient on the dry side and quantizes every consumer onto the
    // lattice staircase.
    {
      const float dry_margin = 1e-6f;
      const auto ground_norm = [&] (std::size_t cell) {
        return terrain.at (cell % width, cell / width);
      };
      std::vector<float> signed_surface = surface;
      for (int y = 0; y < height; ++y)
        for (int x = 0; x < width; ++x) {
          const std::size_t cell = static_cast<std::size_t> (y) * width + x;
          const float ground = ground_norm (cell);
          if (surface[cell] - ground > dry_margin)
            continue; // wet: keep the painted level
          float wettest = -std::numeric_limits<float>::infinity ();
          for (int dy = -1; dy <= 1; ++dy)
            for (int dx = -1; dx <= 1; ++dx) {
              if (dx == 0 && dy == 0)
                continue;
              int nx = x + dx;
              int ny = y + dy;
              if (periodic) {
                nx = (nx % width + width) % width;
                ny = (ny % height + height) % height;
              } else if (nx < 0 || ny < 0 || nx >= width || ny >= height)
                continue;
              const std::size_t neighbor =
                static_cast<std::size_t> (ny) * width + nx;
              if (surface[neighbor] - ground_norm (neighbor) > dry_margin)
                wettest = std::max (wettest, surface[neighbor]);
            }
          if (wettest > -std::numeric_limits<float>::infinity ())
            signed_surface[cell] = std::min (wettest, ground - dry_margin);
        }
      surface = std::move (signed_surface);
    }

    return { .surface = ScalarRaster (permanent.domain (), std::move (surface)),
             .amplitude = std::move (amplitude),
             .flow = std::move (flow) };
  }
}
