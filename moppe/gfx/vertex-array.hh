#ifndef MOPPE_VARRAY_HH
#define MOPPE_VARRAY_HH

#include <moppe/gfx/math.hh>

#include <vector>

namespace moppe {
namespace gfx {
  class VertexArray {
  public:
    void reserve (int n) 
    { m_vector.reserve (m_vector.size () + n * 3); }

    inline void add (const Vector3D& v) {
      m_vector.push_back (v.x);
      m_vector.push_back (v.y);
      m_vector.push_back (v.z);
    }

    inline void clear ()
    { m_vector.clear (); }

    inline const float * address (int offset) const
    { return &m_vector[offset * 3]; }

    inline int size () const { return m_vector.size (); }

  private:
    std::vector<float> m_vector;
  };

  void render_vertex_arrays (int mode,
			     int from,
			     int count,
			     const VertexArray& vertices,
			     const VertexArray& normals);
}
}

#endif
