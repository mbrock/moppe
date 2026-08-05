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
