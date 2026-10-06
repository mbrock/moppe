#ifndef MOPPE_GAME_GLIDER_RENDER_HH
#define MOPPE_GAME_GLIDER_RENDER_HH

#include <moppe/game/frame_view.hh>
#include <moppe/render/draw.hh>
#include <moppe/render/renderer.hh>

namespace moppe::game {
  // The hang glider (models/glider.blend) as baked meshes drawn through
  // the renderer, with the hiker flying it prone in the harness, skinned
  // into the frame's draw list.
  void render_glider (render::Renderer& r,
                      render::DrawList& dl,
                      const GliderPose& glider,
                      float time,
                      uint64_t motion_base);
}

#endif
