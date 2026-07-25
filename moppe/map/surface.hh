#ifndef MOPPE_MAP_SURFACE_HH
#define MOPPE_MAP_SURFACE_HH

#include <moppe/map/surface_sections.hh>

// The world's finite ground geometry is the SurfaceGeometry bundle itself:
// elevation, normals, and the material history the world's erosion left
// behind, over one terrain domain. These are the operations the game and the
// generator perform on it. Readings analysed over the same domain live in a
// separate bundle the completed world owns.

namespace moppe::map {
  // Normals and the broad snow support plane follow from elevation; rebuild
  // them whenever the heightfield changes.
  void rebuild_geometry (SurfaceGeometry& geometry);
}

#endif
