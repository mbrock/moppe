#ifndef MOPPE_GAME_HELICOPTER_RENDER_HH
#define MOPPE_GAME_HELICOPTER_RENDER_HH

#include <moppe/game/frame_view.hh>
#include <moppe/render/renderer.hh>

namespace moppe::game {
  // The helicopter (models/helicopter.blend) as baked meshes drawn
  // through the renderer: the body, the main rotor turned about its mast,
  // and the tail rotor about its axle. Its pilot sits behind tinted glass
  // and is not drawn.
  void render_helicopter (render::Renderer& r,
                          const HelicopterPose& helicopter,
                          uint64_t motion_base);
}

#endif
