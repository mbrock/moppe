#ifndef MOPPE_TERRAIN_HH
#define MOPPE_TERRAIN_HH

#include <moppe/map/generate.hh>

namespace moppe {
namespace gfx {
  class NormalMap {
  public:
    NormalMap (int width, int height);

    inline const Vector3D& at (int x, int y) const
    { return m_data[y][x]; }

    void reset         ();
    void add           (int x, int y, const Vector3D& v);
    void normalize_all ();

  private:
    typedef boost::multi_array<Vector3D, 2> array_t;
    typedef array_t::index                  index_t;

    array_t m_data;

    const int m_width;
    const int m_height;
  };

  class TerrainRenderer {
  public:
    TerrainRenderer (const map::HeightMap& map,
		     float scale_w,
		     float scale_h);

    void recalculate_normals ();
    void render              ();

  private:
    Vector3D vertex          (int x, int y)   const;
    Vector3D normal          (int x, int y)   const;
    Vector3D triangle_normal (int x1, int y1,
			      int x2, int y2,
			      int x3, int y3) const;

  private:
    const map::HeightMap& m_map;

    const float m_scale_w;
    const float m_scale_h;

    NormalMap m_normals;
  };
}
}

#endif
