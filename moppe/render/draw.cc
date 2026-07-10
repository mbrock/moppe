#include <moppe/render/draw.hh>

#include <cassert>
#include <cmath>
#include <map>

namespace moppe {
namespace render {
  DrawList::DrawList ()
    : m_top (0),
      m_normal_dirty (true),
      m_normal (0, 0, 1),
      m_u (0), m_v (0),
      m_lit (true),
      m_fogged (true),
      m_wind (0),
      m_texture (0),
      m_prim (Prim::Triangles),
      m_in_begin (false)
  { }

  void
  DrawList::clear () {
    m_top = 0;
    m_stack[0] = Mat4 ();
    m_normal_dirty = true;
    m_color = Color ();
    m_normal = Vector3D (0, 0, 1);
    m_u = m_v = 0;
    m_lit = true;
    m_fogged = true;
    m_wind = 0;
    m_texture = 0;
    m_state = DrawState ();
    m_in_begin = false;
    m_prim_scratch.clear ();
    m_vertices.clear ();
    m_runs.clear ();
  }

  // -- matrix stack --------------------------------------------------

  void DrawList::push () {
    assert (m_top + 1 < MAX_DEPTH);
    m_stack[m_top + 1] = m_stack[m_top];
    ++m_top;
  }

  void DrawList::pop () {
    assert (m_top > 0);
    --m_top;
    m_normal_dirty = true;
  }

  void DrawList::load_identity () {
    m_stack[m_top] = Mat4 ();
    m_normal_dirty = true;
  }

  void DrawList::translate (const Vector3D& v) {
    m_stack[m_top] = m_stack[m_top] * Mat4::translation (v);
    m_normal_dirty = true;
  }

  void DrawList::translate (float x, float y, float z)
  { translate (Vector3D (x, y, z)); }

  void DrawList::rotate (radians_t angle, const Vector3D& axis) {
    m_stack[m_top] = m_stack[m_top] * Mat4::rotation (angle, axis);
    m_normal_dirty = true;
  }

  void DrawList::rotate_deg (degrees_t angle,
			     float ax, float ay, float az) {
    rotate (degrees_to_radians (angle), Vector3D (ax, ay, az));
  }

  void DrawList::scale (const Vector3D& v) {
    m_stack[m_top] = m_stack[m_top] * Mat4::scaling (v);
    m_normal_dirty = true;
  }

  void DrawList::scale (float x, float y, float z)
  { scale (Vector3D (x, y, z)); }

  void DrawList::mult (const Mat4& m) {
    m_stack[m_top] = m_stack[m_top] * m;
    m_normal_dirty = true;
  }

  const NormalMat&
  DrawList::normal_matrix () {
    if (m_normal_dirty) {
      m_normal_mat = NormalMat::from (m_stack[m_top]);
      m_normal_dirty = false;
    }
    return m_normal_mat;
  }

  // -- attributes ----------------------------------------------------

  void DrawList::color (float r, float g, float b, float a)
  { m_color = Color (r, g, b, a); }

  void DrawList::color (const Vector3D& c, float a)
  { m_color = Color (c.x, c.y, c.z, a); }

  void DrawList::lit (bool on)    { m_lit = on; }
  void DrawList::fogged (bool on) { m_fogged = on; }
  void DrawList::wind (float w)   { m_wind = Color::quantize (w); }

  void DrawList::set_texture (const Texture* t) { m_texture = t; }

  void DrawList::normal (const Vector3D& n) { m_normal = n; }

  void DrawList::uv (float u, float v) { m_u = u; m_v = v; }

  void DrawList::state (const DrawState& s) { m_state = s; }

  // -- geometry ------------------------------------------------------

  Vertex
  DrawList::make_vertex (float x, float y, float z) {
    const Mat4& m = m_stack[m_top];
    const Vector3D p = m.transform_point (Vector3D (x, y, z));
    const Vector3D n = normal_matrix ().apply (m_normal);

    Vertex v;
    v.px = p.x; v.py = p.y; v.pz = p.z;
    v.nx = n.x; v.ny = n.y; v.nz = n.z;
    v.u = m_u; v.v = m_v;
    v.color = m_color;
    v.lit = m_lit ? 1 : 0;
    v.fogged = m_fogged ? 1 : 0;
    v.wind = m_wind;
    v.pad1 = 0;
    return v;
  }

  void
  DrawList::flush_run_state () {
    // Open a new run unless the current one matches state+texture.
    if (!m_runs.empty ()) {
      Run& r = m_runs.back ();
      if (r.state == m_state && r.texture == m_texture)
	return;
      if (r.count == 0) {
	// Empty run: just retarget it.
	r.state = m_state;
	r.texture = m_texture;
	return;
      }
    }
    Run r;
    r.state = m_state;
    r.texture = m_texture;
    r.first = (uint32_t) m_vertices.size ();
    r.count = 0;
    m_runs.push_back (r);
  }

  void
  DrawList::emit_raw (const Vertex& v) {
    m_vertices.push_back (v);
    m_runs.back ().count++;
  }

  void
  DrawList::begin (Prim p) {
    assert (!m_in_begin);
    m_in_begin = true;
    m_prim = p;
    m_prim_scratch.clear ();
  }

  void
  DrawList::vertex (const Vector3D& v) { vertex (v.x, v.y, v.z); }

  void
  DrawList::vertex (float x, float y, float z) {
    assert (m_in_begin);
    m_prim_scratch.push_back (make_vertex (x, y, z));
  }

  void
  DrawList::end () {
    assert (m_in_begin);
    m_in_begin = false;

    const std::vector<Vertex>& s = m_prim_scratch;
    const size_t n = s.size ();
    if (n == 0)
      return;

    flush_run_state ();

    switch (m_prim) {
    case Prim::Triangles:
      for (size_t i = 0; i + 2 < n; i += 3) {
	emit_raw (s[i]); emit_raw (s[i + 1]); emit_raw (s[i + 2]);
      }
      break;

    case Prim::TriangleStrip:
      for (size_t i = 0; i + 2 < n; ++i) {
	// Flip odd triangles to keep winding consistent.
	if (i % 2 == 0) {
	  emit_raw (s[i]); emit_raw (s[i + 1]); emit_raw (s[i + 2]);
	} else {
	  emit_raw (s[i + 1]); emit_raw (s[i]); emit_raw (s[i + 2]);
	}
      }
      break;

    case Prim::TriangleFan:
    case Prim::Polygon:
      for (size_t i = 1; i + 1 < n; ++i) {
	emit_raw (s[0]); emit_raw (s[i]); emit_raw (s[i + 1]);
      }
      break;

    case Prim::Quads:
      for (size_t i = 0; i + 3 < n; i += 4) {
	emit_raw (s[i]); emit_raw (s[i + 1]); emit_raw (s[i + 2]);
	emit_raw (s[i]); emit_raw (s[i + 2]); emit_raw (s[i + 3]);
      }
      break;

    case Prim::QuadStrip:
      // Quad k is vertices (2k, 2k+1, 2k+3, 2k+2) in GL order.
      for (size_t i = 0; i + 3 < n; i += 2) {
	emit_raw (s[i]); emit_raw (s[i + 1]); emit_raw (s[i + 3]);
	emit_raw (s[i]); emit_raw (s[i + 3]); emit_raw (s[i + 2]);
      }
      break;

    case Prim::Lines:
      for (size_t i = 0; i + 1 < n; i += 2)
	emit_line (s[i], s[i + 1], 1.5f);
      break;

    case Prim::LineStrip:
      for (size_t i = 0; i + 1 < n; ++i)
	emit_line (s[i], s[i + 1], 1.5f);
      break;
    }

    m_prim_scratch.clear ();
  }

  void
  DrawList::emit_line (const Vertex& p0, const Vertex& p1, float w) {
    float dx = p1.px - p0.px, dy = p1.py - p0.py;
    const float len = std::sqrt (dx * dx + dy * dy);
    if (len < 1e-6f)
      return;
    const float hw = w * 0.5f / len;
    const float ox = -dy * hw, oy = dx * hw;

    Vertex a = p0, b = p1, c = p1, d = p0;
    a.px += ox; a.py += oy;
    b.px += ox; b.py += oy;
    c.px -= ox; c.py -= oy;
    d.px -= ox; d.py -= oy;

    emit_raw (a); emit_raw (b); emit_raw (c);
    emit_raw (a); emit_raw (c); emit_raw (d);
  }

  void
  DrawList::line (float x0, float y0, float x1, float y1, float w) {
    // Transform the endpoints before expanding the line.  This keeps
    // its width in final HUD coordinates and, crucially, makes direct
    // lines obey the same safe-area/layout transforms as vertex-based
    // geometry.
    flush_run_state ();
    emit_line (make_vertex (x0, y0, 0),
	       make_vertex (x1, y1, 0), w);
  }

  // -- solid primitives ----------------------------------------------
  //
  // Unit meshes are generated once per distinct parameter set and
  // cached as raw triangles (position + normal); recording an
  // instance transforms them by the current matrix.

  namespace {
    struct SolidVert { Vector3D p, n; };
    typedef std::vector<SolidVert> SolidMesh;

    void tri (SolidMesh& m, const SolidVert& a, const SolidVert& b,
	      const SolidVert& c) {
      m.push_back (a); m.push_back (b); m.push_back (c);
    }

    SolidMesh make_cube () {
      SolidMesh m;
      static const float f[6][3] =
	{ {1,0,0}, {-1,0,0}, {0,1,0}, {0,-1,0}, {0,0,1}, {0,0,-1} };
      for (int i = 0; i < 6; ++i) {
	const Vector3D n (f[i][0], f[i][1], f[i][2]);
	// Build a tangent frame per face.
	const Vector3D u = (std::fabs (n.y) > 0.9f)
	  ? Vector3D (1, 0, 0) : Vector3D (0, 1, 0);
	const Vector3D s = n.cross (u).normalized ();
	const Vector3D t = n.cross (s);
	const Vector3D c = n * 0.5f;
	SolidVert v00 = { c - s * 0.5f - t * 0.5f, n };
	SolidVert v10 = { c + s * 0.5f - t * 0.5f, n };
	SolidVert v11 = { c + s * 0.5f + t * 0.5f, n };
	SolidVert v01 = { c - s * 0.5f + t * 0.5f, n };
	tri (m, v00, v10, v11);
	tri (m, v00, v11, v01);
      }
      return m;
    }

    SolidMesh make_sphere (int slices, int stacks) {
      SolidMesh m;
      for (int i = 0; i < stacks; ++i) {
	const float a0 = PI * (float) i / stacks - PI / 2;
	const float a1 = PI * (float) (i + 1) / stacks - PI / 2;
	for (int j = 0; j < slices; ++j) {
	  const float b0 = PI2 * (float) j / slices;
	  const float b1 = PI2 * (float) (j + 1) / slices;
	  const Vector3D p00 (std::cos (a0) * std::cos (b0),
			      std::sin (a0),
			      std::cos (a0) * std::sin (b0));
	  const Vector3D p10 (std::cos (a0) * std::cos (b1),
			      std::sin (a0),
			      std::cos (a0) * std::sin (b1));
	  const Vector3D p01 (std::cos (a1) * std::cos (b0),
			      std::sin (a1),
			      std::cos (a1) * std::sin (b0));
	  const Vector3D p11 (std::cos (a1) * std::cos (b1),
			      std::sin (a1),
			      std::cos (a1) * std::sin (b1));
	  SolidVert v00 = { p00, p00 }, v10 = { p10, p10 };
	  SolidVert v01 = { p01, p01 }, v11 = { p11, p11 };
	  tri (m, v00, v11, v10);
	  tri (m, v00, v01, v11);
	}
      }
      return m;
    }

    SolidMesh make_cone (int slices, int stacks) {
      // Unit cone: base radius 1 at z=0, apex at z=1, closed base.
      SolidMesh m;
      // Side normal: for a cone the slope normal is
      // normalize(cos(b), sin(b), r/h) with r/h = 1 here.
      const float nz = 1.0f / std::sqrt (2.0f);
      for (int i = 0; i < stacks; ++i) {
	const float z0 = (float) i / stacks;
	const float z1 = (float) (i + 1) / stacks;
	const float r0 = 1.0f - z0, r1 = 1.0f - z1;
	for (int j = 0; j < slices; ++j) {
	  const float b0 = PI2 * (float) j / slices;
	  const float b1 = PI2 * (float) (j + 1) / slices;
	  const Vector3D n0 (std::cos (b0) * nz, std::sin (b0) * nz, nz);
	  const Vector3D n1 (std::cos (b1) * nz, std::sin (b1) * nz, nz);
	  SolidVert v00 = { Vector3D (r0 * std::cos (b0),
				      r0 * std::sin (b0), z0), n0 };
	  SolidVert v10 = { Vector3D (r0 * std::cos (b1),
				      r0 * std::sin (b1), z0), n1 };
	  SolidVert v01 = { Vector3D (r1 * std::cos (b0),
				      r1 * std::sin (b0), z1), n0 };
	  SolidVert v11 = { Vector3D (r1 * std::cos (b1),
				      r1 * std::sin (b1), z1), n1 };
	  tri (m, v00, v10, v11);
	  tri (m, v00, v11, v01);
	}
      }
      // Base cap at z=0, normal -z.
      const Vector3D bn (0, 0, -1);
      for (int j = 0; j < slices; ++j) {
	const float b0 = PI2 * (float) j / slices;
	const float b1 = PI2 * (float) (j + 1) / slices;
	SolidVert c = { Vector3D (0, 0, 0), bn };
	SolidVert e0 = { Vector3D (std::cos (b0), std::sin (b0), 0), bn };
	SolidVert e1 = { Vector3D (std::cos (b1), std::sin (b1), 0), bn };
	tri (m, c, e0, e1);
      }
      return m;
    }

    SolidMesh make_torus (float inner, float outer,
			  int sides, int rings) {
      // GLUT convention: `inner` is the tube radius, `outer` the
      // spine radius; the torus wraps the Z axis.  Not unit-cached:
      // the two radii shape the mesh, so bake them in.
      SolidMesh m;
      for (int i = 0; i < rings; ++i) {
	const float a0 = PI2 * (float) i / rings;
	const float a1 = PI2 * (float) (i + 1) / rings;
	for (int j = 0; j < sides; ++j) {
	  const float b0 = PI2 * (float) j / sides;
	  const float b1 = PI2 * (float) (j + 1) / sides;

	  struct Gen {
	    static SolidVert at (float a, float b, float inr, float outr) {
	      const Vector3D spine (std::cos (a), std::sin (a), 0);
	      const Vector3D n = spine * std::cos (b)
		+ Vector3D (0, 0, 1) * std::sin (b);
	      SolidVert v = { spine * outr + n * inr, n };
	      return v;
	    }
	  };

	  SolidVert v00 = Gen::at (a0, b0, inner, outer);
	  SolidVert v10 = Gen::at (a1, b0, inner, outer);
	  SolidVert v01 = Gen::at (a0, b1, inner, outer);
	  SolidVert v11 = Gen::at (a1, b1, inner, outer);
	  tri (m, v00, v10, v11);
	  tri (m, v00, v11, v01);
	}
      }
      return m;
    }
  }

  namespace {
    // Record a cached solid through the normal begin/end path; the
    // caller sets up any pre-scale on the matrix stack.
    void record_solid (DrawList& dl, const SolidMesh& mesh) {
      dl.begin (Prim::Triangles);
      for (size_t i = 0; i < mesh.size (); ++i) {
	dl.normal (mesh[i].n);
	dl.vertex (mesh[i].p);
      }
      dl.end ();
    }
  }

  void
  DrawList::cube (float size) {
    static SolidMesh mesh = make_cube ();
    push ();
    scale (Vector3D (size, size, size));
    record_solid (*this, mesh);
    pop ();
  }

  void
  DrawList::sphere (float radius, int slices, int stacks) {
    static std::map<std::pair<int, int>, SolidMesh> cache;
    SolidMesh& mesh = cache[std::make_pair (slices, stacks)];
    if (mesh.empty ())
      mesh = make_sphere (slices, stacks);

    push ();
    scale (Vector3D (radius, radius, radius));
    record_solid (*this, mesh);
    pop ();
  }

  void
  DrawList::cone (float base, float height, int slices, int stacks) {
    static std::map<std::pair<int, int>, SolidMesh> cache;
    SolidMesh& mesh = cache[std::make_pair (slices, stacks)];
    if (mesh.empty ())
      mesh = make_cone (slices, stacks);

    push ();
    scale (Vector3D (base, base, height));
    record_solid (*this, mesh);
    pop ();
  }

  void
  DrawList::torus (float inner, float outer, int sides, int rings) {
    struct Key {
      float i, o; int s, r;
      bool operator < (const Key& k) const {
	if (i != k.i) return i < k.i;
	if (o != k.o) return o < k.o;
	if (s != k.s) return s < k.s;
	return r < k.r;
      }
    };
    static std::map<Key, SolidMesh> cache;
    Key key = { inner, outer, sides, rings };
    SolidMesh& mesh = cache[key];
    if (mesh.empty ())
      mesh = make_torus (inner, outer, sides, rings);

    record_solid (*this, mesh);
  }
}
}
