#ifndef MOPPE_TERRAIN_HH
#define MOPPE_TERRAIN_HH

#include <moppe/map/generate.hh>

#include <boost/shared_ptr.hpp>

namespace moppe {
namespace gfx {
  class TerrainRenderer {
  public:
    TerrainRenderer (const map::HeightMap& map);
    void render ();

  private:
    const map::HeightMap& m_map;
  };
}
}

#endif
