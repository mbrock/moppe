#include <lavoir/buffer.hh>
#include <lavoir/bundle.hh>

#include <tests/test.hh>

#include <cstdint>
#include <numeric>
#include <utility>

namespace lv = lavoir;

MOPPE_TEST (lavoir_buffers_are_page_aligned_and_page_rounded) {
  const lv::buffer storage = lv::buffer::with_size (100);
  MOPPE_CHECK (storage.size () == 100);
  MOPPE_CHECK (storage.capacity () == lv::page_size);
  MOPPE_CHECK (reinterpret_cast<std::uintptr_t> (storage.lease ().data ()) %
                 lv::page_size ==
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
