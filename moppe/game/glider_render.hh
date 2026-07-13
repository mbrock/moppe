#ifndef MOPPE_GAME_GLIDER_RENDER_HH
#define MOPPE_GAME_GLIDER_RENDER_HH

#include <moppe/mov/glider.hh>
#include <moppe/render/draw.hh>

namespace moppe::game {
  void render_glider (render::DrawList& dl,
                      const mov::Glider& glider,
                      float time,
                      const Vec3& visual_scale = Vec3 (1, 1, 1));
}

#endif
