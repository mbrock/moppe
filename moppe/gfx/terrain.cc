#include <moppe/gfx/terrain.hh>

namespace moppe {
namespace gfx {
  TerrainRenderer::TerrainRenderer (const map::HeightMap& map)
    : m_map (map)
//       m_face_normals (m_map.width () - 1,
// 		      m_map.height () - 1),
//       m_vertex_normals (m_map.width (),
// 			m_map.height ())
  {
    recalculate_normals ();
  }

  void TerrainRenderer::calculate_face_normals () {
  }

  void TerrainRenderer::calculate_vertex_normals () {
  }

  void TerrainRenderer::recalculate_normals () {
    calculate_face_normals ();
    calculate_vertex_normals ();
  }
}
}
