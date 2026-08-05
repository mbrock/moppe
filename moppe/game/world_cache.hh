#ifndef MOPPE_GAME_WORLD_CACHE_HH
#define MOPPE_GAME_WORLD_CACHE_HH

#include <moppe/game/generated_world.hh>

#include <memory>
#include <string>

namespace moppe::game {
  enum class WorldCacheMode { Reuse, Refresh, Disabled };

  // The default namespace is stable across executable builds. The complete
  // recipe still selects the world, and the stored schema and recipe are
  // validated before reuse. A named namespace lets a developer keep an
  // additional independently refreshable world.
  struct WorldCacheConfig {
    WorldCacheMode mode = WorldCacheMode::Reuse;
    std::string key;
  };

  std::string world_cache_name (const terrain::WorldRecipe& recipe,
                                const WorldCacheConfig& config);

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
