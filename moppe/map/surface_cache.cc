#include <moppe/map/surface_cache.hh>

#include <moppe/spatial/bundle_storage.hh>
#include <moppe/terrain/domain_storage.hh>

#include <fstream>
#include <stdexcept>

// A saved surface is the expensive part of world generation. The geometry
// bundle knows how to write and read itself, so this file is only the
// question of where it lives: every column travels, and a file whose lattice
// or column shape no longer matches counts as no file at all.

namespace moppe::map {
  bool try_load_cache (SurfaceGeometry& geometry, const std::string& path) {
    std::ifstream file (path, std::ios::binary);
    if (!file)
      return false;
    return spatial::load_bundle (file, geometry);
  }

  void save_cache (const SurfaceGeometry& geometry, const std::string& path) {
    std::ofstream file (path, std::ios::binary);
    if (!file)
      throw std::runtime_error ("can't write surface cache: " + path);
    spatial::write_bundle (file, geometry);
    if (!file)
      throw std::runtime_error ("can't write surface cache: " + path);
  }
}
