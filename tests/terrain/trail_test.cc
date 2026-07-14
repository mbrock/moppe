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
             .grading_iterations = iteration_count (20) };
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
  MOPPE_CHECK (!network.components.empty ());
  MOPPE_CHECK (network.influence.values ().size () == original.size ());
  for (const CellIndex cell : network.cells) {
    MOPPE_CHECK (network.contains (cell));
    const CellIndex next = network.receiver[cell.value];
    if (next != no_cell) {
      MOPPE_CHECK (network.contains (next));
      MOPPE_CHECK (network.component_by_cell[cell.value] ==
                   network.component_by_cell[next.value]);
    }
    CellIndex cursor = cell;
    std::size_t steps = 0;
    while (network.receiver[cursor.value] != no_cell &&
           steps <= network.cells.size ()) {
      cursor = network.receiver[cursor.value];
      ++steps;
    }
    MOPPE_CHECK (steps <= network.cells.size ());
  }
  for (const float influence : network.influence.values ()) {
    MOPPE_CHECK (influence >= 0.0f);
    MOPPE_CHECK (influence <= 1.0f);
  }
}

MOPPE_TEST (trail_formation_is_deterministic_and_bounded) {
  const std::vector<float> original = bumpy_valley ();
  const TerrainView terrain (trail_valley_grid (), original);
  const TrailFormation parameters = test_parameters ();
  const TrailFormationResult first = form_trails (terrain, parameters);
  const TrailFormationResult second = form_trails (terrain, parameters);

  MOPPE_CHECK (first.heights == second.heights);
  MOPPE_CHECK (first.network.receiver == second.network.receiver);
  MOPPE_CHECK (first.network.component_by_cell ==
               second.network.component_by_cell);
  MOPPE_CHECK (std::ranges::equal (first.network.influence.values (),
                                   second.network.influence.values ()));
  for (std::size_t cell = 0; cell < original.size (); ++cell) {
    const float change_m = (first.heights[cell] - original[cell]) *
                           trail_valley_grid ().height_scale_m ();
    MOPPE_CHECK (change_m >= -meters_value (parameters.maximum_cut) - 1e-5f);
    MOPPE_CHECK (change_m <= meters_value (parameters.maximum_fill) + 1e-5f);
  }
}
