#include <moppe/terrain/trail.hh>

#include <tests/test.hh>

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <vector>

using namespace moppe::terrain;

namespace {
  std::vector<float> bumpy_valley () {
    std::vector<float> heights (9 * 9);
    for (int y = 0; y < 9; ++y)
      for (int x = 0; x < 9; ++x) {
        const float bump = x == 4 && y == 4 ? 0.02f : 0.0f;
        heights[static_cast<std::size_t> (y) * 9 + x] =
          y == 8 ? -0.1f : 0.65f - 0.005f * y + 0.03f * std::abs (x - 4) + bump;
      }
    return heights;
  }

  TerrainGrid trail_valley_grid () {
    return { .width = 9,
             .height = 9,
             .spacing_x = 5.0f * mp_units::si::metre,
             .spacing_y = 5.0f * mp_units::si::metre,
             .height_scale = 100.0f * mp_units::si::metre };
  }

  TrailFormation test_parameters () {
    return { .sea_level = 0.0f,
             .minimum_catchment_area =
               75.0f * mp_units::si::metre * mp_units::si::metre,
             .maximum_catchment_area =
               25000.0f * mp_units::si::metre * mp_units::si::metre,
             .minimum_height_above_sea = 0.0f * mp_units::si::metre,
             .width = 2.0f * mp_units::si::metre,
             .shoulder_blend = 2.0f * mp_units::si::metre,
             .maximum_cut = 2.0f * mp_units::si::metre,
             .maximum_fill = 2.0f * mp_units::si::metre,
             .maximum_grade = 0.2f * terrain_slope[mp_units::one],
             .designed_grade = 0.06f * terrain_slope[mp_units::one],
             .grading_iterations = iteration_count (20) };
  }

  std::vector<float> moated_peak () {
    std::vector<float> heights (17 * 17, 0.48f);
    for (int y = 5; y <= 11; ++y)
      for (int x = 5; x <= 11; ++x) {
        const bool moat = x == 5 || x == 11 || y == 5 || y == 11;
        heights[static_cast<std::size_t> (y) * 17 + x] =
          moat ? 0.10f
               : 0.78f - 0.02f * std::max (std::abs (x - 8), std::abs (y - 8));
      }
    for (int y = 0; y < 5; ++y)
      heights[static_cast<std::size_t> (y) * 17 + 5] = 0.10f;
    return heights;
  }

  TerrainGrid moated_peak_grid () {
    return { .width = 17,
             .height = 17,
             .spacing_x = 24.0f * mp_units::si::metre,
             .spacing_y = 24.0f * mp_units::si::metre,
             .height_scale = 20.0f * mp_units::si::metre };
  }

  std::vector<float> alpine_temptation () {
    constexpr int side = 25;
    std::vector<float> heights (side * side, 0.16f);
    for (int y = 0; y < side; ++y)
      for (int x = 0; x < side; ++x) {
        const float radius = std::hypot (x - 12.0f, y - 11.0f);
        if (radius < 6.0f)
          heights[static_cast<std::size_t> (y) * side + x] =
            0.76f - 0.07f * radius;
      }
    for (int x = 0; x < side; ++x)
      heights[24 * side + x] = -0.1f;
    return heights;
  }

  TerrainGrid alpine_temptation_grid () {
    return { .width = 25,
             .height = 25,
             .spacing_x = 100.0f * mp_units::si::metre,
             .spacing_y = 100.0f * mp_units::si::metre,
             .height_scale = 100.0f * mp_units::si::metre };
  }

  float sample_height_m (const std::vector<float>& heights,
                         const TerrainGrid& grid,
                         float x_m,
                         float z_m) {
    const float x = std::clamp (
      x_m / grid.spacing_x_m (), 0.0f, static_cast<float> (grid.width - 1));
    const float y = std::clamp (
      z_m / grid.spacing_y_m (), 0.0f, static_cast<float> (grid.height - 1));
    const int x0 = static_cast<int> (std::floor (x));
    const int y0 = static_cast<int> (std::floor (y));
    const int x1 = std::min (x0 + 1, static_cast<int> (grid.width - 1));
    const int y1 = std::min (y0 + 1, static_cast<int> (grid.height - 1));
    const float top = std::lerp (
      heights[y0 * grid.width + x0], heights[y0 * grid.width + x1], x - x0);
    const float bottom = std::lerp (
      heights[y1 * grid.width + x0], heights[y1 * grid.width + x1], x - x0);
    return std::lerp (top, bottom, y - y0) * grid.height_scale_m ();
  }
}

MOPPE_TEST (trail_formation_grades_a_dry_valley_floor) {
  const std::vector<float> original = bumpy_valley ();
  const TrailFormationResult result = form_trails (
    TerrainView (trail_valley_grid (), original), test_parameters ());

  MOPPE_CHECK (result.heights.size () == original.size ());
  MOPPE_CHECK (result.report.centerline_cells > cell_count (0));
  MOPPE_CHECK (result.report.shaped_cells > cell_count (0));
  MOPPE_CHECK (result.report.connected_components > 0);
  MOPPE_CHECK (result.report.cut_volume > 0.0);
  MOPPE_CHECK (result.report.fill_volume > 0.0);
  MOPPE_CHECK (result.report.mean_centerline_grade >= 0.0);
  MOPPE_CHECK (result.report.maximum_centerline_grade >=
               result.report.mean_centerline_grade);
  MOPPE_CHECK (meters_value (result.report.maximum_centerline_step) <=
               std::hypot (trail_valley_grid ().spacing_x_m (),
                           trail_valley_grid ().spacing_y_m ()) +
                 1e-5f);
  bool valley_changed = false;
  for (int y = 1; y < 8; ++y)
    for (int x = 3; x <= 5; ++x)
      valley_changed |=
        std::fabs (result.heights[y * 9 + x] - original[y * 9 + x]) > 1e-6f;
  MOPPE_CHECK (valley_changed);

  // The path remains in the low corridor rather than planing the hillsides.
  MOPPE_CHECK_NEAR (result.heights[4 * 9], original[4 * 9], 1e-7f);
  MOPPE_CHECK_NEAR (result.heights[4 * 9 + 8], original[4 * 9 + 8], 1e-7f);
  // Standing water is never used as trail or modified by a nearby stamp.
  for (int x = 0; x < 9; ++x)
    MOPPE_CHECK_NEAR (result.heights[8 * 9 + x], original[8 * 9 + x], 1e-7f);
}

MOPPE_TEST (trail_network_retains_connected_graph_and_material_footprint) {
  const std::vector<float> original = bumpy_valley ();
  const TrailFormationResult result = form_trails (
    TerrainView (trail_valley_grid (), original), test_parameters ());
  const TrailNetwork& network = result.network;

  MOPPE_CHECK (!network.cells.empty ());
  MOPPE_CHECK (network.components.size () == 1);
  MOPPE_CHECK (network.plan.home_base != no_cell);
  MOPPE_CHECK (network.plan.scenic_focus != no_cell);
  MOPPE_CHECK (network.plan.circuit == network.cells);
  MOPPE_CHECK (network.cells.front () == network.plan.home_base);
  MOPPE_CHECK (network.alignment.points.size () > network.cells.size ());
  MOPPE_CHECK (network.alignment.length > 0.0 * mp_units::si::metre);
  MOPPE_CHECK (network.influence.values ().size () == original.size ());
  MOPPE_CHECK (network.home_base_influence.values ().size () ==
               original.size ());
  MOPPE_CHECK (network.components.front ().anchor_cell ==
               network.plan.home_base);
  MOPPE_CHECK_NEAR (
    network.home_base_influence.values ()[network.plan.home_base.value],
    1.0f,
    1e-6f);
  for (const CellIndex cell : network.cells) {
    MOPPE_CHECK (network.contains (cell));
    const CellIndex next = network.receiver[cell.value];
    MOPPE_CHECK (next != no_cell);
    MOPPE_CHECK (network.contains (next));
    MOPPE_CHECK (network.component_by_cell[cell.value] ==
                 network.component_by_cell[next.value]);
    CellIndex cursor = cell;
    for (std::size_t steps = 0; steps < network.cells.size (); ++steps)
      cursor = network.receiver[cursor.value];
    MOPPE_CHECK (cursor == cell);
  }
  for (const float influence : network.influence.values ()) {
    MOPPE_CHECK (influence >= 0.0f);
    MOPPE_CHECK (influence <= 1.0f);
  }

  const std::size_t width = network.source_grid.unique_width ();
  const float home_x =
    (network.plan.home_base.value % width) * network.source_grid.spacing_x_m ();
  const float home_z =
    (network.plan.home_base.value / width) * network.source_grid.spacing_y_m ();
  MOPPE_CHECK_NEAR (network.alignment.points.front ().x_m, home_x, 1e-5f);
  MOPPE_CHECK_NEAR (network.alignment.points.front ().z_m, home_z, 1e-5f);

  float maximum_step = 0.0f;
  bool has_non_grid_heading = false;
  for (std::size_t point = 0; point < network.alignment.points.size ();
       ++point) {
    const TrailAlignmentPoint a = network.alignment.points[point];
    const TrailAlignmentPoint b =
      network.alignment.points[(point + 1) % network.alignment.points.size ()];
    const float dx = b.x_m - a.x_m;
    const float dz = b.z_m - a.z_m;
    maximum_step = std::max (maximum_step, std::hypot (dx, dz));
    has_non_grid_heading |= std::fabs (dx) > 1e-4f && std::fabs (dz) > 1e-4f &&
                            std::fabs (std::fabs (dx) - std::fabs (dz)) > 1e-3f;
  }
  // Hermite speed varies slightly inside each chord, but sampling remains
  // comfortably denser than the five-metre source lattice.
  MOPPE_CHECK (maximum_step <= 3.0f);
  MOPPE_CHECK (has_non_grid_heading);
}

MOPPE_TEST (trail_formation_is_deterministic_and_bounded) {
  const std::vector<float> original = bumpy_valley ();
  const TerrainView terrain (trail_valley_grid (), original);
  const TrailFormation parameters = test_parameters ();
  const TrailFormationResult first = form_trails (terrain, parameters);
  const TrailFormationResult second = form_trails (terrain, parameters);

  MOPPE_CHECK (first.heights == second.heights);
  MOPPE_CHECK (first.network.earthwork_delta_m ==
               second.network.earthwork_delta_m);
  MOPPE_CHECK (first.network.earthwork_delta_m.size () == original.size ());
  MOPPE_CHECK (first.network.alignment == second.network.alignment);
  MOPPE_CHECK (first.network.receiver == second.network.receiver);
  MOPPE_CHECK (first.network.component_by_cell ==
               second.network.component_by_cell);
  MOPPE_CHECK (first.network.plan.home_base == second.network.plan.home_base);
  MOPPE_CHECK (first.network.plan.control_sites ==
               second.network.plan.control_sites);
  MOPPE_CHECK (std::ranges::equal (first.network.influence.values (),
                                   second.network.influence.values ()));
  for (std::size_t cell = 0; cell < original.size (); ++cell) {
    const float change_m = (first.heights[cell] - original[cell]) *
                           trail_valley_grid ().height_scale_m ();
    MOPPE_CHECK_NEAR (first.network.earthwork_delta_m[cell], change_m, 1e-5f);
    MOPPE_CHECK (change_m >= -meters_value (parameters.maximum_cut) - 1e-5f);
    MOPPE_CHECK (change_m <= meters_value (parameters.maximum_fill) + 1e-5f);
  }
}

MOPPE_TEST (trail_crossfall_drains_toward_the_naturally_lower_side) {
  const std::vector<float> original = bumpy_valley ();
  TrailFormation level_parameters = test_parameters ();
  level_parameters.crossfall = 0.0f * terrain_slope[mp_units::one];
  TrailFormation drained_parameters = level_parameters;
  drained_parameters.crossfall = 0.08f * terrain_slope[mp_units::one];

  const TrailFormationResult level = form_trails (
    TerrainView (trail_valley_grid (), original), level_parameters);
  const TrailFormationResult drained = form_trails (
    TerrainView (trail_valley_grid (), original), drained_parameters);

  // Crossfall changes only the constructed section, not the planned route.
  MOPPE_CHECK (drained.network.alignment == level.network.alignment);
  MOPPE_CHECK (drained.network.plan.circuit == level.network.plan.circuit);
  MOPPE_CHECK (drained.heights != level.heights);
  for (std::size_t cell = 0; cell < original.size (); ++cell) {
    const float change_m = (drained.heights[cell] - original[cell]) *
                           trail_valley_grid ().height_scale_m ();
    MOPPE_CHECK (change_m >=
                 -meters_value (drained_parameters.maximum_cut) - 1e-5f);
    MOPPE_CHECK (change_m <=
                 meters_value (drained_parameters.maximum_fill) + 1e-5f);
  }

  const TerrainGrid grid = trail_valley_grid ();
  const float maximum_x = (grid.width - 1) * grid.spacing_x_m ();
  const float maximum_z = (grid.height - 1) * grid.spacing_y_m ();
  bool observed_downhill_fall = false;
  for (std::size_t segment = 0;
       segment + 1 < drained.network.alignment.points.size ();
       ++segment) {
    const TrailAlignmentPoint a = drained.network.alignment.points[segment];
    const TrailAlignmentPoint b = drained.network.alignment.points[segment + 1];
    const float dx = b.x_m - a.x_m;
    const float dz = b.z_m - a.z_m;
    const float length = std::hypot (dx, dz);
    if (length <= 1e-5f)
      continue;
    const float mid_x = 0.5f * (a.x_m + b.x_m);
    const float mid_z = 0.5f * (a.z_m + b.z_m);
    const float nx = -dz / length;
    const float nz = dx / length;
    constexpr float natural_probe = 2.5f;
    constexpr float tread_probe = 1.0f;
    if (mid_x - natural_probe < 0.0f || mid_z - natural_probe < 0.0f ||
        mid_x + natural_probe > maximum_x || mid_z + natural_probe > maximum_z)
      continue;
    const float natural_positive = sample_height_m (
      original, grid, mid_x + nx * natural_probe, mid_z + nz * natural_probe);
    const float natural_negative = sample_height_m (
      original, grid, mid_x - nx * natural_probe, mid_z - nz * natural_probe);
    const float downhill_sign =
      natural_positive <= natural_negative ? 1.0f : -1.0f;
    const auto cross_section = [&] (const std::vector<float>& heights) {
      const float downhill =
        sample_height_m (heights,
                         grid,
                         mid_x + downhill_sign * nx * tread_probe,
                         mid_z + downhill_sign * nz * tread_probe);
      const float uphill =
        sample_height_m (heights,
                         grid,
                         mid_x - downhill_sign * nx * tread_probe,
                         mid_z - downhill_sign * nz * tread_probe);
      return downhill - uphill;
    };
    if (cross_section (drained.heights) <
        cross_section (level.heights) - 1e-4f) {
      observed_downhill_fall = true;
      break;
    }
  }
  MOPPE_CHECK (observed_downhill_fall);
}

MOPPE_TEST (trail_circuit_keeps_control_sites_on_home_base_land) {
  TrailFormation parameters = test_parameters ();
  parameters.sea_level = 0.2f;
  parameters.home_base_water_distance = 60.0f * mp_units::si::metre;
  parameters.desired_circuit_radius = 110.0f * mp_units::si::metre;
  const TrailFormationResult result =
    form_trails (TerrainView (moated_peak_grid (), moated_peak ()), parameters);

  MOPPE_CHECK (result.network.components.size () == 1);
  MOPPE_CHECK (result.network.cells.size () >= 4);
  for (const CellIndex site : result.network.plan.control_sites) {
    const int x = static_cast<int> (site.value % 17);
    const int y = static_cast<int> (site.value / 17);
    MOPPE_CHECK (!(x >= 6 && x <= 10 && y >= 6 && y <= 10));
  }
}

MOPPE_TEST (pioneer_circuit_views_the_mountain_from_below) {
  const std::vector<float> original = alpine_temptation ();
  TrailFormation parameters = test_parameters ();
  parameters.home_base_water_distance = 200.0f * mp_units::si::metre;
  parameters.desired_circuit_radius = 700.0f * mp_units::si::metre;
  parameters.highland_preference_height_above_sea = 20.0f * mp_units::si::metre;
  parameters.alpine_avoidance_height_above_sea = 40.0f * mp_units::si::metre;
  const TrailFormationResult result =
    form_trails (TerrainView (alpine_temptation_grid (), original), parameters);

  float maximum_original_height = 0.0f;
  for (const CellIndex cell : result.network.cells)
    maximum_original_height =
      std::max (maximum_original_height, original[cell.value] * 100.0f);
  MOPPE_CHECK (maximum_original_height < 40.0f);
  MOPPE_CHECK (
    meters_value (result.report.maximum_centerline_height_above_sea) < 40.0f);
}
