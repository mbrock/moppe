#ifndef MOPPE_GAME_GLIDER_RENDER_HH
#define MOPPE_GAME_GLIDER_RENDER_HH

#include <moppe/game/frame_view.hh>
#include <moppe/render/draw.hh>

namespace moppe::game {
  void
  render_glider (render::DrawList& dl, const GliderPose& glider, float time);
}

#endif
