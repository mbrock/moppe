
#include <moppe/app/gl.hh>
#include <moppe/gfx/terrain.hh>

#include <iostream>

namespace moppe {
namespace gfx {
  using namespace moppe::gl;

  TerrainRenderer::TerrainRenderer (const map::HeightMap& map)
    : m_map (map),
      m_vertex_shader (GL_VERTEX_SHADER_ARB, "shaders/test.vert"),
      m_fragment_shader (GL_FRAGMENT_SHADER_ARB, "shaders/test.frag")
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
  TerrainRenderer::translate () {
//     Vector3D size = m_map.size ();
//     glTranslatef (-0.5 * size.x, 0,
// 		  -0.5 * size.z);
  }

  void
  TerrainRenderer::setup_shader () {
    m_vertex_shader.load ();
    m_fragment_shader.load ();
    m_shader_program.load ();
    m_shader_program.attach (m_vertex_shader);
    m_shader_program.attach (m_fragment_shader);
    m_shader_program.link ();
    
    m_vertex_shader.print_log ();
    m_fragment_shader.print_log ();
    m_shader_program.print_log ();
  }

  void
  TerrainRenderer::render () {
    gl::ScopedMatrixSaver matrix;

    m_shader_program.use ();

    int width     = m_map.width ();
    int height    = m_map.height ();

    translate ();

    static const float specular[] = { 0.8f, 0.8f, 0.8f, 1.0f };
    glMaterialfv (GL_FRONT, GL_SPECULAR, specular);
    glMateriali (GL_FRONT, GL_SHININESS, 96);

    for (int y = 0; y < height - 2; ++y)
      render_vertex_arrays (GL_TRIANGLE_STRIP,
			    2 * y * (width - 1),
			    2 * (width - 1),
			    m_vertices,
			    m_normals);

    m_shader_program.unuse ();
  }

  void
  TerrainRenderer::render_directly () {
    gl::ScopedMatrixSaver matrix;

    int width     = m_map.width ();
    int height    = m_map.height ();

    translate ();

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
