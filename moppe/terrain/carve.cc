#include <moppe/terrain/carve.hh>

#include <moppe/terrain/drainage.hh>
#include <moppe/terrain/flood.hh>

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <limits>
#include <stdexcept>
#include <vector>

namespace moppe::terrain {
  namespace {
    void validate (const ChannelCarving& p) {
      if (!std::isfinite (p.sea_level) ||
          !std::isfinite (p.minimum_area_cells) ||
          p.minimum_area_cells <= 0.0f ||
          !std::isfinite (p.depth_per_sqrt_m2) || p.depth_per_sqrt_m2 < 0.0f ||
          !std::isfinite (meters_value (p.minimum_depth)) ||
          p.minimum_depth < 0.0f * mp_units::si::metre ||
          !std::isfinite (meters_value (p.maximum_depth)) ||
          p.maximum_depth < p.minimum_depth ||
          !std::isfinite (meters_value (p.bank_blend)) ||
          p.bank_blend < 0.0f * mp_units::si::metre)
        throw std::invalid_argument ("invalid channel carving parameters");
    }

    float bank_ramp (float distance, float half_width, float blend) {
      if (distance <= half_width)
        return 0.0f;
      if (blend <= 0.0f)
        return 1.0f;
      const float t = std::min ((distance - half_width) / blend, 1.0f);
      return t * t * (3.0f - 2.0f * t);
    }

    struct CarvePoint {
      std::uint32_t cell;
      float bed_m;
      float width_m;
    };
  }

  float channel_width_m (float area) noexcept {
    // Wider than strict hydraulic geometry: rivers must read as rivers
    // from a motorcycle, and their valleys are AGE-scaled.
    return std::clamp (0.012f * std::sqrt (area), 1.5f, 24.0f);
  }

  float channel_depth_m (float area,
                         const ChannelCarving& parameters) noexcept {
    // Every visible reach earns its full depth: a stream you can see is
    // a channel you can ride into, never a painted-on film.
    return std::clamp (parameters.depth_per_sqrt_m2 * std::sqrt (area),
                       meters_value (parameters.minimum_depth),
                       meters_value (parameters.maximum_depth));
  }

  ChannelCarvingResult carve_channels (const TerrainView& terrain,
                                       const ChannelCarving& parameters) {
    validate (parameters);
    const TerrainGrid& grid = terrain.grid ();
    const int width = static_cast<int> (grid.unique_width ());
    const int height = static_cast<int> (grid.unique_height ());
    const std::size_t count = grid.unique_size ();
    const bool periodic = grid.topology == Topology::Torus;
    const float height_scale = grid.height_scale_m ();

    std::vector<float> original (count);
    for (int y = 0; y < height; ++y)
      for (int x = 0; x < width; ++x)
        original[static_cast<std::size_t> (y) * width + x] = terrain.at (x, y);
    std::vector<float> carved = original;

    const FloodField flood =
      analyze_standing_water (terrain, parameters.sea_level);
    const LakeCensus census = census_lakes (flood);
    const DrainageGraph drainage =
      analyze_wet_drainage (terrain, flood, census);
    const square_meters_t minimum_area =
      parameters.minimum_area_cells * grid.cell_area ();
    const RiverNetwork rivers =
      extract_river_network (flood, census, drainage, minimum_area);

    const auto ground_m = [&] (std::uint32_t cell) {
      return original[cell] * height_scale;
    };
    const auto depth_m = [&] (std::uint32_t cell) {
      return channel_depth_m (drainage.contributing_area.values ()[cell],
                              parameters);
    };
    const auto water_cell = [&] (std::uint32_t cell) {
      return census.body[cell] != LakeCensus::dry || flood.ocean[cell];
    };

    // Reaches in topological order, so a downstream reach begins at or
    // below every bed feeding it and beds stay monotone through junctions.
    std::vector<int> pending (rivers.reaches.size (), 0);
    for (const RiverReach& reach : rivers.reaches)
      if (reach.downstream_reach != RiverReach::no_id)
        ++pending[reach.downstream_reach];
    std::vector<std::uint32_t> order;
    order.reserve (rivers.reaches.size ());
    for (const RiverReach& reach : rivers.reaches)
      if (pending[reach.id] == 0)
        order.push_back (reach.id);
    for (std::size_t next = 0; next < order.size (); ++next) {
      const RiverReach& reach = rivers.reaches[order[next]];
      if (reach.downstream_reach != RiverReach::no_id &&
          --pending[reach.downstream_reach] == 0)
        order.push_back (reach.downstream_reach);
    }
    if (order.size () != rivers.reaches.size ())
      throw std::logic_error ("river reaches do not form a downstream DAG");

    // Beds must stay above the water body a path eventually enters, or the
    // flood backs up the carved channel and the whole reach turns into a
    // standing-water arm.  Propagate that floor upstream, then let only the
    // final mouth step dip under the surface.
    constexpr float no_bed = std::numeric_limits<float>::infinity ();
    constexpr float backwater_freeboard = 0.15f;
    std::vector<float> floor_m (rivers.reaches.size (), -no_bed);
    for (std::size_t index = order.size (); index-- > 0;) {
      const RiverReach& reach = rivers.reaches[order[index]];
      if (reach.downstream_ocean)
        floor_m[reach.id] =
          flood.sea_level * height_scale + backwater_freeboard;
      else if (reach.downstream_body != RiverReach::no_id &&
               reach.downstream_body < census.bodies.size ())
        floor_m[reach.id] =
          meters_value (census.bodies[reach.downstream_body].surface_level) +
          backwater_freeboard;
      else if (reach.downstream_reach != RiverReach::no_id)
        floor_m[reach.id] = floor_m[reach.downstream_reach];
    }

    std::vector<float> entry_bed (rivers.reaches.size (), no_bed);
    std::vector<CarvePoint> points;
    for (const std::uint32_t id : order) {
      const RiverReach& reach = rivers.reaches[id];
      float bed = entry_bed[reach.id];
      for (const std::uint32_t cell : reach.cells) {
        bed = std::max (std::min (bed, ground_m (cell) - depth_m (cell)),
                        floor_m[reach.id]);
        points.push_back (
          { cell,
            bed,
            channel_width_m (drainage.contributing_area.values ()[cell]) });
      }
      if (!reach.cells.empty ()) {
        // One step into standing water keeps the bed passing under the
        // mouth instead of ending in a wall at the last dry cell.
        const std::uint32_t last = reach.cells.back ();
        const std::uint32_t next = drainage.receiver[last];
        if (next != last && water_cell (next))
          points.push_back (
            { next, bed - backwater_freeboard - 0.2f, points.back ().width_m });
      }
      if (reach.downstream_reach != RiverReach::no_id)
        entry_bed[reach.downstream_reach] =
          std::min (entry_bed[reach.downstream_reach], bed);
    }

    for (const CarvePoint& point : points) {
      const int cx = static_cast<int> (point.cell) % width;
      const int cy = static_cast<int> (point.cell) / width;
      const float half_width = 0.5f * point.width_m;
      const float radius = half_width + meters_value (parameters.bank_blend);
      // Metric widths on degenerate sub-metre grids must not stamp
      // arbitrarily large neighborhoods.
      constexpr int stamp_limit_cells = 16;
      const int reach_x =
        std::min (stamp_limit_cells,
                  static_cast<int> (std::ceil (radius / grid.spacing_x_m ())));
      const int reach_y =
        std::min (stamp_limit_cells,
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
          const float ground = original[cell] * height_scale;
          if (ground <= point.bed_m)
            continue;
          const float ramp = bank_ramp (
            distance, half_width, meters_value (parameters.bank_blend));
          const float target =
            (point.bed_m + (ground - point.bed_m) * ramp) / height_scale;
          carved[cell] = std::min (carved[cell], target);
        }
    }

    ChannelCarvingReport report { .reaches = rivers.reaches.size () };
    const double cell_area =
      static_cast<double> (square_meters_value (grid.cell_area ()));
    double total_lowering = 0.0;
    double lowered_volume_m3 = 0.0;
    double maximum_lowering_m = 0.0;
    for (std::size_t cell = 0; cell < count; ++cell) {
      const double lowering =
        static_cast<double> (original[cell] - carved[cell]) * height_scale;
      if (lowering <= 0.0)
        continue;
      ++report.carved_cells;
      total_lowering += lowering;
      lowered_volume_m3 += lowering * cell_area;
      maximum_lowering_m = std::max (maximum_lowering_m, lowering);
    }
    report.lowered_volume = lowered_volume_m3 * mp_units::si::metre *
                            mp_units::si::metre * mp_units::si::metre;
    report.maximum_lowering = maximum_lowering_m * mp_units::si::metre;
    if (report.carved_cells)
      report.mean_lowering =
        total_lowering / report.carved_cells * mp_units::si::metre;
    return { .heights = std::move (carved), .report = report };
  }
}
