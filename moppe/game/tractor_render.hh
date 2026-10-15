#ifndef MOPPE_GAME_TRACTOR_RENDER_HH
#define MOPPE_GAME_TRACTOR_RENDER_HH

#include <moppe/game/frame_view.hh>
#include <moppe/game/outfit.hh>
#include <moppe/render/draw.hh>
#include <moppe/render/renderer.hh>

#include <optional>

namespace moppe::game {
  // The tractor (models/tractor.blend) as baked meshes drawn through the
  // renderer: the body and hitch, each wheel on its spring and the front
  // pair turned with the steering, and the steering wheel turned about
  // its column. A driver, given their outfit, is the hiker seated with
  // their hands on the wheel and feet on the footplates, skinned into the
  // frame's draw list.
  void render_tractor (render::Renderer& r,
                       render::DrawList& dl,
                       const TractorPose& tractor,
                       std::optional<Outfit> driver,
                       uint64_t motion_base);
}

#endif
