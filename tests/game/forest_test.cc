#include <moppe/game/forest.hh>
#include <moppe/map/generate.hh>

#include <tests/test.hh>

#include <algorithm>
#include <vector>

MOPPE_TEST (global_forest_sites_are_stable_and_follow_canopy_cover) {
  using namespace moppe;
  map::RandomHeightMap map (
    129, 129, Vec3 (640, 180, 640), 43, terrain::Topology::Bounded);
  std::fill (map.raw_heights (), map.raw_heights () + 129 * 129, 0.42f);
  map.recompute_normals ();
  map::Surface surface (map);
  surface.derive_tree_habitat (
    std::vector<float> (129 * 129, 0.48f), 50.0f * u::m, 160.0f * u::m);
  surface.derive_forest_cover (0xdecafbadU);

  const game::ForestPlan first =
    game::plan_global_forest (surface, 0xa511e9b3U);
  const game::ForestPlan second =
    game::plan_global_forest (surface, 0xa511e9b3U);
  MOPPE_CHECK (first.sites.size () > 100);
  MOPPE_CHECK (first.sites.size () == second.sites.size ());
  for (std::size_t index = 0; index < first.sites.size (); ++index) {
    const game::ForestSite& site = first.sites[index];
    MOPPE_CHECK (site.seed == second.sites[index].seed);
    MOPPE_CHECK_NEAR (site.position[0], second.sites[index].position[0], 1e-6f);
    MOPPE_CHECK_NEAR (site.position[2], second.sites[index].position[2], 1e-6f);
    MOPPE_CHECK (site.cover >= 0.06f);
    MOPPE_CHECK (site.normal[1] > 0.99f);
  }
}

MOPPE_TEST (global_forest_sites_leave_materialized_clearings_empty) {
  using namespace moppe;
  map::RandomHeightMap map (
    65, 65, Vec3 (320, 180, 320), 47, terrain::Topology::Bounded);
  std::fill (map.raw_heights (), map.raw_heights () + 65 * 65, 0.42f);
  map.recompute_normals ();
  map::Surface surface (map);
  surface.derive_tree_habitat (
    std::vector<float> (65 * 65, 0.48f), 50.0f * u::m, 160.0f * u::m);
  surface.materialize_home_base_influence (std::vector<float> (65 * 65, 1.0f));
  surface.derive_forest_cover (0xfeed1234U);

  MOPPE_CHECK (game::plan_global_forest (surface, 0x31415926U).sites.empty ());
}
