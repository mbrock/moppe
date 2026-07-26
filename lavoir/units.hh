#pragma once

#include <mp-units/framework.h>
#include <mp-units/math.h>
#include <mp-units/systems/iec.h>
#include <mp-units/systems/si.h>

#include <cstddef>
#include <cstdint>

/// The workshop's quantity vocabulary. Storage is measured in bytes
/// and pages of the IEC storage-capacity kind; frames and pixels are
/// counting kinds of their own, so a frame sequence cannot leak into
/// a pixel extent without saying so. Every bare number at an API
/// boundary leaves through a named numerical exit, never by accident.

namespace lavoir {
  using namespace mp_units;

  inline constexpr struct dim_occurrence : base_dimension<"#"> {
  } dim_occurrence;

  inline constexpr struct dim_cardinality : base_dimension<"@"> {
  } dim_cardinality;

  inline constexpr struct number_of_steps : quantity_spec<dim_occurrence> {
  } number_of_steps;

  inline constexpr struct number_of_units : quantity_spec<dim_cardinality> {
  } number_of_units;

  inline constexpr struct number_of_grid_units
      : quantity_spec<number_of_units, is_kind> {
  } number_of_grid_units;

  inline constexpr struct grid_offset
      : quantity_spec<number_of_grid_units,
                      is_kind,
                      quantity_tensor_order::vector> {
  } grid_offset;

  inline constexpr struct step final
      : named_unit<"step", kind_of<number_of_steps>> {
  } step;

  inline constexpr struct unit final
      : named_unit<"unit", kind_of<number_of_units>> {
  } unit;

  inline constexpr struct pixel final
      : named_unit<"px", kind_of<number_of_grid_units>> {
  } px;

  inline constexpr struct frame final : named_unit<"frame", step> {
  } frame;

  inline constexpr struct gpu_page final
      : named_unit<"page", mag_power<2, 14> * iec::byte> {
  } gpu_page;

  using bytes_t = quantity<isq::storage_capacity[iec::byte], std::size_t>;
  using gpu_pages_t = quantity<isq::storage_capacity[gpu_page], std::size_t>;
  using pixels_t = quantity<number_of_grid_units[px], std::size_t>;
  using frames_t = quantity<frame, int64_t>;
  using seconds_t = quantity<si::second>;

  /// Values held in a column are a cardinality; the size of one value
  /// is then an exchange rate between denominations, bytes per unit,
  /// and a column's storage falls out by conversion.
  using values_t = quantity<number_of_units[unit], std::size_t>;

  /// The whole pages that cover a size: ceil as unit conversion.
  inline constexpr gpu_pages_t pages_covering (bytes_t size) {
    return ceil<gpu_page> (size);
  }

  /// A BGRA8 image row walks four bytes for every pixel.
  inline constexpr auto bgra_stride = 4 * iec::byte / px;
}
