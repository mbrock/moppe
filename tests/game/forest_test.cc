#include <moppe/game/forest.hh>
#include <moppe/map/surface.hh>
#include <moppe/terrain/moisture.hh>
#include <moppe/terrain/trail.hh>

#include <tests/test.hh>

#include <algorithm>
#include <vector>

namespace {
  moppe::terrain::MoistureMap
  forest_moisture (const moppe::map::Surface& surface, float value) {
    const auto& domain = surface.domain ();
    return moppe::terrain::MoistureMap (
      domain,
      std::vector<moppe::terrain::SurfaceMoisture> (
        domain.size (),
        value * moppe::terrain::surface_moisture[mp_units::one]));
  }

  moppe::terrain::TrailUseMap
  forest_home_base (const moppe::map::Surface& surface, float value) {
    const auto& domain = surface.domain ();
    return moppe::terrain::TrailUseMap (
      domain,
      std::vector<moppe::terrain::TrailInfluence> (
        domain.size (), 0.0f * moppe::terrain::trail_influence[mp_units::one]),
      std::vector<moppe::terrain::HomeBaseInfluence> (
        domain.size (),
        value * moppe::terrain::home_base_influence[mp_units::one]));
  }
}

MOPPE_TEST (global_forest_sites_are_stable_and_follow_canopy_cover) {
  using namespace moppe;
  map::Surface map (129, 129, Vec3 (640, 180, 640));
  map.fill_elevation (moppe::terrain::surface_elevation_point (
    (0.42f) * 180.0f * mp_units::si::metre));
  map.recompute_normals ();
  map::Surface& surface = map;
  surface.set_moisture (forest_moisture (surface, 0.48f));
  surface.derive_tree_habitat (50.0f * u::m, 160.0f * u::m);
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
  map::Surface map (65, 65, Vec3 (320, 180, 320));
  map.fill_elevation (moppe::terrain::surface_elevation_point (
    (0.42f) * 180.0f * mp_units::si::metre));
  map.recompute_normals ();
  map::Surface& surface = map;
  surface.set_moisture (forest_moisture (surface, 0.48f));
  surface.derive_tree_habitat (50.0f * u::m, 160.0f * u::m);
  surface.set_use (forest_home_base (surface, 1.0f));
  surface.derive_forest_cover (0xfeed1234U);

  MOPPE_CHECK (game::plan_global_forest (surface, 0x31415926U).sites.empty ());
}
