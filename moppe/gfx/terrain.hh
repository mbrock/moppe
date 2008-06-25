#ifndef MOPPE_TERRAIN_HH
#define MOPPE_TERRAIN_HH

#include <moppe/map/generate.hh>
#include <moppe/gfx/vertex-array.hh>

#include <boost/scoped_array.hpp>

namespace moppe {
namespace gfx {
  class TerrainRenderer {
  public:
    TerrainRenderer (const map::HeightMap& map);
    
    void regenerate ();
    void render ();
    void render_directly ();

  private:
    const map::HeightMap& m_map;

    VertexArray m_vertices;
    VertexArray m_normals;
  };
}
}

#endif
