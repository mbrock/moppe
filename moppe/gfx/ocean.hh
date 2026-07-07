#ifndef MOPPE_OCEAN_HH
#define MOPPE_OCEAN_HH

#include <moppe/app/gl.hh>
#include <moppe/gfx/math.hh>
#include <moppe/gfx/shader.hh>

namespace moppe {
namespace gfx {
  // A shader-animated water plane at a fixed sea level, extending
  // well past the terrain so it reaches the horizon.
  class Ocean {
  public:
    Ocean (float level, const Vector3D& center, float half_extent);

    void load ();
    void render (float time, const Vector3D& fog_color);

    float level () const { return m_level; }

    void set_fog_scale (float scale) { m_fog_scale = scale; }

  private:
    gl::Shader m_vertex_shader;
    gl::Shader m_fragment_shader;
    gl::ShaderProgram m_program;

    float m_level;
    Vector3D m_center;
    float m_half_extent;
    float m_fog_scale;

    GLuint m_list;
  };
}
}

#endif
