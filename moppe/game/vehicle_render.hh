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
  // shape-changing parts (suspension links, flames) record into the
  // frame's draw list.  Dispatches bike vs. commandeered car/truck
  // on the body kind.  `time` (seconds) replaces the hidden
  // glutGet(GLUT_ELAPSED_TIME) that drove the flashing light bars.
  void render_vehicle (render::Renderer& r, render::DrawList& dl,
                       const mov::Vehicle& v, float time);
}
}

#endif
