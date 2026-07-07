#include <moppe/gfx/ocean.hh>

namespace moppe {
namespace gfx {
  Ocean::Ocean (float level, const Vector3D& center, float half_extent)
    : m_vertex_shader (GL_VERTEX_SHADER_ARB, "shaders/ocean.vert"),
      m_fragment_shader (GL_FRAGMENT_SHADER_ARB, "shaders/ocean.frag"),
      m_level (level),
      m_center (center),
      m_half_extent (half_extent),
      m_fog_scale (0.0004f),
      m_list (0)
  { }

  void
  Ocean::load () {
    m_vertex_shader.load ();
    m_fragment_shader.load ();
    m_program.load ();
    m_program.attach (m_vertex_shader);
    m_program.attach (m_fragment_shader);
    m_program.link ();

    m_vertex_shader.print_log ();
    m_fragment_shader.print_log ();
    m_program.print_log ();

    // A flat grid at sea level; the vertex shader does the waving.
    const int cells = 300;
    const float step = 2 * m_half_extent / cells;
    const float x0 = m_center.x - m_half_extent;
    const float z0 = m_center.z - m_half_extent;

    m_list = glGenLists (1);
    glNewList (m_list, GL_COMPILE);
    for (int j = 0; j < cells; ++j)
      {
	glBegin (GL_TRIANGLE_STRIP);
	for (int i = 0; i <= cells; ++i)
	  {
	    glVertex3f (x0 + i * step, m_level, z0 + j * step);
	    glVertex3f (x0 + i * step, m_level, z0 + (j + 1) * step);
	  }
	glEnd ();
      }
    glEndList ();
  }

  void
  Ocean::render (float time, const Vector3D& fog_color) {
    if (m_list == 0)
      return;

    gl::ScopedAttribSaver attribs (GL_ENABLE_BIT);

    // Visible from below too, and translucent
    glDisable (GL_CULL_FACE);
    glEnable (GL_BLEND);

    m_program.use ();
    m_program.set_float ("time", time);
    m_program.set_float ("fogScale", m_fog_scale);
    m_program.set_vec3 ("fogColor",
			fog_color.x, fog_color.y, fog_color.z);

    glCallList (m_list);

    m_program.unuse ();
  }
}
}
