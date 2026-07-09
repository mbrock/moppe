#include <moppe/gfx/sky.hh>
#include <cmath>
#include <stdexcept>

namespace moppe {
namespace gfx {
  Sky::Sky (const std::string& filename)
    : m_texture(filename),
      m_vertex_shader(GL_VERTEX_SHADER_ARB, "shaders/sky.vert"),
      m_fragment_shader(GL_FRAGMENT_SHADER_ARB, "shaders/sky.frag"),
      m_time(0.0f),
      m_sun_height(0.8f),
      m_cloudiness(0.5f),
      m_sun_dir(0, 1, 0),
      m_fog_color(0.5, 0.6, 0.8),
      m_use_shader(true),
      m_sphere_list(0)
  { }

  void
  Sky::load () {
    try {
      // Load texture as fallback
      m_texture.load();
      
      try {
        // Load shaders
        m_vertex_shader.load();
        m_fragment_shader.load();
        
        // Setup shader program
        m_program.load();
        m_program.attach(m_vertex_shader);
        m_program.attach(m_fragment_shader);
        m_program.link();
        
        // Check shader log for errors
        m_vertex_shader.print_log();
        m_fragment_shader.print_log();
        m_program.print_log();
        
        // Check if shaders compiled successfully
        GLint vertSuccess, fragSuccess, progLinked;
        glGetObjectParameterivARB(m_vertex_shader.id(), GL_OBJECT_COMPILE_STATUS_ARB, &vertSuccess);
        glGetObjectParameterivARB(m_fragment_shader.id(), GL_OBJECT_COMPILE_STATUS_ARB, &fragSuccess);
        glGetObjectParameterivARB(m_program.id(), GL_OBJECT_LINK_STATUS_ARB, &progLinked);
        
        if (!vertSuccess || !fragSuccess || !progLinked) {
          std::cerr << "Shader compilation or linking failed, falling back to texture" << std::endl;
          throw std::runtime_error("Shader compilation failed");
        }
        
        m_use_shader = true;
      } catch (const std::exception& e) {
        std::cerr << "Exception during shader setup: " << e.what() << std::endl;
        m_use_shader = false;
      }
    } catch (const std::exception& e) {
      std::cerr << "Failed to load sky shader: " << e.what() << std::endl;
      std::cerr << "Falling back to texture-based sky" << std::endl;
      m_use_shader = false;
    }

    // The sphere geometry is static (all animation is uniform-
    // driven), so compile it once: one strip per stack ring
    const float radius = 4000.0f;
    const int slices = 24, stacks = 24;

    m_sphere_list = glGenLists(1);
    glNewList(m_sphere_list, GL_COMPILE);
    for (int i = 0; i < stacks; ++i) {
      const float a0 = M_PI * (float) i / stacks - M_PI / 2;
      const float a1 = M_PI * (float) (i + 1) / stacks - M_PI / 2;
      const float y0 = radius * sin(a0), r0 = radius * cos(a0);
      const float y1 = radius * sin(a1), r1 = radius * cos(a1);

      glBegin(GL_TRIANGLE_STRIP);
      for (int j = 0; j <= slices; ++j) {
        const float b = 2 * M_PI * (float) j / slices;
        const float cb = cos(b), sb = sin(b);
        glVertex3f(r1 * cb, y1, r1 * sb);
        glVertex3f(r0 * cb, y0, r0 * sb);
      }
      glEnd();
    }
    glEndList();
  }

  void
  Sky::render () const {
    // Drawn AFTER the terrain with depth testing on (writes off):
    // terrain depth kills the hidden sky fragments, so the pricey
    // cloud shader only runs on visible sky pixels.
    gl::ScopedAttribSaver attribs(GL_ENABLE_BIT |
                                  GL_DEPTH_BUFFER_BIT);
    glDisable(GL_CULL_FACE);
    glDepthMask(GL_FALSE);
    // The sky shader forces depth to the far plane (exactly 1.0 in
    // fp32), so LEQUAL is required for it to pass where the buffer
    // still holds the clear value
    glDepthFunc(GL_LEQUAL);

    if (m_use_shader) {
      // Use procedural shader
      m_program.use();

      // Set uniforms
      int timeLoc = glGetUniformLocationARB(m_program.id(), "time");
      if (timeLoc != -1) glUniform1fARB(timeLoc, m_time);

      int sunHeightLoc = glGetUniformLocationARB(m_program.id(), "sunHeight");
      if (sunHeightLoc != -1) glUniform1fARB(sunHeightLoc, m_sun_height);

      int cloudinessLoc = glGetUniformLocationARB(m_program.id(), "cloudiness");
      if (cloudinessLoc != -1) glUniform1fARB(cloudinessLoc, m_cloudiness);

      int sunDirLoc = glGetUniformLocationARB(m_program.id(), "sunDir");
      if (sunDirLoc != -1)
        glUniform3fARB(sunDirLoc, m_sun_dir.x, m_sun_dir.y,
                       m_sun_dir.z);

      int fogColorLoc = glGetUniformLocationARB(m_program.id(), "fogColor");
      if (fogColorLoc != -1)
        glUniform3fARB(fogColorLoc, m_fog_color.x, m_fog_color.y,
                       m_fog_color.z);

      if (m_sphere_list != 0)
        glCallList(m_sphere_list);

      // Unuse shader
      m_program.unuse();
    } else {
      // Fallback to texture-based sky
      m_texture.bind(0);
      
      GLUquadricObj *q(gluNewQuadric());
      gluQuadricDrawStyle(q, GLU_FILL);
      gluQuadricNormals(q, GLU_SMOOTH);
      gluQuadricTexture(q, GL_TRUE);
      gluSphere(q, 2.0, 20, 20);
      gluDeleteQuadric(q);
    }
  }
  
  Vector3D
  Sky::get_horizon_color() const {
    // Calculate horizon color based on day/night cycle
    // This should match the calculation in the sky.frag shader for horizon colors
    
    // Day horizon colors (blue)
    const Vector3D day_horizon(0.3f, 0.5f, 0.9f);
    
    // Night horizon colors (dark blue)
    const Vector3D night_horizon(0.05f, 0.05f, 0.1f);
    
    // Sunset/sunrise colors (orange/pink tint)
    const Vector3D sunset_horizon(0.7f, 0.4f, 0.3f);
    
    // Determine if we're at sunset/sunrise (between 0.2 and 0.4 or 0.6 and 0.8)
    bool is_sunset = (m_sun_height > 0.1f && m_sun_height < 0.3f) || 
                     (m_sun_height > 0.7f && m_sun_height < 0.9f);
    
    Vector3D horizon_color;
    
    if (is_sunset) {
      // Calculate sunset intensity - peaks at 0.2 and 0.8
      float sunset_intensity;
      if (m_sun_height < 0.5f) {
        sunset_intensity = 1.0f - abs(m_sun_height - 0.2f) / 0.1f;
      } else {
        sunset_intensity = 1.0f - abs(m_sun_height - 0.8f) / 0.1f;
      }
      sunset_intensity = std::max(0.0f, std::min(1.0f, sunset_intensity));
      
      // Base color is interpolation between night and day
      Vector3D base_color = night_horizon * (1.0f - m_sun_height) + day_horizon * m_sun_height;
      
      // Add sunset tint
      horizon_color = base_color * (1.0f - sunset_intensity) + sunset_horizon * sunset_intensity;
    } else {
      // Normal day/night interpolation
      horizon_color = night_horizon * (1.0f - m_sun_height) + day_horizon * m_sun_height;
    }
    
    return horizon_color;
  }
}
}
