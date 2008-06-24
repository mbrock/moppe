
#include <moppe/app/gl.hh>
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

  void TerrainRenderer::render (float w_scale, float h_scale) {
    int width  = m_map.width ();
    int height = m_map.height ();

    gl::ScopedMatrixSaver matrix;

    glTranslatef (-0.5 * w_scale * width, 0,
		  -0.5 * w_scale * height);

    for (int y = 0; y < height - 1; y++)
      {
	glBegin (GL_TRIANGLE_STRIP);
	for (int x = 0; x < width; x++)
	  {
	    Vector3D a (w_scale * x,
			h_scale * m_map.get (x, y),
			w_scale * y);
	    Vector3D b (w_scale * x,
			h_scale * m_map.get (x, y + 1),
			w_scale * (y + 1));

	    Vector3D c (w_scale * (x + 1),
			h_scale * m_map.get (x, y + 1),
			w_scale * (y + 1));
	    Vector3D d (w_scale * (x + 1),
			h_scale * m_map.get (x, y),
			w_scale * y);

	    // AAHH!  THE STUPIDITY!
	    Vector3D n1 = (b - a).cross (c - a);
	    Vector3D n2 = (c - a).cross (d - a);
	    
	    glNormal3f (n2.x, n2.y, n2.z);
	    glVertex3f (a.x, a.y, a.z);
	    glNormal3f (n1.x, n1.y, n1.z);
	    glVertex3f (b.x, b.y, b.z);
	  }
	glEnd ();
      }
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
