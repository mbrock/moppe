#ifndef MOPPE_GAME_LAND_HH
#define MOPPE_GAME_LAND_HH

#include <moppe/map/surface.hh>
#include <moppe/terrain/world_recipe.hh>

#include <string>

namespace moppe::game {
  // A world's land: the surface geology, erosion, and the trails' earthworks
  // leave behind -- elevation, sediment, and the eroded and deposited
  // material -- which is all the minutes of generation produce. Everything
  // else in a world (normals, water, rivers, readings, forest) follows from
  // it in seconds. A seed makes the same land on every platform
  // (docs/determinism.md), so a land file is a cache anyone may share: one
  // packaged with the game, downloaded (tools/fetch-land), or saved after
  // generating are interchangeable.
  //
  // The version counts changes to what land a recipe makes. Bump it with
  // the pinned hash in tests/game/land_test.cc whenever the geology,
  // erosion, or trail formation change their output.
  inline constexpr int LAND_VERSION = 1;

  using Land = spatial::Bundle<terrain::TerrainDomain,
                               terrain::SurfaceElevation,
                               terrain::SedimentThickness,
                               map::ErodedSurfaceMaterial,
                               map::DepositedSurfaceMaterial>;

  // The file a recipe's land is kept in: its version, profile, resolution,
  // and seed, and a hash of the rest of the recipe, as
  // land-v1-play-2048-123-0123abcd.arrows.
  std::string land_file_name (const terrain::WorldRecipe& recipe);

  Land extract_land (const map::SurfaceGeometry& surface);

  // Fills the surface's land columns from a file over its own domain;
  // normals and snow support are left for map::rebuild_geometry. False,
  // with the surface untouched, when there is no such file.
  bool try_load_land (map::SurfaceGeometry& surface, const std::string& path);

  // Writes through a temporary name, so a reader never sees half a file.
  void save_land (const map::SurfaceGeometry& surface, const std::string& path);
}

#endif
