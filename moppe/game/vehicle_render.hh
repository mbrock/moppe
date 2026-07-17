#ifndef MOPPE_GAME_VEHICLE_RENDER_HH
#define MOPPE_GAME_VEHICLE_RENDER_HH

#include <moppe/game/frame_view.hh>
#include <moppe/render/draw.hh>
#include <moppe/render/renderer.hh>

namespace moppe {
  namespace game {
    // The drawing half of the old Vehicle::render, split out so the
    // simulation stays renderer-free.  Reads one immutable vehicle pose;
    // the bike's rigid assemblies
    // are baked meshes drawn straight through the renderer, while
    // shape-changing parts (suspension links) record into the frame's
    // draw list.  Dispatches bike vs. commandeered car/truck on the
    // body kind.  `time` (seconds) replaces the hidden
    // glutGet(GLUT_ELAPSED_TIME) that drove the flashing light bars.
    void render_vehicle (render::Renderer& r,
                         render::DrawList& dl,
                         const VehiclePose& vehicle,
                         float time,
                         const Vec3& visual_scale = Vec3 (1, 1, 1));

    // The exhaust lick and jump-jet plumes: baked unit cones replayed
    // with breathing scale matrices.  Additive glow must blend over the
    // already-drawn solids, so the caller invokes this after playing
    // the world draw list (alongside the star halos), not at vehicle
    // draw time.
    void render_vehicle_flames (render::Renderer& r,
                                const VehiclePose& vehicle,
                                float time,
                                const Vec3& visual_scale = Vec3 (1, 1, 1));
  }
}

#endif
