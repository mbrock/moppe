#ifndef MOPPE_GAME_LAUNCH_OPTIONS_HH
#define MOPPE_GAME_LAUNCH_OPTIONS_HH

#include <moppe/game/graphics_settings.hh>
#include <moppe/game/world.hh>
#include <moppe/game/world_cache.hh>
#include <moppe/mov/vehicle.hh>
#include <moppe/platform/platform.hh>
#include <moppe/terrain/world_recipe.hh>

#include <optional>
#include <string>
#include <string_view>

namespace moppe::game {
  // Everything one launch of the game is configured with.  This is the whole
  // command line resolved into values: no argv survives past parsing, and no
  // platform service is consulted, so the derived defaults below stay
  // testable.
  struct LaunchOptions {
    WorldParams world;
    GraphicsSettings graphics = default_graphics_settings ();
    platform::Config config { .title = "Moppe", .fullscreen = true };
    terrain::TerrainGenerationProfile generation_profile =
      terrain::TerrainGenerationProfile::Play;
    std::optional<int> terrain_resolution;
    // How far the world runs before it comes round again, when not the
    // usual five kilometres.
    std::optional<meters_t> world_size;
    WorldCacheConfig world_cache;
    std::string screenshot_path;
    // Keeps a hand-started run behind the active application, the way
    // captures already stay out of the way.
    bool stay_inactive = false;
    // Play with someone else in the same world (game/companion.hh): the
    // other machine's address, or empty to find them on the local
    // network. Both must launch the same world.
    std::optional<std::string> multiplayer;
    // Negative until the launch either names a seed or recalls a remembered
    // one; a capture always pins its own so comparisons stay reproducible.
    int seed = -1;
    bool show_help = false;
  };

  // Parses argv over already-initialized options, so a platform may install
  // its own graphics defaults first.  Unrecognized arguments are ignored.
  // Returns false with a human-readable error for a malformed command line.
  bool parse_launch_options (int argc,
                             char** argv,
                             LaunchOptions& options,
                             std::string& error);

  // Generated from the parser's flag registry so accepted options and their
  // help text remain one definition.
  std::string launch_options_help (std::string_view program_name);

  // Requires a resolved (non-negative) seed.
  terrain::WorldRecipe make_launch_recipe (const LaunchOptions& options);
}

#endif
