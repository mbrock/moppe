#ifndef MOPPE_GAME_SEED_MEMORY_HH
#define MOPPE_GAME_SEED_MEMORY_HH

#include <moppe/game/world.hh>
#include <moppe/terrain/world_recipe.hh>

namespace moppe::game {
  // An ordinary launch returns to the world you last played rather than a new
  // one.  The memory is keyed by build, generation profile, and resolution,
  // since a seed only reproduces its world under the same three.
  void remember_seed (const WorldParams& world,
                      terrain::TerrainGenerationProfile profile,
                      int seed);

  // Falls back to the clock when nothing is remembered for this key.
  int remembered_seed (const WorldParams& world,
                       terrain::TerrainGenerationProfile profile);

  // Cached terrain from earlier builds cannot be trusted to match the current
  // generator, and it is large.  Drop it rather than let the cache grow one
  // world per build.
  void prune_obsolete_terrain_caches ();
}

#endif
