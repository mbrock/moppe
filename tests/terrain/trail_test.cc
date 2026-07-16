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
