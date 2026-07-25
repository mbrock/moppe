#ifndef MOPPE_GAME_MOPPE_GAME_HH
#define MOPPE_GAME_MOPPE_GAME_HH

#include <moppe/game/launch_options.hh>
#include <moppe/platform/platform.hh>
#include <moppe/terrain/world_recipe.hh>

#include <memory>

namespace moppe::game {
  // The application shell: it owns the generated world, the session on it, and
  // the frame.  A factory rather than a class, so the shell itself stays
  // private to its translation unit and every host reaches it the same way.
  std::unique_ptr<platform::Game> make_moppe_game (const LaunchOptions& options,
                                                   terrain::WorldRecipe recipe);
}

#endif
