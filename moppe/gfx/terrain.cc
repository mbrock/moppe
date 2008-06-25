
#include <moppe/app/gl.hh>
#include <moppe/gfx/terrain.hh>

#include <iostream>

namespace moppe {
namespace gfx {
  TerrainRenderer::TerrainRenderer (const map::HeightMap& map)
    : m_map (map)
  {
    regenerate ();
  }

  void
  TerrainRenderer::regenerate () {
    m_normals.clear ();
    m_vertices.clear ();

    int width      = m_map.width ();
    int height     = m_map.height ();

    m_vertices.reserve ((height - 2) * (width - 1));
    m_normals.reserve ((height - 2) * (width - 1));

    for (int y = 0; y < height - 2; ++y)
      for (int x = 0; x < width - 1; ++x)
	{
	  m_normals.add (m_map.normal (x, y));
	  m_vertices.add (m_map.vertex (x, y));
	  m_normals.add (m_map.normal (x, y + 1));
	  m_vertices.add (m_map.vertex (x, y + 1));
	}
  }

  void
  TerrainRenderer::render () {
    gl::ScopedMatrixSaver matrix;

    int width      = m_map.width ();
    int height     = m_map.height ();
    Vector3D scale = m_map.scale ();

    glTranslatef (-0.5 * scale.x * width, 0,
		  -0.5 * scale.z * height);

    for (int y = 0; y < height - 2; ++y)
      render_vertex_arrays (GL_TRIANGLE_STRIP,
			    2 * y * (width - 1),
			    2 * (width - 1),
			    m_vertices,
			    m_normals);
  }

  void
  TerrainRenderer::render_directly () {
    gl::ScopedMatrixSaver matrix;

    int width      = m_map.width ();
    int height     = m_map.height ();
    Vector3D scale = m_map.scale ();

    glTranslatef (-0.5 * scale.x * width, 0,
		  -0.5 * scale.z * height);

    for (int y = 0; y < height - 2; ++y)
      {
	glBegin (GL_TRIANGLE_STRIP);
	for (int x = 0; x < width - 1; ++x)
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
