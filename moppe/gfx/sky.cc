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
      m_use_shader(true)
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
  }

  void
  Sky::render () const {
    gl::ScopedAttribSaver attribs(GL_ENABLE_BIT);
    glDisable(GL_DEPTH_TEST);
    glDisable(GL_CULL_FACE);
    
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
      
      // Draw a full sphere that completely surrounds the scene
      // Using a sphere instead of dome to ensure complete coverage
      float radius = 4000.0f;  // Much larger radius to encompass terrain at all elevations
      int slices = 32;         // Horizontal resolution
      int stacks = 32;         // Vertical resolution (full sphere)
      
      glBegin(GL_TRIANGLE_STRIP);
      
      // Generate a full sphere (not just hemisphere)
      for (int i = 0; i <= stacks; ++i) {
          // Full sphere goes from -90 to +90 degrees
          float stackAngle = (float)(M_PI) * ((float)i / (float)stacks) - (float)(M_PI / 2);
          float y = radius * sin(stackAngle); // Up vector (-radius to +radius)
          float ringRadius = radius * cos(stackAngle); // Circle radius at this height
          
          for (int j = 0; j <= slices; ++j) {
              float sliceAngle = (float)(2 * M_PI) * ((float)j / (float)slices);
              float x = ringRadius * cos(sliceAngle);
              float z = ringRadius * sin(sliceAngle);
              
              // Generate vertex
              glVertex3f(x, y, z);
              
              // Generate next vertex in strip if not at last ring
              if (i < stacks) {
                  float nextStackAngle = (float)(M_PI) * ((float)(i + 1) / (float)stacks) - (float)(M_PI / 2);
                  float nextY = radius * sin(nextStackAngle);
                  float nextRingRadius = radius * cos(nextStackAngle);
                  float nextX = nextRingRadius * cos(sliceAngle);
                  float nextZ = nextRingRadius * sin(sliceAngle);
                  
                  glVertex3f(nextX, nextY, nextZ);
              }
          }
      }
      
      glEnd();
      
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
