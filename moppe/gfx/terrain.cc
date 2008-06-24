
#include <moppe/app/gl.hh>
#include <moppe/gfx/terrain.hh>

namespace moppe {
namespace gfx {
  TerrainRenderer::TerrainRenderer (const map::HeightMap& map)
    : m_map (map)
  { }

  void
  TerrainRenderer::render () {
    int width      = m_map.width ();
    int height     = m_map.height ();
    Vector3D scale = m_map.scale ();

    gl::ScopedMatrixSaver matrix;

    glTranslatef (-0.5 * scale.x * width, 0,
		  -0.5 * scale.z * height);

    for (int y = 0; y < height - 2; y++)
      {
	glBegin (GL_TRIANGLE_STRIP);
	for (int x = 0; x < width - 1; x++)
	  {
	    gl::normal (m_map.normal (x, y));
	    gl::vertex (m_map.vertex (x, y));
	    gl::normal (m_map.normal (x, y + 1));
	    gl::vertex (m_map.vertex (x, y + 1));
	  }
	glEnd ();
      }
  }
}
}
