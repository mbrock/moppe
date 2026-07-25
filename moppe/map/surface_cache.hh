#ifndef MOPPE_MAP_SURFACE_CACHE_HH
#define MOPPE_MAP_SURFACE_CACHE_HH

#include <moppe/map/surface.hh>

#include <string>

namespace moppe::map {
  // Fills every geometry column from a previous run's file, if one matches
  // this lattice and this bundle's shape.
  bool try_load_cache (SurfaceGeometry& geometry, const std::string& path);

  void save_cache (const SurfaceGeometry& geometry, const std::string& path);
}

#endif
