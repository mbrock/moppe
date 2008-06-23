
#include <moppe/map/generate.hh>
#include <moppe/map/tga.hh>

#include <boost/random.hpp>

#include <iostream>

namespace moppe {
namespace map {
  HeightMap::HeightMap (int width, int height, int seed)
    : m_data   (boost::extents[height][width]),
      m_width  (width),
      m_height (height)
  {
    m_rng.seed (boost::mt19937::result_type (seed));
  }

#define FORALL(x,y)				\
  for (index_t y = 0; y < m_height; ++y)	\
    for (index_t x = 0; x < m_width; ++x)

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

  void
  HeightMap::normalize ()
  {
    translate (0 - min_value ());
    float max = max_value ();
    if (max != 0.0)
      scale (1 / max);
  }

  void
  HeightMap::translate (float d)
  { FORALL (x, y) set (x, y, d + get (x, y)); }

  void
  HeightMap::scale (float k)
  { FORALL (x, y) set (x, y, k * get (x, y)); }
  
  typedef boost::variate_generator<boost::mt19937&, 
				   boost::uniform_real<> > realgen_t;

  void
  HeightMap::randomize_uniformly ()
  {
    boost::uniform_real<> range (0, 1);
    realgen_t g (m_rng, range);
    
    for (index_t y = 0; y < m_height; ++y)
      for (index_t x = 0; x < m_width; ++x)
	set (x, y, g ());
  }

  static void
  do_plasma_step (HeightMap& map, int step, int x, int y, realgen_t& g,
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
  HeightMap::randomize_plasmally (float roughness)
  {
    boost::uniform_real<> range (-1, 1);
    boost::variate_generator<boost::mt19937&, boost::uniform_real<> >
      g (m_rng, range);

    do_plasma_step (*this, 0, 0, 0, g, 1.0, std::pow (2, -roughness),
		    g (), g (), g (), g ());
    normalize ();
  }
  
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
}
}
