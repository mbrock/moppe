#ifndef MOPPE_GAME_JET_SKI_RENDER_HH
#define MOPPE_GAME_JET_SKI_RENDER_HH

#include <moppe/game/frame_view.hh>
#include <moppe/game/outfit.hh>
#include <moppe/render/draw.hh>
#include <moppe/render/renderer.hh>

#include <optional>

namespace moppe::game {
  // The jet ski (models/jet_ski.blend) as baked meshes drawn through the
  // renderer: the hull, the bars turned about the steering column, and
  // the nozzle swung on the transom. A rider, given their outfit, sits
  // astride the saddle with both hands on the grips and their feet in
  // the wells, leaning into the turn, skinned into the frame's draw
  // list.
  void render_jet_ski (render::Renderer& r,
                       render::DrawList& dl,
                       const JetSkiPose& ski,
                       std::optional<Outfit> rider,
                       uint64_t motion_base);
}

#endif
