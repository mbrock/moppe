#include <moppe/game/tree_stand.hh>
#include <moppe/map/generate.hh>

#include <tests/test.hh>

#include <algorithm>
#include <vector>

MOPPE_TEST (tree_grove_is_selected_from_materialized_surface_habitat) {
  using namespace moppe;
  map::RandomHeightMap map (
    65, 65, Vec3 (320, 180, 320), 17, terrain::Topology::Bounded);
  std::fill (map.raw_heights (), map.raw_heights () + 65 * 65, 0.42f);
  map.recompute_normals ();
  map::Surface surface (map);
  surface.derive_tree_habitat (
    std::vector<float> (65 * 65, 0.48f), 50.0f * u::m, 160.0f * u::m);
  surface.derive_forest_cover (1234);

  const game::TreeGrove grove = game::plan_tree_grove (surface, 1234, 9);
  MOPPE_CHECK (grove.sites.size () == 9);
  for (const game::TreeSite& site : grove.sites) {
    const position_t at =
      position (Vec3 (site.position[0], 0, site.position[2]));
    const float elevation =
      surface.elevation_at (at).quantity_from_zero ().numerical_value_in (u::m);
    MOPPE_CHECK_NEAR (site.position[1], elevation, 1e-4f);
    MOPPE_CHECK (site.habitat >= 0.34f);
    MOPPE_CHECK (site.normal[1] > 0.99f);
  }
}

MOPPE_TEST (tree_grove_plan_is_reproducible_but_organisms_are_unique) {
  using namespace moppe;
  map::RandomHeightMap map (
    65, 65, Vec3 (320, 180, 320), 19, terrain::Topology::Bounded);
  std::fill (map.raw_heights (), map.raw_heights () + 65 * 65, 0.42f);
  map.recompute_normals ();
  map::Surface surface (map);
  surface.derive_tree_habitat (
    std::vector<float> (65 * 65, 0.48f), 50.0f * u::m, 160.0f * u::m);
  surface.derive_forest_cover (4567);

  const game::TreeGrove first = game::plan_tree_grove (surface, 4567, 7);
  const game::TreeGrove second = game::plan_tree_grove (surface, 4567, 7);
  MOPPE_CHECK (first.sites.size () == second.sites.size ());
  for (std::size_t index = 0; index < first.sites.size (); ++index) {
    MOPPE_CHECK (first.sites[index].seed == second.sites[index].seed);
    MOPPE_CHECK_NEAR (
      first.sites[index].position[0], second.sites[index].position[0], 1e-6f);
    MOPPE_CHECK_NEAR (
      first.sites[index].position[2], second.sites[index].position[2], 1e-6f);
    for (std::size_t other = 0; other < index; ++other)
      MOPPE_CHECK (first.sites[index].seed != first.sites[other].seed);
  }
}

MOPPE_TEST (forest_recruitment_keeps_canopy_young_trees_and_saplings) {
  using namespace moppe;
  map::RandomHeightMap map (
    129, 129, Vec3 (640, 180, 640), 29, terrain::Topology::Bounded);
  std::fill (map.raw_heights (), map.raw_heights () + 129 * 129, 0.42f);
  map.recompute_normals ();
  map::Surface surface (map);
  surface.derive_tree_habitat (
    std::vector<float> (129 * 129, 0.48f), 50.0f * u::m, 160.0f * u::m);
  surface.derive_forest_cover (6789);

  const game::TreeGrove forest =
    game::plan_tree_grove (surface, 6789, 24, Vec3 (320, 0, 320));
  MOPPE_CHECK (forest.sites.size () == 24);
  for (const game::TreeCohort cohort : { game::TreeCohort::sapling,
                                         game::TreeCohort::young,
                                         game::TreeCohort::canopy })
    MOPPE_CHECK (
      std::ranges::any_of (forest.sites, [cohort] (const game::TreeSite& site) {
        return site.cohort == cohort;
      }));
  MOPPE_CHECK (
    std::ranges::any_of (forest.sites, [] (const game::TreeSite& site) {
      const float dx = site.position[0] - 320.0f;
      const float dz = site.position[2] - 320.0f;
      return dx * dx + dz * dz < 150.0f * 150.0f;
    }));
}

MOPPE_TEST (tree_grove_refuses_a_surface_without_viable_habitat) {
  using namespace moppe;
  map::RandomHeightMap map (
    33, 33, Vec3 (160, 180, 160), 23, terrain::Topology::Bounded);
  std::fill (map.raw_heights (), map.raw_heights () + 33 * 33, 0.42f);
  map.recompute_normals ();
  map::Surface surface (map);
  surface.derive_tree_habitat (
    std::vector<float> (33 * 33, 1.0f), 50.0f * u::m, 160.0f * u::m);
  surface.derive_forest_cover (8910);

  MOPPE_CHECK (game::plan_tree_grove (surface, 8910, 4).sites.empty ());
}
