
#ifndef MOPPE_SKY_HH
#define MOPPE_SKY_HH

#include <moppe/gfx/texture.hh>
#include <moppe/gfx/shader.hh>

namespace moppe {
namespace gfx {
  class Sky {
  public:
    Sky (const std::string& filename);

    void load ();
    void render () const;
    
    // Set sky parameters
    void set_time(float time) { m_time = time; }
    void set_sun_height(float height) { m_sun_height = height; }
    void set_cloudiness(float cloudiness) { m_cloudiness = cloudiness; }

    // One sun for the whole scene: the same world-space direction
    // the GL light and the shadow map use
    void set_sun_direction(const Vector3D& dir) { m_sun_dir = dir; }

    // Terrain/ocean fog color, so the horizon seams match
    void set_fog_color(const Vector3D& c) { m_fog_color = c; }
    
    // Get horizon color for fog
    Vector3D get_horizon_color() const;
    
    // Get current sun height (for lighting adjustments)
    float get_sun_height() const { return m_sun_height; }
    
  private:
    gl::Texture m_texture;        // Kept for fallback
    gl::Shader m_vertex_shader;   // Sky vertex shader
    gl::Shader m_fragment_shader; // Sky fragment shader
    gl::ShaderProgram m_program;  // Sky shader program
    
    // Sky parameters
    float m_time;
    float m_sun_height;
    float m_cloudiness;
    Vector3D m_sun_dir;
    Vector3D m_fog_color;

    bool m_use_shader;
    GLuint m_sphere_list;
  };
}
}

#endif
