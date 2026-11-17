#include <moppe/terrain/domain.hh>

#include <moppe/spatial/bundle_operations.hh>

#include <tests/test.hh>

#include <cstddef>
#include <set>
#include <utility>

using namespace moppe;

MOPPE_TEST (sites_visit_every_lattice_position_exactly_once) {
  const terrain::TerrainDomain domain (4, 3);
  std::set<std::pair<std::size_t, std::size_t>> visited;
  std::size_t count = 0;
  for (const terrain::TerrainIndex site : spatial::sites (domain)) {
    visited.insert ({ site.column, site.row });
    ++count;
  }
  MOPPE_CHECK (count == domain.size ());
  MOPPE_CHECK (visited.size () == domain.size ());
}

MOPPE_TEST (a_step_off_the_lattice_edge_wraps_around_the_torus) {
  const terrain::TerrainDomain domain (4, 3);
  const terrain::TerrainIndex origin { 0, 0 };
  MOPPE_CHECK (domain.shifted (origin, -1, 0) ==
               (terrain::TerrainIndex { 3, 0 }));
  MOPPE_CHECK (domain.shifted (origin, 0, -1) ==
               (terrain::TerrainIndex { 0, 2 }));
  MOPPE_CHECK (domain.shifted (terrain::TerrainIndex { 3, 2 }, 1, 1) ==
               (terrain::TerrainIndex { 0, 0 }));
  // A whole lap along either axis returns to where it started.
  MOPPE_CHECK (domain.shifted (origin, 4, 3) == origin);
  MOPPE_CHECK (domain.shifted (origin, -4, -3) == origin);
}

MOPPE_TEST (every_site_shifted_by_nothing_is_itself) {
  const terrain::TerrainDomain domain (5, 4);
  for (const terrain::TerrainIndex site : spatial::sites (domain))
    MOPPE_CHECK (domain.shifted (site, 0, 0) == site);
}

MOPPE_TEST (continuous_coordinates_rounding_to_a_full_lap_wrap_to_zero) {
  MOPPE_CHECK_NEAR (terrain::wrap_coordinate (-0.00001f, 2048.0f), 0.0f, 0.0f);
}

namespace {
  template <typename A, typename B>
  concept addable = requires (A a, B b) { a + b; };
  template <typename To, typename... From>
  concept composes = requires (From... from) { To { from... }; };
}

// The plan's lanes are distinct kinds: x and z offsets never mix, and a
// displacement does not build from its axes in the wrong order, nor does a
// 3-D displacement move a plan point.
static_assert (!addable<x_displacement_t, z_displacement_t>);
static_assert (
  composes<plan_displacement_t, x_displacement_t, z_displacement_t>);
static_assert (
  !composes<plan_displacement_t, z_displacement_t, x_displacement_t>);
static_assert (addable<plan_position_t, plan_displacement_t>);
static_assert (!addable<plan_position_t, displacement_t>);

MOPPE_TEST (typed_torus_offsets_cross_each_seam_by_its_own_period) {
  // Unequal periods, so a swapped lane gives a different answer.
  const plan_extent_t period = plan_extent (1000.0f * u::m, 2000.0f * u::m);
  const plan_displacement_t way = terrain::torus_offset (
    plan_position (995.0f, 1990.0f), plan_position (5.0f, 10.0f), period);
  MOPPE_CHECK (plan_value (way) == Vec2 (10.0f, 20.0f));
  const auto [across, down] = way;
  MOPPE_CHECK (across == 10.0f * x_displacement[u::m]);
  MOPPE_CHECK (down == 20.0f * z_displacement[u::m]);

  const plan_position_t moved =
    terrain::wrap (plan_position (995.0f, 1990.0f) + way, period);
  MOPPE_CHECK (plan_value (moved) == Vec2 (5.0f, 10.0f));

  // A point in space comes to the copy nearest the reference in plan and
  // keeps its height.
  const position_t near =
    terrain::nearest_image (position (Vec3 (10.0f, 42.0f, 30.0f)),
                            position (Vec3 (990.0f, 0.0f, 1980.0f)),
                            period);
  MOPPE_CHECK (position_value (near) == Vec3 (1010.0f, 42.0f, 2030.0f));
}

MOPPE_TEST (library_rounded_trig_matches_the_standard_library) {
  // libm:: keeps std::'s bits, so presentation code can be typed without
  // moving a vertex.
  for (const float a : { 0.1f, 1.234f, 2.5f, -3.0f, 100.0f }) {
    MOPPE_CHECK (libm::sin (a * u::rad) == std::sin (a));
    MOPPE_CHECK (libm::cos (a * u::rad) == std::cos (a));
    MOPPE_CHECK (radians_value (libm::atan2 (a * u::m, 0.7f * u::m)) ==
                 std::atan2 (a, 0.7f));
    MOPPE_CHECK (libm::hypot (a * u::m, 0.7f * u::m) ==
                 std::hypot (a, 0.7f) * u::m);
  }
}
