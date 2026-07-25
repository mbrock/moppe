#ifndef MOPPE_GAME_SURFACE_PRESENTATION_HH
#define MOPPE_GAME_SURFACE_PRESENTATION_HH

#include <moppe/map/surface.hh>
#include <moppe/render/renderer.hh>

namespace moppe::game {
  // Hand the ground's readings to the renderer as descriptions of the
  // textures they become.
  //
  // There is nothing to store and nothing to pack. A reading and its pixel are
  // the same bytes, so each source writes itself once, straight into the
  // backend's staging memory, converting only where a format genuinely
  // differs -- half precision, or a planar pair drawn from a vector reading.
  //
  // Both bundles are borrowed for the duration of the call, which is all a
  // backend needs: every upload path commits and waits before returning.
  void upload_surface_readings (render::Renderer& renderer,
                                const map::SurfaceGeometry& geometry,
                                const map::SurfaceReadings& readings,
                                bool include_forest);
}

#endif
