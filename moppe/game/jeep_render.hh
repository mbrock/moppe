#ifndef MOPPE_GAME_JEEP_RENDER_HH
#define MOPPE_GAME_JEEP_RENDER_HH

#include <moppe/game/frame_view.hh>
#include <moppe/game/outfit.hh>
#include <moppe/render/draw.hh>
#include <moppe/render/renderer.hh>

namespace moppe::game {
  // Who is seen sitting in a jeep, and what each wears.
  struct JeepRiders {
    bool driver = false;
    bool passenger = false;
    Outfit driver_outfit = Outfit::blue_jacket;
    Outfit passenger_outfit = Outfit::blue_jacket;
  };

  // The jeep (models/jeep.blend) as baked meshes drawn through the
  // renderer: the body, each wheel on its spring and the front pair
  // turned with the steering, and the steering wheel turned about its
  // column. Its riders are the hiker seated, the driver's hands on the
  // wheel, skinned into the frame's draw list.
  void render_jeep (render::Renderer& r,
                    render::DrawList& dl,
                    const JeepPose& jeep,
                    JeepRiders riders,
                    uint64_t motion_base);
}

#endif
