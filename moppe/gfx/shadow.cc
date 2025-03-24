#include <moppe/gfx/shadow.hh>
#include <iostream>

namespace moppe {
namespace gfx {

  ShadowMap::ShadowMap(int width, int height)
    : m_width(width), m_height(height), m_fbo(0), m_depth_texture(0)
  {
    // Check if FBO extension is supported
    std::cerr << "ShadowMap: Checking for framebuffer object support..." << std::endl;
    
    // Check for framebuffer object extension support
    bool fbo_supported = false;
    
    // Get OpenGL extension string
    const char* extensions = (const char*)glGetString(GL_EXTENSIONS);
    if (extensions) {
        std::string ext_string(extensions);
        std::cerr << "ShadowMap: OpenGL extensions: " << ext_string << std::endl;
        // Check if GL_EXT_framebuffer_object or GL_ARB_framebuffer_object is supported
        if (ext_string.find("GL_EXT_framebuffer_object") != std::string::npos ||
            ext_string.find("GL_ARB_framebuffer_object") != std::string::npos) {
            fbo_supported = true;
            std::cerr << "ShadowMap: Framebuffer objects are supported" << std::endl;
        } else {
            std::cerr << "ShadowMap: Framebuffer objects NOT supported - falling back to normal-based shadows" << std::endl;
            return; // Early exit - won't create FBO
        }
    } else {
        std::cerr << "ShadowMap: Could not query OpenGL extensions - assuming no FBO support" << std::endl;
        return; // Early exit - won't create FBO
    }
    
    if (!fbo_supported) {
        return; // Early exit - won't create FBO
    }
    
    // Initialize bias matrix (transforms from [-1,1] to [0,1] for texture coords)
    static const GLfloat biasMatrix[16] = {
      0.5f, 0.0f, 0.0f, 0.0f,
      0.0f, 0.5f, 0.0f, 0.0f,
      0.0f, 0.0f, 0.5f, 0.0f,
      0.5f, 0.5f, 0.5f, 1.0f
    };
    
    std::cerr << "ShadowMap: Initializing shadow map with dimensions " << width << "x" << height << std::endl;
    
    // Copy bias matrix
    for (int i = 0; i < 16; i++) {
      m_bias_matrix[i] = biasMatrix[i];
    }
    
    // Initialize light matrix to identity
    for (int i = 0; i < 16; i++) {
      m_light_matrix[i] = (i % 5 == 0) ? 1.0f : 0.0f; // Identity matrix
    }
    
    std::cerr << "ShadowMap: Creating framebuffer and depth texture..." << std::endl;
    // Create the FBO for the shadow map
    glGenFramebuffers(1, &m_fbo);

    std::cerr << "ShadowMap: FBO ID: " << m_fbo << std::endl;
    
    // Create the depth texture for the shadow map
    glGenTextures(1, &m_depth_texture);
    std::cerr << "ShadowMap: Depth texture ID: " << m_depth_texture << std::endl;
    
    glBindTexture(GL_TEXTURE_2D, m_depth_texture);
    
    std::cerr << "ShadowMap: FBO ID: " << m_fbo << ", Depth texture ID: " << m_depth_texture << std::endl;
    
    // Initialize depth texture
    glTexImage2D(GL_TEXTURE_2D, 0, GL_DEPTH_COMPONENT, 
                 m_width, m_height, 0, 
                 GL_DEPTH_COMPONENT, GL_FLOAT, NULL);
    
    // Set texture parameters
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    
    std::cerr << "ShadowMap: Setting up comparison mode for shadow mapping" << std::endl;
    
    // Comparison mode - needed for shadow mapping
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_COMPARE_MODE, GL_COMPARE_R_TO_TEXTURE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_COMPARE_FUNC, GL_LEQUAL);
    
    // Attach depth texture to the FBO
    glBindFramebuffer(GL_FRAMEBUFFER, m_fbo);
    glFramebufferTexture2D(GL_FRAMEBUFFER, GL_DEPTH_ATTACHMENT, 
                         GL_TEXTURE_2D, m_depth_texture, 0);
    
    // No color buffer is needed
    glDrawBuffer(GL_NONE);
    glReadBuffer(GL_NONE);
    
    // Check for completeness
    GLenum status = glCheckFramebufferStatus(GL_FRAMEBUFFER);
    if (status != GL_FRAMEBUFFER_COMPLETE) {
      std::cerr << "ERROR: Shadow map framebuffer is not complete! Status code: " << status << std::endl;
    } else {
      std::cerr << "ShadowMap: Framebuffer successfully created and complete" << std::endl;
    }
    
    // Unbind the FBO
    glBindFramebuffer(GL_FRAMEBUFFER, 0);
    std::cerr << "ShadowMap: Initialization complete" << std::endl;
  }
  
  ShadowMap::~ShadowMap() {
    glDeleteTextures(1, &m_depth_texture);
    glDeleteFramebuffers(1, &m_fbo);
  }
  
  void ShadowMap::begin_shadow_pass() {
    // Make sure we have a valid framebuffer
    if (m_fbo == 0) {
      std::cerr << "ShadowMap: Cannot begin shadow pass, invalid framebuffer" << std::endl;
      return;
    }
    
    // Save current viewport and switch to shadow map's viewport
    glPushAttrib(GL_VIEWPORT_BIT);
    glViewport(0, 0, m_width, m_height);
    
    // Bind shadow FBO
    glBindFramebuffer(GL_FRAMEBUFFER, m_fbo);
    
    // Clear depth
    glClear(GL_DEPTH_BUFFER_BIT);
    
    // Use front face culling for shadow map pass
    // This helps reduce shadow acne caused by self-shadowing
    glCullFace(GL_FRONT);
    
    // Disable color writes
    glColorMask(GL_FALSE, GL_FALSE, GL_FALSE, GL_FALSE);
    
    // Reduce self-shadowing with enhanced polygon offset for better shadow quality
    glEnable(GL_POLYGON_OFFSET_FILL);
    glPolygonOffset(2.0f, 2.0f); // Reduced offset to allow more precise shadows
  }
  
  void ShadowMap::end_shadow_pass() {
    // Make sure we have a valid framebuffer
    if (m_fbo == 0) {
      return;
    }
    
    // Restore normal rendering
    glDisable(GL_POLYGON_OFFSET_FILL);
    glColorMask(GL_TRUE, GL_TRUE, GL_TRUE, GL_TRUE);
    glCullFace(GL_BACK);
    
    // Unbind FBO
    glBindFramebuffer(GL_FRAMEBUFFER, 0);
    
    // Restore viewport
    glPopAttrib();
  }
  
  void ShadowMap::bind_shadow_map() {
    // Check if shadow mapping is enabled
    if (m_depth_texture == 0) {
      return;
    }
    
    // Bind depth texture to texture unit 3
    glActiveTexture(GL_TEXTURE3);
    glBindTexture(GL_TEXTURE_2D, m_depth_texture);
  }
  
  void ShadowMap::unbind_shadow_map() {
    // Check if shadow mapping is enabled
    if (m_depth_texture == 0) {
      return;
    }
    
    glActiveTexture(GL_TEXTURE3);
    glBindTexture(GL_TEXTURE_2D, 0);
    glActiveTexture(GL_TEXTURE0);
  }
  
  void ShadowMap::update_light_position(const Vector3D& light_dir, const Vector3D& center, float radius) {
    // Save current matrices
    glPushMatrix();
    glMatrixMode(GL_PROJECTION);
    glPushMatrix();
    glMatrixMode(GL_MODELVIEW);
    
    // Set up light's view
    glMatrixMode(GL_PROJECTION);
    glLoadIdentity();
    
    // Orthographic projection from light's point of view
    glOrtho(-radius, radius, -radius, radius, -radius * 3, radius * 3);
    
    glMatrixMode(GL_MODELVIEW);
    glLoadIdentity();
    
    // Create light position that's farther away to produce longer shadows
    Vector3D light_pos = center - light_dir * (radius * 3.5);
    
    // Look from light position toward the scene center with slight offset for better shadowing
    // This helps create longer and more dramatic shadows
    Vector3D center_offset = center + Vector3D(0.0f, radius * 0.05f, 0.0f);
    gluLookAt(light_pos.x, light_pos.y, light_pos.z,
             center_offset.x, center_offset.y, center_offset.z,
             0.0f, 1.0f, 0.0f);
    
    // Get the light's view-projection matrix
    GLfloat proj_matrix[16];
    GLfloat model_matrix[16];
    
    glGetFloatv(GL_PROJECTION_MATRIX, proj_matrix);
    glGetFloatv(GL_MODELVIEW_MATRIX, model_matrix);
    
    // Multiply projection * modelview to get the complete light transform
    // Note: This is a simplified matrix multiplication for 4x4 matrices
    for (int i = 0; i < 4; i++) {
      for (int j = 0; j < 4; j++) {
        m_light_matrix[i*4+j] = 0.0f;
        for (int k = 0; k < 4; k++) {
          m_light_matrix[i*4+j] += proj_matrix[i*4+k] * model_matrix[k*4+j];
        }
      }
    }
    
    // Restore matrices
    glMatrixMode(GL_PROJECTION);
    glPopMatrix();
    glMatrixMode(GL_MODELVIEW);
    glPopMatrix();
  }
}
}