#ifndef MOPPE_TERRAIN_HH
#define MOPPE_TERRAIN_HH

#include <moppe/map/generate.hh>
#include <moppe/gfx/vertex-array.hh>
#include <moppe/gfx/shader.hh>
#include <moppe/gfx/texture.hh>

#include <boost/scoped_array.hpp>
#include <memory>

namespace moppe {
namespace gfx {
  class TerrainRenderer {
  public:
    TerrainRenderer (const map::HeightMap& map);

    void setup_shader ();
    void regenerate ();

    void translate ();
    void render ();
    void render_directly ();

  private:
    const map::HeightMap& m_map;

    VertexArray m_vertices;
    VertexArray m_normals;
    VertexArray m_texcoords;

    gl::Shader m_vertex_shader;
    gl::Shader m_fragment_shader;
    gl::ShaderProgram m_shader_program;

    gl::Texture m_tex_grass;
    gl::Texture m_tex_dirt;
    gl::Texture m_tex_snow;
  };
}
}

#endif
