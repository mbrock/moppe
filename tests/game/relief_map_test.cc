#include <moppe/game/generated_world.hh>
#include <moppe/game/relief_map.hh>
#include <moppe/map/surface.hh>
#include <moppe/terrain/world_recipe.hh>

#include <tests/test.hh>

#include <cstdint>

using namespace moppe;

namespace {
  // A small finished world's sheet, painted as the game paints it.
  game::ReliefMap small_sheet (int size) {
    const terrain::WorldRecipe recipe = terrain::make_world_recipe (
      spatial_extent_in_metres (Vec3 (1200.0f, 320.0f, 1200.0f)),
      65,
      terrain::Seed { 123 },
      50.0f * u::m,
      terrain::TerrainGenerationProfile::Smoke);
    map::SurfaceGeometry surface (terrain::TerrainDomain (
      recipe.resolution (), recipe.resolution (), recipe.extent ()));
    const auto uplift =
      map::initialize_terrain (surface, recipe.seed (), recipe.water_datum ());
    map::evolve_terrain (surface, uplift, recipe.evolution ());
    (void)map::form_river_channels (surface, recipe.water_datum ());
    (void)map::form_terrain_trails (surface, recipe.trail_formation ());
    const terrain::TrailNetwork trails =
      terrain::analyze_trail_network (surface, recipe.trail_formation ());
    map::rebuild_geometry (surface);
    const game::Hydrology hydrology = game::analyze_hydrology (surface, recipe);
    const auto [water, readings] =
      game::analyze_surface (surface, recipe, hydrology, trails.use);
    return game::paint_relief_map (surface,
                                   readings,
                                   water,
                                   std::get<terrain::RiverNetwork> (hydrology),
                                   trails,
                                   terrain::RoadNetwork {},
                                   recipe.water_datum (),
                                   size);
  }
}

MOPPE_TEST (relief_map_is_an_opaque_square_of_the_whole_world) {
  const game::ReliefMap sheet = small_sheet (96);
  MOPPE_CHECK (sheet.size == 96);
  MOPPE_CHECK (sheet.rgba.size () == 96u * 96u * 4u);
  MOPPE_CHECK (sheet.period_x_m == 1200.0f);
  MOPPE_CHECK (sheet.period_z_m == 1200.0f);
  bool opaque = true;
  for (std::size_t pixel = 0; pixel < 96u * 96u; ++pixel)
    opaque = opaque && sheet.rgba[pixel * 4 + 3] == 255;
  MOPPE_CHECK (opaque);
}

MOPPE_TEST (relief_map_marks_tops_above_the_sea_inside_the_world) {
  const game::ReliefMap sheet = small_sheet (64);
  MOPPE_CHECK (sheet.highest_m > 0.0f);
  for (const game::ReliefMap::Summit& top : sheet.summits) {
    MOPPE_CHECK (top.height_m >= 30.0f);
    MOPPE_CHECK (top.height_m <= sheet.highest_m);
    MOPPE_CHECK (top.x_m >= 0.0f && top.x_m < sheet.period_x_m);
    MOPPE_CHECK (top.z_m >= 0.0f && top.z_m < sheet.period_z_m);
  }
}

// The picture is the same however many threads paint it.
MOPPE_TEST (relief_map_paints_the_same_sheet_twice) {
  MOPPE_CHECK (small_sheet (64).rgba == small_sheet (64).rgba);
}
