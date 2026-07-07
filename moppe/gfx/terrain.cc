
#include <moppe/app/gl.hh>
#include <moppe/gfx/terrain.hh>

#include <iostream>

namespace moppe {
namespace gfx {
  using namespace moppe::gl;

  TerrainRenderer::TerrainRenderer (const map::HeightMap& map)
    : m_map (map),
      m_vbo_vertices (0),
      m_vbo_normals (0),
      m_vbo_texcoords (0),
      m_vertex_shader (GL_VERTEX_SHADER_ARB, "shaders/test.vert"),
      m_fragment_shader (GL_FRAGMENT_SHADER_ARB, "shaders/test.frag"),
      m_shadow_vertex_shader (GL_VERTEX_SHADER_ARB, "shaders/shadow.vert"),
      m_shadow_fragment_shader (GL_FRAGMENT_SHADER_ARB, "shaders/shadow.frag"),
      m_shadow_strength(0.7f), // Default shadow strength (0.0 - 1.0)
      m_tex_grass ("textures/grass2.tga"),
      m_tex_dirt ("textures/dirt.tga"),
      m_tex_snow ("textures/snow.tga")
  {
    // Don't create shadow map yet - it will be created in setup_shader() 
    // after OpenGL context is established
    
    regenerate ();
  }
  
  TerrainRenderer::~TerrainRenderer() {
    // Shadow map will be automatically cleaned up by shared_ptr
  }

  void
  TerrainRenderer::regenerate () {
    m_normals.clear ();
    m_vertices.clear ();
    m_texcoords.clear ();

    int width      = m_map.width ();
    int height     = m_map.height ();

    m_vertices.reserve ((height - 2) * (width - 1));
    m_normals.reserve ((height - 2) * (width - 1));

    const float tex_scale = 0.5 * one_meter;

    for (int y = 0; y < height - 2; ++y)
      for (int x = 0; x < width - 1; ++x)
	{
	  m_normals.add (m_map.normal (x, y));
	  m_vertices.add (m_map.vertex (x, y));
	  m_normals.add (m_map.normal (x, y + 1));
	  m_vertices.add (m_map.vertex (x, y + 1));

	  m_texcoords.add (Vector3D (x * tex_scale,
				     y * tex_scale,
				     0.0));
	  m_texcoords.add (Vector3D (x * tex_scale,
				     (y + 1) * tex_scale,
				     0.0));
	}

    // Re-upload if the GL buffers already exist (i.e. not the
    // initial pre-context call from the constructor)
    if (m_vbo_vertices != 0)
      upload_buffers ();
  }

  void
  TerrainRenderer::upload_buffers () {
    if (m_vbo_vertices == 0)
      {
	glGenBuffers (1, &m_vbo_vertices);
	glGenBuffers (1, &m_vbo_normals);
	glGenBuffers (1, &m_vbo_texcoords);
      }

    glBindBuffer (GL_ARRAY_BUFFER, m_vbo_vertices);
    glBufferData (GL_ARRAY_BUFFER, m_vertices.size () * sizeof (float),
		  m_vertices.address (0), GL_STATIC_DRAW);
    glBindBuffer (GL_ARRAY_BUFFER, m_vbo_normals);
    glBufferData (GL_ARRAY_BUFFER, m_normals.size () * sizeof (float),
		  m_normals.address (0), GL_STATIC_DRAW);
    glBindBuffer (GL_ARRAY_BUFFER, m_vbo_texcoords);
    glBufferData (GL_ARRAY_BUFFER, m_texcoords.size () * sizeof (float),
		  m_texcoords.address (0), GL_STATIC_DRAW);
    glBindBuffer (GL_ARRAY_BUFFER, 0);

    const int width  = m_map.width ();
    const int height = m_map.height ();

    m_strip_first.clear ();
    m_strip_count.clear ();
    for (int y = 0; y < height - 2; ++y)
      {
	m_strip_first.push_back (2 * y * (width - 1));
	m_strip_count.push_back (2 * (width - 1));
      }
  }

  void
  TerrainRenderer::draw_strips () {
    glEnableClientState (GL_VERTEX_ARRAY);
    glEnableClientState (GL_NORMAL_ARRAY);
    glEnableClientState (GL_TEXTURE_COORD_ARRAY);

    glBindBuffer (GL_ARRAY_BUFFER, m_vbo_vertices);
    glVertexPointer (3, GL_FLOAT, 0, 0);
    glBindBuffer (GL_ARRAY_BUFFER, m_vbo_normals);
    glNormalPointer (GL_FLOAT, 0, 0);
    glBindBuffer (GL_ARRAY_BUFFER, m_vbo_texcoords);
    glTexCoordPointer (3, GL_FLOAT, 0, 0);
    glBindBuffer (GL_ARRAY_BUFFER, 0);

    glMultiDrawArrays (GL_TRIANGLE_STRIP,
		       &m_strip_first[0], &m_strip_count[0],
		       (GLsizei) m_strip_first.size ());

    glDisableClientState (GL_VERTEX_ARRAY);
    glDisableClientState (GL_NORMAL_ARRAY);
    glDisableClientState (GL_TEXTURE_COORD_ARRAY);
  }

  void
  TerrainRenderer::translate () {
//     Vector3D size = m_map.size ();
//     glTranslatef (-0.5 * size.x, 0,
// 		  -0.5 * size.z);
  }

  void
  TerrainRenderer::setup_shader () {
    // Load main terrain shader
    m_vertex_shader.load ();
    m_fragment_shader.load ();
    m_shader_program.load ();
    m_shader_program.attach (m_vertex_shader);
    m_shader_program.attach (m_fragment_shader);
    m_shader_program.link ();
    
    m_vertex_shader.print_log ();
    m_fragment_shader.print_log ();
    m_shader_program.print_log ();
    
    // The sun is fixed so this renders only once; 4096 keeps the
    // shadows reasonably crisp across the 6km terrain
    m_shadow_map.reset(new ShadowMap(4096, 4096));

    // Only load shadow mapping shader if shadow mapping is available
    if (m_shadow_map && m_shadow_map->is_valid()) {
      std::cerr << "Setting up shadow mapping shaders" << std::endl;
      m_shadow_vertex_shader.load();
      m_shadow_fragment_shader.load();
      m_shadow_program.load();
      m_shadow_program.attach(m_shadow_vertex_shader);
      m_shadow_program.attach(m_shadow_fragment_shader);
      m_shadow_program.link();
      
      m_shadow_vertex_shader.print_log();
      m_shadow_fragment_shader.print_log();
      m_shadow_program.print_log();
    } else {
      std::cerr << "Shadow mapping not available - using normal-based shadows" << std::endl;
    }

    // Set texture filtering to improve distant appearance;
    // anisotropic filtering keeps grazing-angle ground crisp
    m_tex_grass.load ();
    m_tex_grass.bind(0);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR_MIPMAP_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    glTexParameterf(GL_TEXTURE_2D, GL_TEXTURE_MAX_ANISOTROPY_EXT, 8.0f);
    glGenerateMipmap(GL_TEXTURE_2D);

    m_tex_dirt.load ();
    m_tex_dirt.bind(1);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR_MIPMAP_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    glTexParameterf(GL_TEXTURE_2D, GL_TEXTURE_MAX_ANISOTROPY_EXT, 8.0f);
    glGenerateMipmap(GL_TEXTURE_2D);

    m_tex_snow.load ();
    m_tex_snow.bind(2);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR_MIPMAP_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    glTexParameterf(GL_TEXTURE_2D, GL_TEXTURE_MAX_ANISOTROPY_EXT, 8.0f);
    glGenerateMipmap(GL_TEXTURE_2D);
    
    // Initialize fog color to a default value
    Vector3D defaultFogColor(0.5, 0.6, 0.8);
    set_fog_color(defaultFogColor);

    // Push the terrain geometry to the GPU
    upload_buffers();
  }

  void
  TerrainRenderer::update_shadow_map(const Vector3D& light_dir) {
    // Always update light direction for normal-based shadowing
    set_light_direction(light_dir);
    
    // Check if we have a valid shadow map
    if (m_shadow_map && m_shadow_map->is_valid()) {
      // Calculate terrain bounds
      Vector3D bounds = m_map.size();
      Vector3D center(bounds.x / 2, bounds.y / 2, bounds.z / 2);
      float radius = sqrt(bounds.x * bounds.x + bounds.y * bounds.y + bounds.z * bounds.z) / 2;
      
      m_shadow_map->update_light_position(light_dir, center, radius);
      m_shadow_map->begin_shadow_pass();
      render_for_shadow_map();
      m_shadow_map->end_shadow_pass();
    }
  }
  
  void
  TerrainRenderer::render_for_shadow_map() {
    // Check if shadow mapping is enabled
    if (!m_shadow_map || !m_shadow_map->is_valid()) {
      return; // Skip if no valid shadow map
    }
    
    gl::ScopedMatrixSaver matrix;
    
    // Use shadow program for depth pass
    m_shadow_program.use();
    
    // Pass light matrix to the shader
    m_shadow_program.set_matrix("lightMatrix", m_shadow_map->get_light_matrix());

    translate();
    draw_strips();

    m_shadow_program.unuse();
  }

  void
  TerrainRenderer::render () {
    gl::ScopedMatrixSaver matrix;

    // Enable lighting for rounded appearance
    glEnable(GL_LIGHTING);
    glEnable(GL_NORMALIZE); // Normalize normals for better lighting
    
    m_shader_program.use();

    // Bind terrain textures
    m_shader_program.set_int("grass", 0);
    m_shader_program.set_int("dirt", 1);
    m_shader_program.set_int("snow", 2);
    m_tex_grass.bind(0);
    m_tex_dirt.bind(1);
    m_tex_snow.bind(2);
    
    // Check if shadow mapping is available
    if (m_shadow_map && m_shadow_map->is_valid()) {
      // Bind shadow map texture
      m_shadow_map->bind_shadow_map();
      m_shader_program.set_int("shadowMap", 3); // Shadow map on texture unit 3
      
      // Pass light matrix to the shader
      m_shader_program.set_matrix("lightMatrix", m_shadow_map->get_light_matrix());
      
      // Pass shadow strength to the shader
      m_shader_program.set_float("shadowStrength", m_shadow_strength);
    } else {
      // Set shadow strength to 0 to use normal-based shadows in the shader
      m_shader_program.set_float("shadowStrength", 0.0f);
    }

    translate();

    // Material settings for better light reflections
    // The actual color values will be set by the dynamic lighting system
    // These are just base properties for the terrain
    static const float specular[] = { 0.6f, 0.6f, 0.6f, 1.0f };
    static const float diffuse[] = { 0.9f, 0.9f, 0.9f, 1.0f };
    static const float ambient[] = { 0.4f, 0.4f, 0.4f, 0.0f };
    
    glMaterialfv(GL_FRONT, GL_AMBIENT, ambient);
    glMaterialfv(GL_FRONT, GL_DIFFUSE, diffuse);
    glMaterialfv(GL_FRONT, GL_SPECULAR, specular);
    glMaterialf(GL_FRONT, GL_SHININESS, 60.0f);
    
    // Enable two-sided lighting for better results on steep terrain
    glLightModeli(GL_LIGHT_MODEL_TWO_SIDE, GL_TRUE);

    draw_strips();

    // Disable two-sided lighting to avoid affecting other objects
    glLightModeli(GL_LIGHT_MODEL_TWO_SIDE, GL_FALSE);
    
    // Unbind shadow map if it's being used
    if (m_shadow_map && m_shadow_map->is_valid()) {
      m_shadow_map->unbind_shadow_map();
    }
    
    m_shader_program.unuse();

//     glLineWidth (1);
//     glBegin (GL_LINES);
//     for (int y = 0; y < height - 1; ++y)
//       for (int x = 0; x < width - 1; ++x)
// 	{
// 	  gl::vertex (m_map.vertex (x, y));
// 	  gl::vertex (m_map.vertex (x, y) + Vector3D (0, 6, 0));
// 	}
//     glEnd ();
  }

  void
  TerrainRenderer::render_directly () {
    gl::ScopedMatrixSaver matrix;

    int width     = m_map.width ();
    int height    = m_map.height ();

    translate ();

    for (int y = 0; y < height - 2; ++y)
      {
	glBegin (GL_TRIANGLE_STRIP);
	for (int x = 0; x < width - 1; ++x)
	  {
	    gl::normal (m_map.normal (x, y));
	    gl::vertex (m_map.vertex (x, y));
	    gl::normal (m_map.normal (x, y + 1));
	    gl::vertex (m_map.vertex (x, y + 1));
	  }
	glEnd ();
      }
  }
  
  void
  TerrainRenderer::set_fog_color(const Vector3D& color) {
    // If the shader program is loaded, set the fog color uniform
    m_shader_program.use();
    
    GLint fogColorLoc = glGetUniformLocationARB(m_shader_program.id(), "fogColor");
    if (fogColorLoc != -1) {
      glUniform3fARB(fogColorLoc, color.x, color.y, color.z);
    }
    
    m_shader_program.unuse();
  }
  
  void
  TerrainRenderer::set_terrain_scales (float height_scale,
				       float sea_norm,
				       float fog_scale) {
    m_shader_program.use ();
    m_shader_program.set_float ("heightScale", height_scale);
    m_shader_program.set_float ("seaLevel", sea_norm);
    m_shader_program.set_float ("fogScale", fog_scale);
    m_shader_program.unuse ();
  }

  // Helper to pass current light direction to the shader for normal-based shadows
  void
  TerrainRenderer::set_light_direction(const Vector3D& light_dir) {
    m_shader_program.use();
    m_shader_program.set_vec3("sunDirection", light_dir.x, light_dir.y, light_dir.z);
    m_shader_program.unuse();
  }
}
}
