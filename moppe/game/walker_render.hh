#ifndef MOPPE_GAME_WALKER_RENDER_HH
#define MOPPE_GAME_WALKER_RENDER_HH

#include <moppe/game/frame_view.hh>
#include <moppe/render/draw.hh>

namespace moppe::game {
  // The walking figure reads the frozen presentation pose rather than the
  // mutable walker simulation object.
  void render_walker (render::DrawList& draw,
                      const WalkerPose& walker,
                      float time,
                      const Vec3& visual_scale = Vec3 (1, 1, 1));
}

#endif
