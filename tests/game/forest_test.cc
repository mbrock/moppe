#include <moppe/game/forest.hh>
#include <moppe/map/surface.hh>

#include <tests/surface_fixture.hh>
#include <tests/test.hh>

#include <algorithm>
#include <vector>

MOPPE_TEST (global_forest_sites_are_stable_and_follow_canopy_cover) {
  using namespace moppe;
  map::Surface map (129, 129, Vec3 (640, 180, 640));
  map.fill_elevation (moppe::terrain::surface_elevation_point (
    (0.42f) * 180.0f * mp_units::si::metre));
  map.recompute_normals ();
  map::Surface& surface = map;
  const map::SurfaceReadings readings = test::complete_readings (
    surface,
    { .moisture = test::uniform_moisture (surface.domain (), 0.48f),
      .seed = 0xdecafbadU });

  const game::ForestPlan first =
    game::plan_global_forest (surface, readings, 0xa511e9b3U);
  const game::ForestPlan second =
    game::plan_global_forest (surface, readings, 0xa511e9b3U);
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
  const map::SurfaceReadings readings = test::complete_readings (
    surface,
    { .moisture = test::uniform_moisture (surface.domain (), 0.48f),
      .use = test::uniform_use (surface.domain (), 0.0f, 1.0f),
      .seed = 0xfeed1234U });

  MOPPE_CHECK (
    game::plan_global_forest (surface, readings, 0x31415926U).sites.empty ());
}
