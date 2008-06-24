#ifndef MOPPE_TERRAIN_HH
#define MOPPE_TERRAIN_HH

#include <moppe/map/generate.hh>

namespace moppe {
namespace gfx {
  class TerrainRenderer {
  public:
    TerrainRenderer (const map::HeightMap& map);

    void recalculate_normals ();
    void render (float w_scale, float h_scale);

  private:
    void calculate_face_normals ();
    void calculate_vertex_normals ();

  private:
    const map::HeightMap& m_map;
//     const NormalMap& m_face_normals;
//     const NormalMap& m_vertex_normals;
  };
}
}

#endif
