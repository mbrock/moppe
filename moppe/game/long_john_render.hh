#ifndef MOPPE_GAME_LONG_JOHN_RENDER_HH
#define MOPPE_GAME_LONG_JOHN_RENDER_HH

#include <moppe/game/frame_view.hh>
#include <moppe/game/outfit.hh>
#include <moppe/render/draw.hh>
#include <moppe/render/renderer.hh>

#include <optional>

namespace moppe::game {
  // Krisjanis's Long John (models/long_john.blend) as baked meshes drawn
  // through the renderer: the frame and its box, the fork turned about
  // the front head tube and the bars about the rider's, tied 1:1 by their
  // cables, the wheels rolling each at its own size, and the crank
  // turning with the pedals kept level on it. A rider, given their
  // outfit, is the hiker on the saddle over the back wheel with their
  // hands on the grips and their feet going round on the pedals.
  void render_long_john (render::Renderer& r,
                         render::DrawList& dl,
                         const VehiclePose& bike,
                         std::optional<Outfit> rider,
                         uint64_t motion_base);
}

#endif
