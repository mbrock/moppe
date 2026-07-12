#ifndef MOPPE_RENDER_DRAW_HH
#define MOPPE_RENDER_DRAW_HH

#include <moppe/gfx/mat4.hh>
#include <moppe/gfx/math.hh>
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
        const Texture* texture; // null = untextured (white)
        uint32_t first;         // vertex offset
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
      void rotate (radians_t angle, float ax, float ay, float az);
      void scale (const Vector3D& v);
      void scale (float x, float y, float z);
      void mult (const Mat4& m);
      const Mat4& matrix () const {
        return m_stack[m_top];
      }

      // -- attribute / state -------------------------------------------
      void color (float r, float g, float b, float a = 1.0f);
      void color (const Vector3D& c, float a = 1.0f);
      void lit (bool on);
      void fogged (bool on);
      // Wind sway weight for subsequent vertices (0 = anchored, 1 =
      // full amplitude); the scene vertex shader animates it.
      void wind (proportion_t w);
      // Marks full-geometry grass so the shared scene shader can apply thin-
      // blade normals, root occlusion, backlighting, and restrained highlights.
      void grass (bool on);
      void set_texture (const Texture* t);
      void normal (const Vector3D& n);
      void uv (float u, float v);
      void state (const DrawState& s);
      DrawState& state () {
        return m_state;
      }

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

      // Splice another recorded list into this one verbatim: a bulk
      // vertex copy plus run fixups, no re-transformation.  This is how
      // geometry baked once (e.g. the static parts of the HUD) replays
      // per frame without paying record cost again.  The other list's
      // vertices are already world-space, so the matrix stack does not
      // apply; adjacent compatible runs coalesce.
      void append (const DrawList& baked);

      // -- results -----------------------------------------------------
      const std::vector<Vertex>& vertices () const {
        return m_vertices;
      }
      const std::vector<Run>& runs () const {
        return m_runs;
      }
      bool empty () const {
        return m_vertices.empty ();
      }

    private:
      void emit_line (const Vertex& a, const Vertex& b, float width);
      void flush_run_state ();
      Vertex make_vertex (float x, float y, float z);
      const NormalMat& normal_matrix ();
      const Vector3D& world_normal ();

      static const int MAX_DEPTH = 32;
      Mat4 m_stack[MAX_DEPTH];
      int m_top;
      NormalMat m_normal_mat;
      bool m_normal_dirty;

      Color m_color;
      Vector3D m_normal;
      Vector3D m_world_normal; // m_normal through the normal matrix
      bool m_world_normal_dirty;
      float m_u, m_v;
      bool m_lit;
      bool m_fogged;
      uint8_t m_wind;
      bool m_grass;
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
