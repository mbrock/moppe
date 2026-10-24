#ifndef MOPPE_GAME_MOUNTAIN_BIKE_RENDER_HH
#define MOPPE_GAME_MOUNTAIN_BIKE_RENDER_HH

#include <moppe/game/frame_view.hh>
#include <moppe/game/outfit.hh>
#include <moppe/render/draw.hh>
#include <moppe/render/renderer.hh>

#include <optional>

namespace moppe::game {
  // The mountainbike (models/mountain_bike.blend) as baked meshes drawn
  // through the renderer: the frame, the bars and fork turned about the
  // head tube's axis with the lowers sliding up the stanchions as the fork
  // works, the wheels rolling, and the crank turning with the pedals kept
  // level on it. A rider, given their outfit, is the hiker on the saddle
  // with their hands on the grips and their feet going round on the
  // pedals, skinned into the frame's draw list.
  void render_mountain_bike (render::Renderer& r,
                             render::DrawList& dl,
                             const VehiclePose& bike,
                             std::optional<Outfit> rider,
                             uint64_t motion_base);
}

#endif
