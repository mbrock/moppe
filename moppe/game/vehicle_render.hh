#ifndef MOPPE_GAME_VEHICLE_RENDER_HH
#define MOPPE_GAME_VEHICLE_RENDER_HH

#include <moppe/mov/vehicle.hh>
#include <moppe/render/draw.hh>
#include <moppe/render/renderer.hh>

namespace moppe {
  namespace game {
    // The drawing half of the old Vehicle::render, split out so the
    // simulation stays renderer-free.  Reads pose and body style
    // through Vehicle's const accessors; the bike's rigid assemblies
    // are baked meshes drawn straight through the renderer, while
    // shape-changing parts (suspension links) record into the frame's
    // draw list.  Dispatches bike vs. commandeered car/truck on the
    // body kind.  `time` (seconds) replaces the hidden
    // glutGet(GLUT_ELAPSED_TIME) that drove the flashing light bars.
    void render_vehicle (render::Renderer& r,
                         render::DrawList& dl,
                         const mov::Vehicle& v,
                         float time,
                         float visual_scale = 1.0f);

    // The exhaust lick and jump-jet plumes: baked unit cones replayed
    // with breathing scale matrices.  Additive glow must blend over the
    // already-drawn solids, so the caller invokes this after playing
    // the world draw list (alongside the star halos), not at vehicle
    // draw time.
    void render_vehicle_flames (render::Renderer& r,
                                const mov::Vehicle& v,
                                float time,
                                float visual_scale = 1.0f);
  }
}

#endif
