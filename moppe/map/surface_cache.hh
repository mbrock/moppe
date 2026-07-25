#ifndef MOPPE_MAP_SURFACE_CACHE_HH
#define MOPPE_MAP_SURFACE_CACHE_HH

#include <moppe/map/surface.hh>

#include <string>

namespace moppe::map {
  // Fills the elevation and material columns from a previous run's file, if
  // one matches this lattice. Derived geometry still needs rebuilding.
  bool try_load_cache (SurfaceGeometry& geometry, const std::string& path);

  void save_cache (const SurfaceGeometry& geometry, const std::string& path);
}

#endif
