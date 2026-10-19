#ifndef MOPPE_GAME_CADDY_RENDER_HH
#define MOPPE_GAME_CADDY_RENDER_HH

#include <moppe/game/frame_view.hh>
#include <moppe/game/jeep_render.hh>
#include <moppe/render/draw.hh>
#include <moppe/render/renderer.hh>

namespace moppe::game {
  // The Caddy (models/caddy.blend) as baked meshes drawn through the
  // renderer: the body, the hardtop, the tailgate shut on its hinge, the
  // glass, each wheel on its spring and the front pair turned with the
  // steering, and the steering wheel turned about its column. Its riders
  // are the hiker seated, the driver's hands on the wheel, skinned into
  // the frame's draw list.
  // Laden (game/sling.hh), its hardtop is off and its tailgate down
  // for the motocross in its bed.
  void render_caddy (render::Renderer& r,
                     render::DrawList& dl,
                     const CaddyPose& caddy,
                     JeepRiders riders,
                     uint64_t motion_base,
                     bool laden = false);

  // The helicopter's line from its hook to the ring over the Caddy, and
  // the bridle's four legs from there down to it; slack, they sag.
  void render_sling (render::DrawList& dl, const SlingPose& sling);
}

#endif
