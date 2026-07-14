#include <moppe/terrain/trail.hh>

#include <moppe/profile.hh>
#include <moppe/terrain/drainage.hh>
#include <moppe/terrain/flood.hh>

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <stdexcept>
#include <vector>

namespace moppe::terrain {
  namespace {
    void validate (const TrailFormation& p) {
      if (!std::isfinite (p.sea_level) ||
          !std::isfinite (square_meters_value (p.minimum_catchment_area)) ||
          !std::isfinite (square_meters_value (p.maximum_catchment_area)) ||
          p.minimum_catchment_area <=
            0.0f * mp_units::si::metre * mp_units::si::metre ||
          p.maximum_catchment_area < p.minimum_catchment_area ||
          !std::isfinite (meters_value (p.minimum_height_above_sea)) ||
          p.minimum_height_above_sea < 0.0f * mp_units::si::metre ||
          !std::isfinite (meters_value (p.width)) ||
          p.width <= 0.0f * mp_units::si::metre ||
          !std::isfinite (meters_value (p.shoulder_blend)) ||
          p.shoulder_blend < 0.0f * mp_units::si::metre ||
          !std::isfinite (meters_value (p.maximum_cut)) ||
          p.maximum_cut < 0.0f * mp_units::si::metre ||
          !std::isfinite (meters_value (p.maximum_fill)) ||
          p.maximum_fill < 0.0f * mp_units::si::metre ||
          !std::isfinite (p.maximum_grade.numerical_value_in (mp_units::one)) ||
          p.maximum_grade < 0.0f * terrain_slope[mp_units::one] ||
          p.grading_iterations < 0)
        throw std::invalid_argument ("invalid trail formation parameters");
    }

    float shoulder_ramp (float distance, float half_width, float blend) {
      if (distance <= half_width)
        return 1.0f;
      if (blend <= 0.0f)
        return 0.0f;
      const float t = std::min ((distance - half_width) / blend, 1.0f);
      const float smooth = t * t * (3.0f - 2.0f * t);
      return 1.0f - smooth;
    }

    float receiver_distance_m (std::size_t cell,
                               std::size_t receiver,
                               const TerrainGrid& grid) {
      const int width = static_cast<int> (grid.unique_width ());
      const int height = static_cast<int> (grid.unique_height ());
      int dx =
        static_cast<int> (receiver % width) - static_cast<int> (cell % width);
      int dy =
        static_cast<int> (receiver / width) - static_cast<int> (cell / width);
      if (grid.topology == Topology::Torus) {
        if (dx > width / 2)
          dx -= width;
        if (dx < -width / 2)
          dx += width;
        if (dy > height / 2)
          dy -= height;
        if (dy < -height / 2)
          dy += height;
      }
      return std::hypot (dx * grid.spacing_x_m (), dy * grid.spacing_y_m ());
    }
  }

  TrailNetwork analyze_trail_network (const TerrainView& terrain,
                                      const TrailFormation& parameters,
                                      const DrainageGraph& drainage,
                                      const FloodField& flood) {
    MOPPE_PROFILE_ZONE ("analyze_trail_network");
    validate (parameters);
    const TerrainGrid& grid = terrain.grid ();
    const int width = static_cast<int> (grid.unique_width ());
    const int height = static_cast<int> (grid.unique_height ());
    const std::size_t count = grid.unique_size ();
    const bool periodic = grid.topology == Topology::Torus;
    const float height_scale = grid.height_scale_m ();
    const auto height_at = [&terrain, width] (std::size_t cell) {
      return terrain.at (cell % width, cell / width);
    };
    if (drainage.width () != static_cast<std::size_t> (width) ||
        drainage.height () != static_cast<std::size_t> (height) ||
        flood.width () != static_cast<std::size_t> (width) ||
        flood.height () != static_cast<std::size_t> (height))
      throw std::invalid_argument (
        "trail network readings do not match terrain");

    std::vector<std::uint8_t> centerline (count, 0);
    const float minimum_dry_height =
      parameters.sea_level * height_scale +
      meters_value (parameters.minimum_height_above_sea);
    for (std::size_t cell = 0; cell < count; ++cell) {
      const float catchment_area = drainage.contributing_area.values ()[cell];
      const bool flowing = drainage.receiver[cell] != cell;
      const bool dry = flood.water_depth.values ()[cell] <= 1e-7f;
      centerline[cell] =
        flowing && dry &&
        height_at (cell) * height_scale >= minimum_dry_height &&
        catchment_area >=
          square_meters_value (parameters.minimum_catchment_area) &&
        catchment_area <=
          square_meters_value (parameters.maximum_catchment_area);
    }

    std::vector<CellIndex> cells;
    cells.reserve (count / 32);
    for (std::uint32_t cell = 0; cell < count; ++cell)
      if (centerline[cell])
        cells.emplace_back (cell);
    std::stable_sort (
      cells.begin (), cells.end (), [&height_at] (CellIndex a, CellIndex b) {
        return height_at (a.value) < height_at (b.value);
      });

    std::vector<CellIndex> receiver (count, no_cell);
    std::vector<TrailComponentId> component_by_cell (count, no_trail_component);
    std::vector<TrailComponent> components;
    for (const CellIndex cell : cells) {
      const CellIndex next = drainage.receiver[cell.value];
      if (next.value < count && centerline[next.value] &&
          component_by_cell[next.value] != no_trail_component) {
        receiver[cell.value] = next;
        component_by_cell[cell.value] = component_by_cell[next.value];
        ++components[component_by_cell[cell.value].value].cells;
      } else {
        const TrailComponentId id { static_cast<std::uint32_t> (
          components.size ()) };
        component_by_cell[cell.value] = id;
        components.push_back (
          { .id = id, .terminal_cell = cell, .cells = cell_count (1) });
      }
    }

    std::vector<float> influence (count, 0.0f);
    const float half_width =
      std::max (0.5f * meters_value (parameters.width),
                1.01f * std::max (grid.spacing_x_m (), grid.spacing_y_m ()));
    const float blend = meters_value (parameters.shoulder_blend);
    const float radius = half_width + blend;
    constexpr int stamp_limit_cells = 64;
    const int reach_x = std::min (
      stamp_limit_cells,
      static_cast<int> (std::ceil (radius / grid.spacing_x_m ())) + 1);
    const int reach_y = std::min (
      stamp_limit_cells,
      static_cast<int> (std::ceil (radius / grid.spacing_y_m ())) + 1);
    for (const CellIndex center : cells) {
      const int cx = static_cast<int> (center.value % width);
      const int cy = static_cast<int> (center.value / width);
      for (int dy = -reach_y; dy <= reach_y; ++dy)
        for (int dx = -reach_x; dx <= reach_x; ++dx) {
          int x = cx + dx;
          int y = cy + dy;
          if (periodic) {
            x = wrap_index (x, width);
            y = wrap_index (y, height);
          } else if (x < 0 || x >= width || y < 0 || y >= height)
            continue;
          const float distance =
            std::hypot (dx * grid.spacing_x_m (), dy * grid.spacing_y_m ());
          if (distance >= radius)
            continue;
          const std::size_t cell = static_cast<std::size_t> (y) * width + x;
          if (flood.water_depth.values ()[cell] > 1e-7f)
            continue;
          influence[cell] = std::max (
            influence[cell], shoulder_ramp (distance, half_width, blend));
        }
    }

    const FieldSamplingGrid2D domain { .width =
                                         static_cast<std::size_t> (width),
                                       .height =
                                         static_cast<std::size_t> (height),
                                       .max_x = grid.spacing_x_m () * width,
                                       .max_y = grid.spacing_y_m () * height };
    return { .source_grid = grid,
             .receiver = std::move (receiver),
             .component_by_cell = std::move (component_by_cell),
             .cells = std::move (cells),
             .components = std::move (components),
             .influence = ScalarRaster (domain, std::move (influence)) };
  }

  TrailNetwork analyze_trail_network (const TerrainView& terrain,
                                      const TrailFormation& parameters) {
    const DrainageGraph drainage = analyze_drainage (terrain);
    const FloodField flood =
      analyze_standing_water (terrain, parameters.sea_level);
    return analyze_trail_network (terrain, parameters, drainage, flood);
  }

  std::vector<float> expand_trail_influence (const TrailNetwork& network) {
    const std::size_t width = network.source_grid.width;
    const std::size_t height = network.source_grid.height;
    const std::size_t unique_width = network.source_grid.unique_width ();
    const std::size_t unique_height = network.source_grid.unique_height ();
    if (network.influence.values ().size () != unique_width * unique_height)
      throw std::invalid_argument (
        "trail influence does not match its source grid");
    std::vector<float> expanded (width * height);
    for (std::size_t y = 0; y < height; ++y)
      for (std::size_t x = 0; x < width; ++x)
        expanded[y * width + x] =
          network.influence
            .values ()[(y % unique_height) * unique_width + x % unique_width];
    return expanded;
  }

  TrailFormationResult form_trails (const TerrainView& terrain,
                                    const TrailFormation& parameters) {
    MOPPE_PROFILE_ZONE ("form_trails");
    validate (parameters);
    const TerrainGrid& grid = terrain.grid ();
    const int width = static_cast<int> (grid.unique_width ());
    const int height = static_cast<int> (grid.unique_height ());
    const std::size_t count = grid.unique_size ();
    const bool periodic = grid.topology == Topology::Torus;
    const float height_scale = grid.height_scale_m ();
    const float cell_area = square_meters_value (grid.cell_area ());

    std::vector<float> original (count);
    for (int y = 0; y < height; ++y)
      for (int x = 0; x < width; ++x)
        original[static_cast<std::size_t> (y) * width + x] = terrain.at (x, y);

    const DrainageGraph drainage = analyze_drainage (terrain);
    const FloodField flood =
      analyze_standing_water (terrain, parameters.sea_level);
    TrailNetwork network =
      analyze_trail_network (terrain, parameters, drainage, flood);

    // Project every selected drainage edge toward the requested grade. Both
    // endpoints move, but their own earthwork limits prevent the path from
    // replacing the shape of the valley with an engineered road cutting.
    std::vector<float> grade_m (count);
    for (std::size_t cell = 0; cell < count; ++cell)
      grade_m[cell] = original[cell] * height_scale;
    const float maximum_grade =
      parameters.maximum_grade.numerical_value_in (mp_units::one);
    const float maximum_cut = meters_value (parameters.maximum_cut);
    const float maximum_fill = meters_value (parameters.maximum_fill);
    for (int iteration = 0;
         iteration < count_value (parameters.grading_iterations);
         ++iteration) {
      for (std::size_t cell = 0; cell < count; ++cell) {
        const CellIndex receiver = network.receiver[cell];
        if (!network.contains (
              CellIndex { static_cast<std::uint32_t> (cell) }) ||
            receiver == no_cell)
          continue;
        const std::size_t next = receiver.value;
        const float allowed =
          maximum_grade * receiver_distance_m (cell, next, grid);
        const float difference = grade_m[cell] - grade_m[next];
        if (std::fabs (difference) <= allowed)
          continue;
        const float excess = std::fabs (difference) - allowed;
        const float direction = difference > 0.0f ? 1.0f : -1.0f;
        grade_m[cell] -= direction * 0.5f * excess;
        grade_m[next] += direction * 0.5f * excess;
        grade_m[cell] =
          std::clamp (grade_m[cell],
                      original[cell] * height_scale - maximum_cut,
                      original[cell] * height_scale + maximum_fill);
        grade_m[next] =
          std::clamp (grade_m[next],
                      original[next] * height_scale - maximum_cut,
                      original[next] * height_scale + maximum_fill);
      }
    }

    // A weighted stamp makes confluences independent of traversal order. At
    // coarse preview resolutions the effective core grows just enough for
    // adjacent D8 samples to remain a continuous ribbon.
    std::vector<float> target_sum (count, 0.0f);
    std::vector<float> influence_sum (count, 0.0f);
    const float half_width =
      std::max (0.5f * meters_value (parameters.width),
                1.01f * std::max (grid.spacing_x_m (), grid.spacing_y_m ()));
    const float blend = meters_value (parameters.shoulder_blend);
    const float radius = half_width + blend;
    constexpr int stamp_limit_cells = 64;
    const int reach_x = std::min (
      stamp_limit_cells,
      static_cast<int> (std::ceil (radius / grid.spacing_x_m ())) + 1);
    const int reach_y = std::min (
      stamp_limit_cells,
      static_cast<int> (std::ceil (radius / grid.spacing_y_m ())) + 1);
    for (const CellIndex center : network.cells) {
      const int cx = static_cast<int> (center.value % width);
      const int cy = static_cast<int> (center.value / width);
      for (int dy = -reach_y; dy <= reach_y; ++dy)
        for (int dx = -reach_x; dx <= reach_x; ++dx) {
          int x = cx + dx;
          int y = cy + dy;
          if (periodic) {
            x = wrap_index (x, width);
            y = wrap_index (y, height);
          } else if (x < 0 || x >= width || y < 0 || y >= height)
            continue;
          const float distance =
            std::hypot (dx * grid.spacing_x_m (), dy * grid.spacing_y_m ());
          if (distance >= radius)
            continue;
          const std::size_t cell = static_cast<std::size_t> (y) * width + x;
          if (flood.water_depth.values ()[cell] > 1e-7f)
            continue;
          const float influence = shoulder_ramp (distance, half_width, blend);
          target_sum[cell] += influence * grade_m[center.value];
          influence_sum[cell] += influence;
        }
    }

    std::vector<float> shaped = original;
    TrailFormationReport report;
    report.centerline_cells = cell_count (network.cells.size ());
    report.connected_components = network.components.size ();
    double absolute_change = 0.0;
    double maximum_change = 0.0;
    double cut_volume = 0.0;
    double fill_volume = 0.0;
    for (std::size_t cell = 0; cell < count; ++cell) {
      if (influence_sum[cell] <= 0.0f)
        continue;
      const float original_m = original[cell] * height_scale;
      const float target_m = target_sum[cell] / influence_sum[cell];
      const float influence = std::min (influence_sum[cell], 1.0f);
      const float shaped_m =
        std::clamp (original_m + influence * (target_m - original_m),
                    original_m - maximum_cut,
                    original_m + maximum_fill);
      const double change = shaped_m - original_m;
      if (std::fabs (change) <= 1e-6)
        continue;
      shaped[cell] = shaped_m / height_scale;
      ++report.shaped_cells;
      absolute_change += std::fabs (change);
      maximum_change = std::max (maximum_change, std::fabs (change));
      if (change < 0.0)
        cut_volume += -change * cell_area;
      else
        fill_volume += change * cell_area;
    }
    report.cut_volume = cut_volume * mp_units::si::metre * mp_units::si::metre *
                        mp_units::si::metre;
    report.fill_volume = fill_volume * mp_units::si::metre *
                         mp_units::si::metre * mp_units::si::metre;
    report.maximum_absolute_change = maximum_change * mp_units::si::metre;
    if (report.shaped_cells)
      report.mean_absolute_change = absolute_change /
                                    count_value (report.shaped_cells) *
                                    mp_units::si::metre;
    return { .heights = std::move (shaped),
             .network = std::move (network),
             .report = report };
  }
}
