#ifndef MOPPE_GAME_WORLD_CACHE_HH
#define MOPPE_GAME_WORLD_CACHE_HH

#include <moppe/game/generated_world.hh>

#include <cstdint>
#include <memory>
#include <optional>
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

  // Which world a cache directory holds, read from its header alone. A
  // package that ships a baked world lets this choose the default recipe;
  // the full recipe is still validated when the world loads.
  struct WorldCacheIdentity {
    int resolution = 0;
    std::uint32_t seed = 0;
    terrain::TerrainGenerationProfile profile {};
  };

  std::optional<WorldCacheIdentity>
  read_world_cache_identity (const std::string& directory);
}

#endif
