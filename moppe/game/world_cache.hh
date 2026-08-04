#ifndef MOPPE_GAME_WORLD_CACHE_HH
#define MOPPE_GAME_WORLD_CACHE_HH

#include <moppe/game/generated_world.hh>

#include <memory>
#include <string>
#include <string_view>

namespace moppe::game {
  enum class WorldCacheMode { Reuse, Refresh, Disabled };

  // The default namespace follows the linked executable, so changing any
  // world-building code gets a fresh cache automatically. A named namespace
  // deliberately omits that build identity for iterative development.
  struct WorldCacheConfig {
    WorldCacheMode mode = WorldCacheMode::Reuse;
    std::string key;
  };

  std::string world_cache_name (const terrain::WorldRecipe& recipe,
                                const WorldCacheConfig& config,
                                std::string_view build_identity);

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
