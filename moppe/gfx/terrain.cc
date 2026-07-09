
#include <moppe/app/gl.hh>
#include <moppe/gfx/terrain.hh>

#include <algorithm>
#include <cmath>
#include <iostream>

namespace moppe {
namespace gfx {
  using namespace moppe::gl;

  TerrainRenderer::TerrainRenderer (const map::HeightMap& map)
    : m_map (map),
      m_vbo_vertices (0),
      m_vbo_normals (0),
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

    int width      = m_map.width ();
    int height     = m_map.height ();

    m_vertices.reserve ((height - 2) * (width - 1));
    m_normals.reserve ((height - 2) * (width - 1));

    // Texture coordinates are derived from position in the vertex
    // shader (uniform texScale), so no texcoord stream is stored.

    for (int y = 0; y < height - 2; ++y)
      for (int x = 0; x < width - 1; ++x)
	{
	  m_normals.add (m_map.normal (x, y));
	  m_vertices.add (m_map.vertex (x, y));
	  m_normals.add (m_map.normal (x, y + 1));
	  m_vertices.add (m_map.vertex (x, y + 1));
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
      }

    glBindBuffer (GL_ARRAY_BUFFER, m_vbo_vertices);
    glBufferData (GL_ARRAY_BUFFER, m_vertices.size () * sizeof (float),
		  m_vertices.address (0), GL_STATIC_DRAW);
    glBindBuffer (GL_ARRAY_BUFFER, m_vbo_normals);
    glBufferData (GL_ARRAY_BUFFER, m_normals.size () * sizeof (float),
		  m_normals.address (0), GL_STATIC_DRAW);
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

    // Build culling chunks: ~128x128-cell blocks with bounding
    // spheres.  Adjacent chunks share a boundary column so strips
    // remain seamless.
    const int CHUNK = 128;
    const int pairs = width - 1;   // vertex pairs per strip row
    const int strips = height - 2; // strip rows
    const Vector3D scale = m_map.scale ();

    m_chunks.clear ();
    for (int r0 = 0; r0 < strips; r0 += CHUNK)
      {
	const int r1 = std::min (r0 + CHUNK - 1, strips - 1);
	for (int p0 = 0; p0 + 1 < pairs; p0 += CHUNK)
	  {
	    const int p1 = std::min (p0 + CHUNK, pairs - 1);

	    float ymin = 1e9f, ymax = -1e9f;
	    for (int y = r0; y <= r1 + 1; ++y)
	      for (int x = p0; x <= p1; ++x)
		{
		  const float h = m_map.get (x, y);
		  ymin = std::min (ymin, h);
		  ymax = std::max (ymax, h);
		}
	    ymin *= scale.y;
	    ymax *= scale.y;

	    Chunk c;
	    c.row0 = r0;
	    c.row1 = r1;
	    c.pair0 = p0;
	    c.pair1 = p1;

	    const float x0 = p0 * scale.x, x1 = p1 * scale.x;
	    const float z0 = r0 * scale.z, z1 = (r1 + 1) * scale.z;
	    c.center = Vector3D ((x0 + x1) / 2, (ymin + ymax) / 2,
				 (z0 + z1) / 2);
	    const float hx = (x1 - x0) / 2, hy = (ymax - ymin) / 2,
	                hz = (z1 - z0) / 2;
	    c.radius = std::sqrt (hx * hx + hy * hy + hz * hz);

	    m_chunks.push_back (c);
	  }
      }

    m_draw_first.reserve (m_strip_first.size () * 18);
    m_draw_count.reserve (m_strip_first.size () * 18);
  }

  void
  TerrainRenderer::bind_arrays () {
    glEnableClientState (GL_VERTEX_ARRAY);
    glEnableClientState (GL_NORMAL_ARRAY);

    glBindBuffer (GL_ARRAY_BUFFER, m_vbo_vertices);
    glVertexPointer (3, GL_FLOAT, 0, 0);
    glBindBuffer (GL_ARRAY_BUFFER, m_vbo_normals);
    glNormalPointer (GL_FLOAT, 0, 0);
    glBindBuffer (GL_ARRAY_BUFFER, 0);
  }

  void
  TerrainRenderer::unbind_arrays () {
    glDisableClientState (GL_VERTEX_ARRAY);
    glDisableClientState (GL_NORMAL_ARRAY);
  }

  void
  TerrainRenderer::draw_all_strips () {
    bind_arrays ();
    glMultiDrawArrays (GL_TRIANGLE_STRIP,
		       &m_strip_first[0], &m_strip_count[0],
		       (GLsizei) m_strip_first.size ());
    unbind_arrays ();
  }

  void
  TerrainRenderer::draw_culled_strips (const Vector3D& cam,
				       const Vector3D& view_dir,
				       float max_dist) {
    const int pairs = m_map.width () - 1;

    m_draw_first.clear ();
    m_draw_count.clear ();

    for (size_t i = 0; i < m_chunks.size (); ++i)
      {
	const Chunk& c = m_chunks[i];
	const Vector3D d = c.center - cam;
	const float dist2 = d.length2 ();

	// too far: the haze has swallowed it
	const float reach = max_dist + c.radius;
	if (dist2 > reach * reach)
	  continue;

	// entirely behind the camera plane (conservative: keeps
	// anything whose sphere pokes past the eye)
	if (dist2 > c.radius * c.radius &&
	    d.dot (view_dir) < -c.radius)
	  continue;

	const GLsizei count = 2 * (c.pair1 - c.pair0 + 1);
	for (int row = c.row0; row <= c.row1; ++row)
	  {
	    m_draw_first.push_back (2 * (row * pairs + c.pair0));
	    m_draw_count.push_back (count);
	  }
      }

    if (m_draw_first.empty ())
      return;

    bind_arrays ();
    glMultiDrawArrays (GL_TRIANGLE_STRIP,
		       &m_draw_first[0], &m_draw_count[0],
		       (GLsizei) m_draw_first.size ());
    unbind_arrays ();
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

    // Constant uniforms: texture units never change
    m_shader_program.use();
    m_shader_program.set_int("grass", 0);
    m_shader_program.set_int("dirt", 1);
    m_shader_program.set_int("snow", 2);
    m_shader_program.set_int("shadowMap", 3);
    m_shader_program.unuse();

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

      // The sun is fixed, so the lookup matrix and strength are
      // constants from here on: push them to the terrain shader once
      m_shader_program.use();
      m_shader_program.set_matrix("lightMatrix",
                                  m_shadow_map->get_light_matrix());
      m_shader_program.set_float("shadowStrength", m_shadow_strength);
      m_shader_program.unuse();
    } else {
      m_shader_program.use();
      m_shader_program.set_float("shadowStrength", 0.0f);
      m_shader_program.unuse();
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

    // Rasterize from the light: the un-biased NDC matrix
    m_shadow_program.set_matrix("lightMatrix",
                                m_shadow_map->get_light_matrix_ndc());

    translate();
    draw_all_strips();

    m_shadow_program.unuse();
  }

  void
  TerrainRenderer::render (const Vector3D& cam,
			   const Vector3D& view_dir,
			   float max_dist) {
    gl::ScopedMatrixSaver matrix;
    gl::ScopedAttribSaver attribs (GL_ENABLE_BIT);

    glEnable(GL_LIGHTING);

    // Terrain is opaque: skipping blending saves framebuffer
    // read-modify-write across most of the screen
    glDisable(GL_BLEND);

    m_shader_program.use();

    // Bind terrain textures (unit 0 is reused by post effects, so
    // rebinding each frame is required)
    m_tex_grass.bind(0);
    m_tex_dirt.bind(1);
    m_tex_snow.bind(2);
    if (m_shadow_map && m_shadow_map->is_valid())
      m_shadow_map->bind_shadow_map();

    translate();

    // The shader reads these material/light products per pixel
    static const float diffuse[] = { 0.9f, 0.9f, 0.9f, 1.0f };
    static const float ambient[] = { 0.4f, 0.4f, 0.4f, 0.0f };
    glMaterialfv(GL_FRONT, GL_AMBIENT, ambient);
    glMaterialfv(GL_FRONT, GL_DIFFUSE, diffuse);

    draw_culled_strips(cam, view_dir, max_dist);

    if (m_shadow_map && m_shadow_map->is_valid())
      m_shadow_map->unbind_shadow_map();

    m_shader_program.unuse();
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
    // Texture repeat per world meter (was a stored texcoord stream)
    m_shader_program.set_float ("texScale",
				0.5f * one_meter / m_map.scale ().x);
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
