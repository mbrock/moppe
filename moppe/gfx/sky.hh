
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
    
    bool m_use_shader;
  };
}
}

#endif
