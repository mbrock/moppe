#ifndef MOPPE_GAME_SEED_MEMORY_HH
#define MOPPE_GAME_SEED_MEMORY_HH

#include <moppe/game/world.hh>
#include <moppe/terrain/world_recipe.hh>

namespace moppe::game {
  // An ordinary launch returns to the world you last played rather than a new
  // one. The memory is stable across builds and keyed by generation profile
  // and resolution; the finished-world cache validates the complete recipe.
  void remember_seed (const WorldParams& world,
                      terrain::TerrainGenerationProfile profile,
                      int seed);

  // Falls back to the clock when nothing is remembered for this key.
  int remembered_seed (const WorldParams& world,
                       terrain::TerrainGenerationProfile profile);

  // Drop obsolete build-keyed terrain, seed, and finished-world caches left by
  // older versions. Stable default and named finished-world caches survive.
  void prune_obsolete_terrain_caches ();
}

#endif
