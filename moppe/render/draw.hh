#ifndef MOPPE_RENDER_DRAW_HH
#define MOPPE_RENDER_DRAW_HH

#include <moppe/gfx/math.hh>
#include <moppe/gfx/mat4.hh>
#include <moppe/render/types.hh>

#include <vector>

namespace moppe {
namespace render {
  // Immediate-mode recorder: the port of glBegin/glEnd, the matrix
  // stack, glColor, and the glutSolid* primitives.  Vertices are
  // transformed to world space on the CPU at record time (normals by
  // the inverse-transpose, then normalized, matching fixed-function
  // GL with GL_NORMALIZE).  Consecutive geometry with identical
  // state/texture coalesces into a single run; backends replay the
  // runs from one streamed vertex buffer.
  //
  // The same recorder bakes retained meshes: record once, hand to
  // Renderer::create_mesh.
  class DrawList {
  public:
    struct Run {
      DrawState state;
      const Texture* texture;   // null = untextured (white)
      uint32_t first;           // vertex offset
      uint32_t count;
    };

    DrawList ();

    void clear ();

    // -- matrix stack ------------------------------------------------
    void push ();
    void pop ();
    void load_identity ();
    void translate (const Vector3D& v);
    void translate (float x, float y, float z);
    void rotate (radians_t angle, const Vector3D& axis);
    void rotate_deg (degrees_t angle, float ax, float ay, float az);
    void scale (const Vector3D& v);
    void scale (float x, float y, float z);
    void mult (const Mat4& m);
    const Mat4& matrix () const { return m_stack[m_top]; }

    // -- attribute / state -------------------------------------------
    void color (float r, float g, float b, float a = 1.0f);
    void color (const Vector3D& c, float a = 1.0f);
    void lit (bool on);
    void fogged (bool on);
    void set_texture (const Texture* t);
    void normal (const Vector3D& n);
    void uv (float u, float v);
    void state (const DrawState& s);
    DrawState& state () { return m_state; }

    // -- geometry ----------------------------------------------------
    void begin (Prim p);
    void vertex (const Vector3D& v);
    void vertex (float x, float y, float z = 0.0f);
    void end ();

    // Solid primitives, matching the glutSolid* conventions:
    // cube centered at origin; lat/long sphere; cone along +Z with
    // its base at z=0 (closed); torus around the Z axis where
    // `inner` is the tube radius and `outer` the spine radius.
    void cube (float size);
    void sphere (float radius, int slices, int stacks);
    void cone (float base, float height, int slices, int stacks);
    void torus (float inner, float outer, int sides, int rings);

    // 2D convenience for the HUD (records in the z=0 plane): a thick
    // line segment as a quad.  Endpoints honor the matrix stack; width
    // is measured after transformation in final HUD coordinates.
    void line (float x0, float y0, float x1, float y1, float width);

    // -- results -----------------------------------------------------
    const std::vector<Vertex>& vertices () const { return m_vertices; }
    const std::vector<Run>& runs () const { return m_runs; }
    bool empty () const { return m_vertices.empty (); }

  private:
    void emit_line (const Vertex& a, const Vertex& b, float width);
    void emit_raw (const Vertex& v);   // no matrix transform (prebaked)
    void flush_run_state ();
    Vertex make_vertex (float x, float y, float z);
    const NormalMat& normal_matrix ();

    static const int MAX_DEPTH = 32;
    Mat4 m_stack[MAX_DEPTH];
    int m_top;
    NormalMat m_normal_mat;
    bool m_normal_dirty;

    Color m_color;
    Vector3D m_normal;
    float m_u, m_v;
    bool m_lit;
    bool m_fogged;
    const Texture* m_texture;
    DrawState m_state;

    Prim m_prim;
    bool m_in_begin;
    std::vector<Vertex> m_prim_scratch;

    std::vector<Vertex> m_vertices;
    std::vector<Run> m_runs;
  };
}
}

#endif
