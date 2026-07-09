#ifndef MOPPE_TERRAIN_HH
#define MOPPE_TERRAIN_HH

#include <moppe/map/generate.hh>
#include <moppe/gfx/vertex-array.hh>
#include <moppe/gfx/shader.hh>
#include <moppe/gfx/texture.hh>
#include <moppe/gfx/shadow.hh>

#include <moppe/app/gl.hh>

#include <boost/scoped_array.hpp>
#include <boost/shared_ptr.hpp>
#include <memory>
#include <vector>

namespace moppe {
namespace gfx {
  class TerrainRenderer {
  public:
    TerrainRenderer (const map::HeightMap& map);
    ~TerrainRenderer();

    void setup_shader();
    void regenerate();

    void translate();
    // Draws only chunks within max_dist of the camera and not
    // entirely behind it
    void render(const Vector3D& cam, const Vector3D& view_dir,
                float max_dist);
    void render_for_shadow_map(); // Render just the geometry for shadow map
    void render_directly();
    
    // Set the fog color for the shader
    void set_fog_color(const Vector3D& color);

    // Tell the shader the world's vertical scale, normalized sea
    // level, and haze density so the rock/snow/beach bands and fog
    // land at sensible altitudes for any world size
    void set_terrain_scales(float height_scale, float sea_norm,
                            float fog_scale);
    
    // Shadow support
    void update_shadow_map(const Vector3D& light_dir);
    void set_shadow_strength(float strength) { m_shadow_strength = strength; }
    float get_shadow_strength() const { return m_shadow_strength; }
    void set_light_direction(const Vector3D& light_dir);

  private:
    void upload_buffers ();
    void draw_all_strips ();
    void draw_culled_strips (const Vector3D& cam,
                             const Vector3D& view_dir,
                             float max_dist);
    void bind_arrays ();
    void bind_arrays_lo ();
    void unbind_arrays ();

    // A block of terrain (~128x128 cells) with a bounding sphere,
    // the granularity of per-frame culling and LOD selection
    struct Chunk {
      Vector3D center;
      float radius;
      int row0, row1;       // fine strip rows, inclusive
      int pair0, pair1;     // fine vertex-pair columns, inclusive
      int lo_row0, lo_row1; // the same block in the coarse mesh
      int lo_pair0, lo_pair1;
    };

  private:
    const map::HeightMap& m_map;

    VertexArray m_vertices;
    VertexArray m_normals;
    VertexArray m_vertices_lo; // 4x decimated mesh for far chunks
    VertexArray m_normals_lo;

    // Static terrain geometry lives on the GPU
    GLuint m_vbo_vertices;
    GLuint m_vbo_normals;
    GLuint m_vbo_vertices_lo;
    GLuint m_vbo_normals_lo;
    std::vector<GLint>   m_strip_first;
    std::vector<GLsizei> m_strip_count;

    std::vector<Chunk>   m_chunks;
    std::vector<GLint>   m_draw_first; // per-frame scratch
    std::vector<GLsizei> m_draw_count;
    std::vector<GLint>   m_draw_first_lo;
    std::vector<GLsizei> m_draw_count_lo;

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
