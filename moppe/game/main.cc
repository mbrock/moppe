// The entry point every Moppe host shares: resolve the command line, settle
// on a seed, and hand the resulting recipe to the application shell.  It is
// deliberately the only file compiled per executable, so a platform's own
// defaults are the one thing that varies here.

#include <moppe/game/launch_options.hh>
#include <moppe/game/moppe_game.hh>
#include <moppe/game/seed_memory.hh>
#include <moppe/platform/platform.hh>
#include <moppe/profile.hh>

#include <exception>
#include <iostream>
#include <memory>
#include <string>

int main (int argc, char** argv) {
  using namespace moppe;
  if (::getenv ("MOPPE_TRACY_WAIT"))
    MOPPE_PROFILE_WAIT ();
  MOPPE_PROFILE_THREAD ("Main");
  MOPPE_PROFILE_ZONE ("main");

  game::LaunchOptions options;
#ifdef MOPPE_DEFAULT_APPLE_TV_GRAPHICS
  options.graphics = game::apple_tv_graphics_settings ();
#endif

  std::string error;
  if (!game::parse_launch_options (argc, argv, options, error)) {
    std::cerr << error << '\n';
    return -1;
  }
  if (!game::apply_graphics_environment (options.graphics, error)) {
    std::cerr << error << '\n';
    return -1;
  }
  game::print_graphics_settings (std::cerr, options.graphics);
  if (options.benchmark)
    game::publish_benchmark_environment (*options.benchmark);

  game::prune_obsolete_terrain_caches ();
  if (options.seed < 0)
    options.seed =
      game::remembered_seed (options.world, options.generation_profile);

  std::unique_ptr<platform::Game> game =
    game::make_moppe_game (options, game::make_launch_recipe (options));

  try {
    return platform::run (*game, options.config);
  } catch (const std::exception& e) {
    std::cerr << "\nError: " << e.what () << "\n";
    return -1;
  }
}
