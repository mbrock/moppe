#include <moppe/terrain/cpu_evaluator.hh>
#include <moppe/terrain/discretization.hh>

#include <tests/test.hh>

#include <concepts>
#include <stdexcept>

using namespace moppe;
using namespace moppe::terrain;

namespace {
  template <typename Left, typename Right>
  concept lattice_addable = requires (Left left, Right right) { left + right; };

  static_assert (lattice_addable<RowOffset, RowOffset>);
  static_assert (lattice_addable<RowIndex, RowOffset>);
  static_assert (!lattice_addable<RowIndex, RowIndex>);
  static_assert (!lattice_addable<RowOffset, ColumnOffset>);
  static_assert (!std::same_as<RowIndex, ColumnIndex>);
  static_assert (!std::same_as<SampleIndex, RowIndex>);
}

MOPPE_TEST (terrain_discretization_connects_field_and_physical_positions) {
  const TerrainDiscretization discretization (
    { .width = 5,
      .height = 3,
      .min_x = -1.0f,
      .max_x = 1.0f,
      .min_y = 2.0f,
      .max_y = 6.0f },
    { .width = 5,
      .height = 3,
      .spacing_x = 10.0f * mp_units::si::metre,
      .spacing_y = 20.0f * mp_units::si::metre,
      .height_scale = 500.0f * mp_units::si::metre });

  const GridPointIndex point = grid_point (2, 1);
  const FieldCoordinate2D coordinate = discretization.field_position (point);
  MOPPE_CHECK (coordinate.u == field_coordinate (0.0f));
  MOPPE_CHECK (coordinate.v == field_coordinate (4.0f));

  const HorizontalPosition2D physical =
    discretization.physical_position (point);
  MOPPE_CHECK (physical.x == 20.0f * mp_units::si::metre);
  MOPPE_CHECK (physical.z == 20.0f * mp_units::si::metre);
  MOPPE_CHECK (discretization.grid ().cell_area () ==
               200.0f * mp_units::si::metre * mp_units::si::metre);
}

MOPPE_TEST (terrain_discretization_rejects_unrelated_lattice_extents) {
  bool threw = false;
  try {
    (void)TerrainDiscretization ({ .width = 4, .height = 3 },
                                 { .width = 5, .height = 3 });
  } catch (const std::invalid_argument&) {
    threw = true;
  }
  MOPPE_CHECK (threw);
}

MOPPE_TEST (terrain_raster_retains_the_discretization_that_sampled_it) {
  const TerrainDiscretization discretization (
    { .width = 3, .height = 2 },
    { .width = 3,
      .height = 2,
      .spacing_x = 4.0f * mp_units::si::metre,
      .spacing_y = 7.0f * mp_units::si::metre });
  const RelativeElevationField field =
    field_cast<relative_elevation> (coordinate_u () + 2.0f * coordinate_v ());

  const TerrainRelativeElevationRaster raster =
    materialize (CpuEvaluator (), field, discretization);

  const GridPointIndex point = grid_point (2, 1);
  MOPPE_CHECK (raster.sample (point) == relative_elevation (3.0f));
  MOPPE_CHECK (raster.field_position (point).u == field_coordinate (1.0f));
  MOPPE_CHECK (raster.physical_position (point).x ==
               8.0f * mp_units::si::metre);
  MOPPE_CHECK (raster.physical_position (point).z ==
               7.0f * mp_units::si::metre);
}

MOPPE_TEST (grid_points_flatten_to_affine_storage_indices) {
  const TerrainDiscretization discretization ({ .width = 5, .height = 4 },
                                              { .width = 5, .height = 4 });
  const GridPointIndex point = grid_point (3, 2);

  const SampleIndex flat = discretization.flatten (point);
  MOPPE_CHECK (sample_number (flat) == 13);
  MOPPE_CHECK (discretization.unflatten (flat) == point);

  const ColumnOffset two_columns = column_offset (2 * column_unit);
  const RowOffset one_row = row_offset (1 * row_unit);
  MOPPE_CHECK (point.column + two_columns == column_index (5));
  MOPPE_CHECK (point.row - one_row == row_index (1));
}

MOPPE_TEST (toroidal_discretization_distinguishes_storage_and_unique_points) {
  const TerrainDiscretization discretization (
    { .width = 6, .height = 4 },
    { .width = 6, .height = 4, .topology = Topology::Torus });

  MOPPE_CHECK (discretization.grid ().width == 6);
  MOPPE_CHECK (discretization.grid ().height == 4);
  MOPPE_CHECK (discretization.grid ().unique_width () == 5);
  MOPPE_CHECK (discretization.grid ().unique_height () == 3);
  MOPPE_CHECK (discretization.grid ().unique_size () == 15);
}
