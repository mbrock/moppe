#ifndef MOPPE_TERRAIN_HH
#define MOPPE_TERRAIN_HH

#include <moppe/map/generate.hh>
#include <moppe/gfx/vertex-array.hh>
#include <moppe/gfx/shader.hh>
#include <moppe/gfx/texture.hh>
#include <moppe/gfx/shadow.hh>

#include <boost/scoped_array.hpp>
#include <boost/shared_ptr.hpp>
#include <memory>

namespace moppe {
namespace gfx {
  class TerrainRenderer {
  public:
    TerrainRenderer (const map::HeightMap& map);
    ~TerrainRenderer();

    void setup_shader();
    void regenerate();

    void translate();
    void render();
    void render_for_shadow_map(); // Render just the geometry for shadow map
    void render_directly();
    
    // Set the fog color for the shader
    void set_fog_color(const Vector3D& color);
    
    // Shadow support
    void update_shadow_map(const Vector3D& light_dir);
    void set_shadow_strength(float strength) { m_shadow_strength = strength; }
    float get_shadow_strength() const { return m_shadow_strength; }
    void set_light_direction(const Vector3D& light_dir);

  private:
    const map::HeightMap& m_map;

    VertexArray m_vertices;
    VertexArray m_normals;
    VertexArray m_texcoords;

    gl::Shader m_vertex_shader;
    gl::Shader m_fragment_shader;
    gl::ShaderProgram m_shader_program;
    
    // Shadow mapping shaders
    gl::Shader m_shadow_vertex_shader;
    gl::Shader m_shadow_fragment_shader;
    gl::ShaderProgram m_shadow_program;
    
    // Shadow map
    boost::shared_ptr<ShadowMap> m_shadow_map;
    float m_shadow_strength;

    gl::Texture m_tex_grass;
    gl::Texture m_tex_dirt;
    gl::Texture m_tex_snow;
  };
}
}

#endif
