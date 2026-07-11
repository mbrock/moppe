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
      if (!std::isfinite (p.sea_level)
	  || !std::isfinite (p.minimum_area_cells)
	  || p.minimum_area_cells <= 0.0f
	  || !std::isfinite (p.depth_per_sqrt_m2)
	  || p.depth_per_sqrt_m2 < 0.0f
	  || !std::isfinite (p.minimum_depth_m) || p.minimum_depth_m < 0.0f
	  || !std::isfinite (p.maximum_depth_m)
	  || p.maximum_depth_m < p.minimum_depth_m
	  || !std::isfinite (p.bank_blend_m) || p.bank_blend_m < 0.0f)
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

  float
  channel_width_m (float area_m2) noexcept
  {
    return std::clamp (0.008f * std::sqrt (area_m2), 1.2f, 12.0f);
  }

  ChannelCarvingResult
  carve_channels (const TerrainView& terrain,
		  const ChannelCarving& parameters)
  {
    validate (parameters);
    const TerrainGrid& grid = terrain.grid ();
    const int width = static_cast<int> (grid.unique_width ());
    const int height = static_cast<int> (grid.unique_height ());
    const std::size_t count = grid.unique_size ();
    const bool periodic = grid.topology == Topology::Torus;
    const float height_scale = grid.height_scale;

    std::vector<float> original (count);
    for (int y = 0; y < height; ++y)
      for (int x = 0; x < width; ++x)
	original[static_cast<std::size_t> (y) * width + x] = terrain.at (x, y);
    std::vector<float> carved = original;

    const FloodField flood = analyze_standing_water
      (terrain, parameters.sea_level);
    const LakeCensus census = census_lakes (flood);
    const DrainageGraph drainage = analyze_wet_drainage
      (terrain, flood, census);
    const float minimum_area_m2 = parameters.minimum_area_cells
      * grid.spacing_x * grid.spacing_y;
    const RiverNetwork rivers = extract_river_network
      (flood, census, drainage, minimum_area_m2);

    const auto ground_m = [&] (std::uint32_t cell) {
      return original[cell] * height_scale;
    };
    const auto depth_m = [&] (std::uint32_t cell) {
      const float area = drainage.contributing_area.values ()[cell];
      const float depth = std::clamp
	(parameters.depth_per_sqrt_m2 * std::sqrt (area),
	 parameters.minimum_depth_m, parameters.maximum_depth_m);
      // Channels narrower than a cell become proportionally shallower
      // notches instead of full-depth cell-wide trenches.
      const float coverage = std::clamp
	(channel_width_m (area) / grid.spacing_x, 0.35f, 1.0f);
      return depth * coverage;
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
    constexpr float no_bed = std::numeric_limits<float>::infinity ();
    std::vector<float> entry_bed (rivers.reaches.size (), no_bed);
    std::vector<std::uint32_t> ready;
    for (const RiverReach& reach : rivers.reaches)
      if (pending[reach.id] == 0)
	ready.push_back (reach.id);

    std::vector<CarvePoint> points;
    std::size_t processed = 0;
    while (!ready.empty ()) {
      const RiverReach& reach = rivers.reaches[ready.back ()];
      ready.pop_back ();
      ++processed;
      float bed = entry_bed[reach.id];
      for (const std::uint32_t cell : reach.cells) {
	bed = std::min (bed, ground_m (cell) - depth_m (cell));
	points.push_back ({ cell, bed, channel_width_m
	  (drainage.contributing_area.values ()[cell]) });
      }
      if (!reach.cells.empty ()) {
	// One step into standing water keeps the bed passing under the
	// mouth instead of ending in a wall at the last dry cell.
	const std::uint32_t last = reach.cells.back ();
	const std::uint32_t next = drainage.receiver[last];
	if (next != last && water_cell (next))
	  points.push_back ({ next, bed, points.back ().width_m });
      }
      if (reach.downstream_reach != RiverReach::no_id) {
	entry_bed[reach.downstream_reach] = std::min
	  (entry_bed[reach.downstream_reach], bed);
	if (--pending[reach.downstream_reach] == 0)
	  ready.push_back (reach.downstream_reach);
      }
    }
    if (processed != rivers.reaches.size ())
      throw std::logic_error ("river reaches do not form a downstream DAG");

    for (const CarvePoint& point : points) {
      const int cx = static_cast<int> (point.cell) % width;
      const int cy = static_cast<int> (point.cell) / width;
      const float half_width = 0.5f * point.width_m;
      const float radius = half_width + parameters.bank_blend_m;
      // Metric widths on degenerate sub-metre grids must not stamp
      // arbitrarily large neighborhoods.
      constexpr int stamp_limit_cells = 16;
      const int reach_x = std::min (stamp_limit_cells, static_cast<int>
	(std::ceil (radius / grid.spacing_x)));
      const int reach_y = std::min (stamp_limit_cells, static_cast<int>
	(std::ceil (radius / grid.spacing_y)));
      for (int dy = -reach_y; dy <= reach_y; ++dy)
	for (int dx = -reach_x; dx <= reach_x; ++dx) {
	  int x = cx + dx;
	  int y = cy + dy;
	  if (periodic) {
	    x = (x % width + width) % width;
	    y = (y % height + height) % height;
	  } else if (x < 0 || x >= width || y < 0 || y >= height)
	    continue;
	  const float distance = std::hypot
	    (dx * grid.spacing_x, dy * grid.spacing_y);
	  if (distance >= radius)
	    continue;
	  const std::size_t cell =
	    static_cast<std::size_t> (y) * width + x;
	  const float ground = original[cell] * height_scale;
	  if (ground <= point.bed_m)
	    continue;
	  const float ramp = bank_ramp
	    (distance, half_width, parameters.bank_blend_m);
	  const float target =
	    (point.bed_m + (ground - point.bed_m) * ramp) / height_scale;
	  carved[cell] = std::min (carved[cell], target);
	}
    }

    ChannelCarvingReport report { .reaches = rivers.reaches.size () };
    const double cell_area = static_cast<double> (grid.spacing_x)
      * grid.spacing_y;
    double total_lowering = 0.0;
    for (std::size_t cell = 0; cell < count; ++cell) {
      const double lowering = static_cast<double>
	(original[cell] - carved[cell]) * height_scale;
      if (lowering <= 0.0)
	continue;
      ++report.carved_cells;
      total_lowering += lowering;
      report.lowered_volume_m3 += lowering * cell_area;
      report.maximum_lowering_m = std::max
	(report.maximum_lowering_m, lowering);
    }
    if (report.carved_cells)
      report.mean_lowering_m = total_lowering / report.carved_cells;
    return { .heights = std::move (carved), .report = report };
  }
}
