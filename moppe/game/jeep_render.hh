#ifndef MOPPE_GAME_JEEP_RENDER_HH
#define MOPPE_GAME_JEEP_RENDER_HH

#include <moppe/game/car_render.hh>

namespace moppe::game {
  // The jeep (models/jeep.blend) as baked meshes drawn through the
  // renderer: the body, each wheel on its spring and the front pair
  // turned with the steering, and the steering wheel turned about its
  // column. Its riders are the hiker seated, the driver's hands on the
  // wheel, skinned into the frame's draw list.
  void render_jeep (render::Renderer& r,
                    render::DrawList& dl,
                    const CarPose& jeep,
                    JeepRiders riders,
                    uint64_t motion_base);
}

#endif
