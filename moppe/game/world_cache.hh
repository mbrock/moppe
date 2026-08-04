#ifndef MOPPE_GAME_WORLD_CACHE_HH
#define MOPPE_GAME_WORLD_CACHE_HH

#include <moppe/game/generated_world.hh>

#include <memory>
#include <string>

namespace moppe::game {
  // A finished-world cache is a directory of typed Arrow fields plus compact
  // topology. It contains every renderer-free artifact GeneratedWorld owns.
  std::unique_ptr<GeneratedWorld>
  try_load_world_cache (WorldParams params,
                        terrain::WorldRecipe recipe,
                        const std::string& directory);

  void save_world_cache (const GeneratedWorld& world,
                         const std::string& directory);
}

#endif
