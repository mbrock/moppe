
#include <moppe/map/generate.hh>
#include <moppe/map/tga.hh>
#include <moppe/gfx/math.hh>

#include <boost/random.hpp>

#include <iostream>

namespace moppe {
namespace map {
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

  RandomHeightMap::RandomHeightMap (int width, int height,
				    const Vector3D& scale,
				    int seed)
    : NormalComputingHeightMap (width, height, scale),
      m_data    (boost::extents[height][width])
  {
    m_rng.seed (boost::mt19937::result_type (seed));
  }

#define FORALL(x,y)				\
  for (int y = 0; y < m_height; ++y)	\
    for (int x = 0; x < m_width; ++x)

  float
  HeightMap::min_value () const
  {
    float min = get (0, 0);

    FORALL (x, y)
      {
	float v = get (x, y);
	min = (v < min) ? v : min;
      }

    return min;
  }

  float
  HeightMap::max_value () const
  {
    float max = get (0, 0);

    FORALL (x, y)
      {
	float v = get (x, y);
	max = (v > max) ? v : max;
      }

    return max;
  }

  Vector3D
  HeightMap::vertex (int x, int y) const
  {
    return Vector3D (m_scale.x * x,
		     m_scale.y * get (x, y),
		     m_scale.z * y);    
  }

  Vector3D
  HeightMap::triangle_normal (int x1, int y1,
			      int x2, int y2,
			      int x3, int y3) const
  {
    Vector3D a = vertex (x1, y1);
    Vector3D b = vertex (x2, y2);
    Vector3D c = vertex (x3, y3);

    return (b - a).cross (c - a).normalized ();
  }

  void
  RandomHeightMap::normalize ()
  {
    translate (0 - min_value ());
    float max = max_value ();
    if (max != 0.0)
      rescale (1 / max);
  }

  void
  RandomHeightMap::translate (float d)
  { FORALL (x, y) set (x, y, d + get (x, y)); }

  void
  RandomHeightMap::rescale (float k)
  { FORALL (x, y) set (x, y, k * get (x, y)); }
  
  typedef boost::variate_generator<boost::mt19937&, 
				   boost::uniform_real<> > realgen_t;

  void
  RandomHeightMap::randomize_uniformly ()
  {
    boost::uniform_real<> range (0, 1);
    realgen_t g (m_rng, range);
    
    for (index_t y = 0; y < m_height; ++y)
      for (index_t x = 0; x < m_width; ++x)
	set (x, y, g ());

    recompute_normals ();
  }

  static void
  do_plasma_step (RandomHeightMap& map, int step, int x, int y, realgen_t& g,
		  float max_displacement, float r,
		  float nw_v, float ne_v, float sw_v, float se_v)
  {
    const int side = (map.width ()) / (1 << step);

    const int nw_y = y + 0,       nw_x = x + 0;
    const int n_y  = y + 0,        n_x = x + side / 2;
    const int w_y  = y + side / 2, w_x = x + 0;
    const int c_y  = y + side / 2, c_x = x + side / 2;

    if (side == 1)
      {
	map.set (x, y, 0.25 * (nw_v + ne_v + sw_v + se_v));
	return;
      }

    const float d = g () * max_displacement;
    const float m = max_displacement * r;

    const float c_v = 0.25 * (nw_v + ne_v + sw_v + se_v) + d;
    const float w_v = 0.5 * (nw_v + sw_v);
    const float e_v = 0.5 * (ne_v + se_v);
    const float n_v = 0.5 * (nw_v + ne_v);
    const float s_v = 0.5 * (sw_v + se_v);

    do_plasma_step (map, step + 1, nw_x, nw_y, g, m, r,
		    nw_v, n_v, w_v, c_v);
    do_plasma_step (map, step + 1, n_x, n_y, g, m, r,
		    n_v, ne_v, c_v, e_v);
    do_plasma_step (map, step + 1, w_x, w_y, g, m, r,
		    w_v, c_v, sw_v, s_v);
    do_plasma_step (map, step + 1, c_x, c_y, g, m, r,
		    c_v, e_v, s_v, se_v);
  }

  void
  RandomHeightMap::randomize_plasmally (float roughness)
  {
    boost::uniform_real<> range (-1, 1);
    boost::variate_generator<boost::mt19937&, boost::uniform_real<> >
      g (m_rng, range);

    do_plasma_step (*this, 0, 0, 0, g, 1.0, std::pow (2, -roughness),
		    g (), g (), g (), g ());
    normalize ();
    recompute_normals ();
  }

  float
  InterpolatingHeightMap::get (int x, int y) const
  {
    return linear_interpolate (m_from->get (x, y),
			       m_to->get (x, y),
			       m_alpha);
  }

  Vector3D
  InterpolatingHeightMap::normal (int x, int y) const
  {
    // Maybe QUATERNION interpolation would be BETTER?
    return linear_vector_interpolate (m_from->normal (x, y),
				      m_to->normal (x, y),
				      m_alpha);
  }

  void
  InterpolatingHeightMap::set_blending_factor (float alpha)
  { m_alpha = min (alpha, 1.0f); }

  void 
  InterpolatingHeightMap::increase_blending_factor (float delta)
  { m_alpha = min (m_alpha + delta, 1.0f); }
  
  void
  write_tga (std::ostream& stream, const HeightMap& map)
  {
    int w = map.width ();
    int h = map.height ();

    std::vector<char> data;
    data.reserve (w * h);

    for (int y = 0; y < h; ++y)
      for (int x = 0; x < w; ++x)
	{
	  char v = map.get (x, y) * 255.0;
	  data.push_back (v);
	}

    tga::write_gray8 (stream, data, w, h);
  }

  void
  compute_normal_map (const HeightMap& height_map,
		      NormalMap& normal_map)
  {
    normal_map.reset ();

    for (int y = 0; y < height_map.height () - 1; ++y)
      for (int x = 0; x < height_map.width () - 1; ++x)
	{
	  Vector3D left =
	    height_map.triangle_normal (x, y,
					x, y + 1,
					x + 1, y + 1);
	  Vector3D right =
	    height_map.triangle_normal (x, y,
					x + 1, y + 1,
					x + 1, y);

	  normal_map.add (x, y, left);
	  normal_map.add (x, y + 1, left);
	  normal_map.add (x + 1, y + 1, left);

	  normal_map.add (x, y, right);
	  normal_map.add (x + 1, y, right);
	  normal_map.add (x + 1, y + 1, right);
	}

    normal_map.normalize_all ();
  }
}
}
