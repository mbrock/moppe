#include <lavoir/buffer.hh>
#include <lavoir/bundle.hh>

#include <tests/test.hh>

#include <cstdint>
#include <numeric>
#include <utility>

namespace lv = lavoir;

MOPPE_TEST (lavoir_buffers_are_page_aligned_and_page_rounded) {
  const lv::buffer storage = lv::buffer::with_size (100 * mp_units::iec::byte);
  MOPPE_CHECK (storage.size () == 100 * mp_units::iec::byte);
  MOPPE_CHECK (storage.capacity () == lv::page_size);
  MOPPE_CHECK (reinterpret_cast<std::uintptr_t> (storage.lease ().data ()) %
                 lv::page_size.numerical_value_in (mp_units::iec::byte) ==
               0);
}

MOPPE_TEST (lavoir_columns_lease_typed_views_of_owned_storage) {
  lv::column<float> heights = lv::column<float>::with_count (64);
  MOPPE_CHECK (heights.count () == 64);

  auto values = heights.lease ();
  std::iota (values.begin (), values.end (), 0.0f);
  MOPPE_CHECK (heights.lease ()[63] == 63.0f);

  lv::column<float> moved = std::move (heights);
  MOPPE_CHECK (moved.count () == 64);
  MOPPE_CHECK (moved.lease ()[63] == 63.0f);
}

MOPPE_TEST (lavoir_fields_are_bundles_of_borrowed_values) {
  static_assert (lv::Domain<lv::interval>);
  static_assert (lv::Bundle<lv::field<lv::interval, int>>);

  lv::column<int> storage = lv::column<int>::with_count (9);
  const lv::interval sites (9);
  const lv::field<lv::interval, int> counts (sites, storage.lease ());

  for (std::size_t site = 0; site < sites.size (); ++site)
    counts[sites.index (site)] = static_cast<int> (site * site);

  MOPPE_CHECK (counts[3] == 9);
  MOPPE_CHECK (storage.lease ()[8] == 64);
}

MOPPE_TEST (lavoir_pages_cover_sizes_by_ceiling_conversion) {
  MOPPE_CHECK (lv::pages_covering (lv::bytes_t::zero ()) == 0 * lv::page);
  MOPPE_CHECK (lv::pages_covering (1 * mp_units::iec::byte) == 1 * lv::page);
  MOPPE_CHECK (lv::pages_covering (lv::page_size) == 1 * lv::page);
  MOPPE_CHECK (lv::pages_covering (lv::page_size + 1 * mp_units::iec::byte) ==
               2 * lv::page);

  const lv::buffer storage = lv::buffer::with_size (100 * mp_units::iec::byte);
  MOPPE_CHECK (storage.size () == 100 * mp_units::iec::byte);
  MOPPE_CHECK (storage.capacity () == lv::bytes_t (1 * lv::page));
}

namespace {
  template <typename A, typename B>
  concept AddableWith = requires (A a, B b) { a + b; };
}

MOPPE_TEST (lavoir_counts_of_different_kinds_stay_severed) {
  // A frame sequence and a pixel extent are both counts, and adding
  // them is a compile error; the static assertion is the test.
  static_assert (!AddableWith<lv::frames_t, lv::pixels_t>);
  static_assert (AddableWith<lv::frames_t, lv::frames_t>);

  // A BGRA row's bytes fall out of the stride: bytes per pixel times
  // pixels is bytes.
  const lv::pixels_t width = 1800 * lv::pixel;
  const lv::bytes_t row = width * lv::bgra_stride;
  MOPPE_CHECK (row == 7200 * mp_units::iec::byte);
}
