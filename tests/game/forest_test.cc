#include <moppe/game/forest.hh>
#include <moppe/map/surface.hh>

#include <tests/surface_fixture.hh>
#include <tests/test.hh>

#include <algorithm>
#include <concepts>
#include <vector>

static_assert (std::same_as<decltype (moppe::game::ForestSite {}.position),
                            moppe::position_t>);
static_assert (std::same_as<decltype (moppe::game::ForestSite {}.normal),
                            moppe::terrain::TerrainNormal>);
static_assert (std::same_as<decltype (moppe::game::ForestSite {}.cover),
                            moppe::map::ForestCover>);
static_assert (std::same_as<decltype (moppe::game::ForestSite {}.size),
                            moppe::game::TreeSizeFactor>);
static_assert (std::same_as<decltype (moppe::game::ForestPlan {}.period),
                            moppe::spatial_extent_t>);

MOPPE_TEST (global_forest_sites_are_stable_and_follow_canopy_cover) {
  using namespace moppe;
  map::SurfaceGeometry surface = map::SurfaceGeometry (terrain::TerrainDomain (
    129, 129, spatial_extent_in_metres (Vec3 (640, 0, 640))));
  std::ranges::fill (spatial::get<terrain::surface_elevation> (surface),
                     moppe::terrain::surface_elevation_point (
                       (0.42f) * 180.0f * mp_units::si::metre));
  map::rebuild_geometry (surface);
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
    MOPPE_CHECK_NEAR (position_value (site.position)[0],
                      position_value (second.sites[index].position)[0],
                      1e-6f);
    MOPPE_CHECK_NEAR (position_value (site.position)[2],
                      position_value (second.sites[index].position)[2],
                      1e-6f);
    MOPPE_CHECK (site.cover >= 0.06f * map::forest_cover[mp_units::one]);
    MOPPE_CHECK (site.normal.numerical_value_in (mp_units::one)[1] > 0.99f);
  }
}

MOPPE_TEST (global_forest_sites_leave_materialized_clearings_empty) {
  using namespace moppe;
  map::SurfaceGeometry surface = map::SurfaceGeometry (terrain::TerrainDomain (
    65, 65, spatial_extent_in_metres (Vec3 (320, 0, 320))));
  std::ranges::fill (spatial::get<terrain::surface_elevation> (surface),
                     moppe::terrain::surface_elevation_point (
                       (0.42f) * 180.0f * mp_units::si::metre));
  map::rebuild_geometry (surface);
  const map::SurfaceReadings readings = test::complete_readings (
    surface,
    { .moisture = test::uniform_moisture (surface.domain (), 0.48f),
      .use = test::uniform_use (surface.domain (), 0.0f, 1.0f),
      .seed = 0xfeed1234U });

  MOPPE_CHECK (
    game::plan_global_forest (surface, readings, 0x31415926U).sites.empty ());
}
