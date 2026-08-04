#include <moppe/game/seed_memory.hh>

#include <moppe/platform/platform.hh>

#include <ctime>
#include <filesystem>
#include <fstream>
#include <sstream>
#include <string>
#include <system_error>

namespace moppe::game {
  namespace {
    std::string last_seed_path (const WorldParams& world,
                                terrain::TerrainGenerationProfile profile) {
      std::ostringstream name;
      name << "last-seed-" << platform::executable_build_id () << '-'
           << terrain::profile_id (profile) << '-' << world.resolution
           << ".txt";
      return platform::cache_path (name.str ());
    }
  }

  void remember_seed (const WorldParams& world,
                      terrain::TerrainGenerationProfile profile,
                      int seed) {
    std::ofstream output (last_seed_path (world, profile));
    if (output)
      output << seed << '\n';
  }

  int remembered_seed (const WorldParams& world,
                       terrain::TerrainGenerationProfile profile) {
    std::ifstream input (last_seed_path (world, profile));
    int seed = -1;
    if (input >> seed && seed >= 0)
      return seed;
    return static_cast<int> (::time (0));
  }

  void prune_obsolete_terrain_caches () {
    const std::string build_id = platform::executable_build_id ();
    std::error_code error;
    const std::filesystem::path root (platform::cache_path (""));
    for (const std::filesystem::directory_entry& entry :
         std::filesystem::directory_iterator (root, error)) {
      if (error)
        continue;
      const std::string name = entry.path ().filename ().string ();
      const bool terrain_file =
        entry.is_regular_file () &&
        (name.starts_with ("terrain-") || name.starts_with ("last-seed-"));
      const bool automatic_world = entry.is_directory () &&
                                   name.starts_with ("world-") &&
                                   !name.starts_with ("world-key-");
      if (terrain_file && name.find (build_id) == std::string::npos)
        std::filesystem::remove (entry.path (), error);
      else if (automatic_world && name.find (build_id) == std::string::npos)
        std::filesystem::remove_all (entry.path (), error);
    }
  }
}
