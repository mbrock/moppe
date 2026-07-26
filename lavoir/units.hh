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

  /// Frames of rendering and pixels of extent are counts of distinct
  /// kinds: severed on purpose, like every count that must not mix.
  QUANTITY_SPEC (frame_count, mp_units::dimensionless, mp_units::is_kind);
  QUANTITY_SPEC (pixel_count, mp_units::dimensionless, mp_units::is_kind);

  inline constexpr struct frame final
      : named_unit<"frame", one, kind_of<frame_count>> {
  } frame;

  inline constexpr struct pixel final
      : named_unit<"px", one, kind_of<pixel_count>> {
  } pixel;

  /// The page is a unit of storage: 2^14 bytes, the alignment and
  /// granularity Metal requires of memory it wraps without copying.
  /// Rounding an allocation up to whole pages is therefore not
  /// arithmetic but a ceiling conversion into this unit.
  inline constexpr struct page final
      : named_unit<"page", mag_power<2, 14> * iec::byte> {
  } page;

  using bytes_t = quantity<isq::storage_capacity[iec::byte], std::size_t>;
  using pages_t = quantity<isq::storage_capacity[page], std::size_t>;
  using pixels_t = quantity<pixel_count[pixel], std::size_t>;
  using frames_t = quantity<frame_count[frame], std::uint64_t>;
  using seconds_t = quantity<si::second, double>;

  inline constexpr bytes_t page_size = bytes_t (1 * page);

  /// The whole pages that cover a size: ceil as unit conversion.
  inline constexpr pages_t pages_covering (bytes_t size) {
    return ceil<page> (size);
  }

  /// A BGRA8 image row walks four bytes for every pixel.
  inline constexpr auto bgra_stride = 4 * iec::byte / pixel;
}
