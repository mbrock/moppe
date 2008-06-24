
#include <moppe/app/gl.hh>
#include <moppe/gfx/terrain.hh>

namespace moppe {
namespace gfx {
  NormalMap::NormalMap (int width, int height)
    : m_data   (boost::extents[height][width]),
      m_width  (width),
      m_height (height)
  {
    reset ();
  }

  void
  NormalMap::reset ()
  {
    for (index_t y = 0; y < m_height; ++y)
      for (index_t x = 0; x < m_width; ++x)
	m_data[y][x] = Vector3D (0, 0, 0);
  }

  void
  NormalMap::add (int x, int y, const Vector3D& v)
  {
    if (((x < 0) || (x > m_width - 1) ||
	 (y < 0) || (y > m_height - 1)))
      return;

    m_data[y][x] += v;
  }

  void
  NormalMap::normalize_all ()
  {
    for (index_t y = 0; y < m_height; ++y)
      for (index_t x = 0; x < m_width; ++x)
	m_data[y][x].normalize ();
  }

  TerrainRenderer::TerrainRenderer (const map::HeightMap& map,
				    float scale_w,
				    float scale_h)
    : m_map (map),
      m_scale_w (scale_w),
      m_scale_h (scale_h),
      m_normals (m_map.width (), m_map.height ())
  {
    recalculate_normals ();
  }

  void
  TerrainRenderer::render () {
    int width  = m_map.width ();
    int height = m_map.height ();

    gl::ScopedMatrixSaver matrix;

    glTranslatef (-0.5 * m_scale_w * width, 0,
		  -0.5 * m_scale_w * height);

    for (int y = 0; y < height - 2; y++)
      {
	glBegin (GL_TRIANGLE_STRIP);
	for (int x = 0; x < width - 1; x++)
	  {
	    Vector3D av (vertex (x, y));
	    Vector3D bv (vertex (x, y + 1));

	    Vector3D an (normal (x, y));
	    Vector3D bn (normal (x, y + 1));

	    gl::normal (an);
	    gl::vertex (av);
	    gl::normal (bn);
	    gl::vertex (bv);
	  }
	glEnd ();
      }
  }

  void
  TerrainRenderer::recalculate_normals ()
  {
    m_normals.reset ();

    for (int y = 0; y < m_map.height () - 1; ++y)
      for (int x = 0; x < m_map.width () - 1; ++x)
	{
	  Vector3D left =
	    triangle_normal (x, y,
			     x, y + 1,
			     x + 1, y + 1);
	  Vector3D right =
	    triangle_normal (x, y,
			     x + 1, y + 1,
			     x + 1, y);

	  m_normals.add (x, y, left);
	  m_normals.add (x, y + 1, left);
	  m_normals.add (x + 1, y + 1, left);

	  m_normals.add (x, y, right);
	  m_normals.add (x + 1, y, right);
	  m_normals.add (x + 1, y + 1, right);
	}

    m_normals.normalize_all ();
  }

  Vector3D
  TerrainRenderer::triangle_normal (int x1, int y1,
				    int x2, int y2,
				    int x3, int y3) const
  {
    Vector3D a = vertex (x1, y1);
    Vector3D b = vertex (x2, y2);
    Vector3D c = vertex (x3, y3);

    return (b - a).cross (c - a).normalized ();
  }

  Vector3D
  TerrainRenderer::vertex (int x, int y) const
  {
    return Vector3D (m_scale_w * x,
		     m_scale_h * m_map.get (x, y),
		     m_scale_w * y);    
  }

  Vector3D
  TerrainRenderer::normal (int x, int y) const
  {
    return m_normals.at (x, y);
  }
}
}
