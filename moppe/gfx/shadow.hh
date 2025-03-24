#ifndef MOPPE_SHADOW_HH
#define MOPPE_SHADOW_HH

#include <moppe/app/gl.hh>
#include <moppe/gfx/math.hh>

namespace moppe {
namespace gfx {
  class ShadowMap {
  public:
    ShadowMap(int width = 2048, int height = 2048);
    ~ShadowMap();
    
    void begin_shadow_pass();
    void end_shadow_pass();
    
    void bind_shadow_map();
    void unbind_shadow_map();
    
    const GLfloat* get_light_matrix() const { return m_light_matrix; }
    void update_light_position(const Vector3D& light_dir, const Vector3D& center, float radius);
    
    // Check if shadow mapping is available/valid
    bool is_valid() const { return m_fbo != 0 && m_depth_texture != 0; }
    
  private:
    int m_width;
    int m_height;
    
    GLuint m_fbo;           // Framebuffer for shadow map
    GLuint m_depth_texture; // Shadow map depth texture
    
    GLfloat m_light_matrix[16]; // Light's view-projection matrix
    GLfloat m_bias_matrix[16];  // Bias matrix for texture coords
  };
}
}

#endif