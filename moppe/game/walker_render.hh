#ifndef MOPPE_GAME_WALKER_RENDER_HH
#define MOPPE_GAME_WALKER_RENDER_HH

#include <moppe/game/frame_view.hh>
#include <moppe/game/mushrooms.hh>
#include <moppe/render/draw.hh>

#include <span>

namespace moppe::game {
  // The walking figure reads the frozen presentation pose rather than the
  // mutable walker simulation object. It carries a wicker basket in its
  // left hand holding `basket`, the mushrooms gathered so far, as they
  // would lie in it.
  void render_walker (render::DrawList& draw,
                      const WalkerPose& walker,
                      float time,
                      std::span<const MushroomSite> basket = {});

  // The basket alone, where the walker's hand holds it: what a walker
  // seeing through their own eyes finds when looking down.
  void render_basket (render::DrawList& draw,
                      const WalkerPose& walker,
                      float time,
                      std::span<const MushroomSite> basket);
}

#endif
