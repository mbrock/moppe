#ifndef MOPPE_GAME_FLEET_RENDER_HH
#define MOPPE_GAME_FLEET_RENDER_HH

#include <moppe/game/blob_shadow.hh>
#include <moppe/game/frame_view.hh>
#include <moppe/game/outfit.hh>
#include <moppe/render/draw.hh>
#include <moppe/render/renderer.hh>

#include <array>
#include <optional>

namespace moppe::game {
  // Who is drawn with a machine: its driver and a passenger beside
  // them, in their outfits, and whether the view is from the driver's
  // own eyes (the motocross is then not drawn at all).
  struct Riders {
    std::optional<Outfit> driver;
    std::optional<Outfit> passenger;
    bool from_the_seat = false;
  };
  using FleetRiders = std::array<Riders, machine_count>;

  // Where a fleet is drawn: the renderer and the draw list, the time
  // (for what sways), the rod in the helmsman's hand, and the soft
  // shadow under each machine and the ground it falls on. A fleet that
  // only casts shadows has no blobs, no riders, and no motion.
  struct FleetScene {
    render::Renderer& r;
    render::DrawList& dl;
    seconds_t time = 0.0f * u::s;
    std::optional<RodPose> rod;
    BlobShadow* blob = nullptr;
    const map::SurfaceGeometry* surface = nullptr;
  };

  // Draws every machine of a fleet with its riders, this player's or
  // the other's, whose motion is told apart.
  void draw_fleet (const FleetScene& scene,
                   const FleetPoses& fleet,
                   const FleetRiders& riders,
                   bool theirs);
  // The fleet as it casts shadows.
  void cast_fleet (render::Renderer& r,
                   render::DrawList& dl,
                   const FleetPoses& fleet);
}

#endif
