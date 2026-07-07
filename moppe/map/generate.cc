
#include <moppe/map/generate.hh>
#include <moppe/map/tga.hh>
#include <moppe/gfx/math.hh>

#include <boost/random.hpp>

#include <cmath>
#include <fstream>
#include <iostream>
#include <stdexcept>
#include <vector>

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
				    const Vector3D& size,
				    int seed)
    : NormalComputingHeightMap (width, height, size),
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
    Vector3D r (m_scale.x * x,
		m_scale.y * get (x, y),
		m_scale.z * y);    
    //    std::cout << x << "," << y << " -> " << r << "\n";
    return r;
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

  // Raise normalized heights to a power; k > 1 sharpens peaks
  // and flattens valleys.
  void
  RandomHeightMap::exponentiate (float k)
  { FORALL (x, y) set (x, y, std::pow (get (x, y), k)); }
  
  typedef boost::variate_generator<boost::mt19937&,
				   boost::uniform_real<> > realgen_t;

  namespace {
    inline float sstep (float e0, float e1, float x) {
      float t = (x - e0) / (e1 - e0);
      t = t < 0 ? 0 : (t > 1 ? 1 : t);
      return t * t * (3 - 2 * t);
    }

    // Classic 2D Perlin gradient noise with fBm and ridged-
    // multifractal composites.
    struct PerlinNoise {
      int perm[512];

      explicit PerlinNoise (unsigned seed) {
	boost::mt19937 rng;
	rng.seed (boost::mt19937::result_type (seed));
	int p[256];
	for (int i = 0; i < 256; ++i)
	  p[i] = i;
	for (int i = 255; i > 0; --i) {
	  boost::uniform_int<> d (0, i);
	  int j = d (rng);
	  int t = p[i]; p[i] = p[j]; p[j] = t;
	}
	for (int i = 0; i < 512; ++i)
	  perm[i] = p[i & 255];
      }

      static float fade (float t)
      { return t * t * t * (t * (t * 6 - 15) + 10); }

      static float lerp (float a, float b, float t)
      { return a + t * (b - a); }

      static float grad (int hash, float x, float y) {
	switch (hash & 7) {
	case 0:  return  x + y;
	case 1:  return  x - y;
	case 2:  return -x + y;
	case 3:  return -x - y;
	case 4:  return  x;
	case 5:  return -x;
	case 6:  return  y;
	default: return -y;
	}
      }

      float noise (float x, float y) const {
	int xi = (int) std::floor (x) & 255;
	int yi = (int) std::floor (y) & 255;
	float xf = x - std::floor (x);
	float yf = y - std::floor (y);

	float u = fade (xf), v = fade (yf);

	int aa = perm[perm[xi] + yi];
	int ab = perm[perm[xi] + yi + 1];
	int ba = perm[perm[xi + 1] + yi];
	int bb = perm[perm[xi + 1] + yi + 1];

	return lerp (lerp (grad (aa, xf, yf),
			   grad (ba, xf - 1, yf), u),
		     lerp (grad (ab, xf, yf - 1),
			   grad (bb, xf - 1, yf - 1), u),
		     v);
      }

      float fbm (float x, float y, int octaves,
		 float lacunarity, float gain) const {
	float sum = 0, amp = 1, freq = 1, norm = 0;
	for (int i = 0; i < octaves; ++i) {
	  sum += amp * noise (x * freq, y * freq);
	  norm += amp;
	  amp *= gain;
	  freq *= lacunarity;
	}
	return sum / norm;  // roughly [-1, 1]
      }

      // Musgrave-style ridges: fold the noise at zero so its
      // crossings become sharp crests, weight octaves by the one
      // below so detail clings to the ridgelines.
      float ridged (float x, float y, int octaves,
		    float lacunarity, float gain) const {
	float sum = 0, amp = 0.5f, freq = 1, weight = 1, norm = 0;
	for (int i = 0; i < octaves; ++i) {
	  float n = 1.0f - std::fabs (noise (x * freq, y * freq));
	  n *= n;
	  n *= weight;
	  weight = n * 2.0f;
	  weight = weight < 0 ? 0 : (weight > 1 ? 1 : weight);
	  sum += n * amp;
	  norm += amp;
	  amp *= gain;
	  freq *= lacunarity;
	}
	return sum / norm;  // [0, 1]
      }
    };
  }

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

  void
  RandomHeightMap::randomize_geologically ()
  {
    PerlinNoise base_noise (m_rng ());
    PerlinNoise ridge_noise (m_rng ());
    PerlinNoise warp_noise (m_rng ());

    const float inv = 1.0f / (m_width - 1);

    FORALL (x, y)
      {
	const float u = x * inv;
	const float v = y * inv;

	// Domain warp: bend all later lookups so ridges and coasts
	// flow in curves instead of following the noise grid
	float wx = warp_noise.fbm (u * 3.0f + 11.3f,
				   v * 3.0f + 7.7f, 4, 2.0f, 0.5f);
	float wy = warp_noise.fbm (u * 3.0f + 91.1f,
				   v * 3.0f + 33.9f, 4, 2.0f, 0.5f);
	const float pu = u + 0.15f * wx;
	const float pv = v + 0.15f * wy;

	// Broad continent shape: which parts are lowland or highland
	float continent =
	  base_noise.fbm (pu * 2.5f, pv * 2.5f, 4, 2.0f, 0.5f)
	  * 0.5f + 0.5f;

	// Gentle rolling detail for the plains
	float plains =
	  base_noise.fbm (pu * 12.0f, pv * 12.0f, 4, 2.0f, 0.5f)
	  * 0.5f + 0.5f;

	// Sharp ridged relief for the mountains
	float mountains =
	  ridge_noise.ridged (pu * 6.0f, pv * 6.0f, 6, 2.05f, 0.55f);

	// Altitude decides the character: smooth low, ridgey high
	const float mountain_mask = sstep (0.45f, 0.75f, continent);

	const float h =
	  continent * 0.55f
	  + plains * 0.12f * (1.0f - mountain_mask)
	  + mountains * 0.65f * mountain_mask;

	set (x, y, h);
      }

    normalize ();
    // NB: no recompute_normals() here -- the caller shapes further
  }

  void
  RandomHeightMap::load_raw_u16 (const std::string& path,
				 float meters_per_unit,
				 float max_height_m)
  {
    std::ifstream f (path.c_str (), std::ios::binary);
    if (!f)
      throw std::runtime_error ("can't open heightmap: " + path);

    std::vector<unsigned char> bytes (m_width * m_height * 2);
    f.read ((char *) &bytes[0], bytes.size ());
    if (f.gcount () != (std::streamsize) bytes.size ())
      throw std::runtime_error ("heightmap truncated: " + path);

    FORALL (x, y)
      {
	const size_t i = 2 * ((size_t) y * m_width + x);
	const unsigned v = bytes[i] | (bytes[i + 1] << 8);
	set (x, y, v * meters_per_unit / max_height_m);
      }
  }

  void
  RandomHeightMap::erode_hydraulically (int droplets)
  {
    // Droplet erosion after Beyer (2015): each raindrop rolls
    // downhill, picking up sediment while it accelerates and
    // dropping it as it slows, carving gullies into mountainsides
    // and building alluvial flats where valleys open out.
    const float inertia    = 0.05f;
    const float capacity_k = 4.0f;
    const float min_slope  = 0.005f;
    const float erode_k    = 0.3f;
    const float deposit_k  = 0.3f;
    const float evaporate  = 0.015f;
    const float gravity    = 4.0f;
    const int   max_steps  = 64;

    boost::uniform_real<> range (0, 1);
    realgen_t g (m_rng, range);

    for (int d = 0; d < droplets; ++d)
      {
	float px = 1.0f + g () * (m_width - 3);
	float py = 1.0f + g () * (m_height - 3);
	float dirx = 0, diry = 0;
	float speed = 1, water = 1, sediment = 0;

	for (int step = 0; step < max_steps; ++step)
	  {
	    const int xi = (int) px, yi = (int) py;
	    const float fx = px - xi, fy = py - yi;

	    const float h00 = get (xi, yi);
	    const float h10 = get (xi + 1, yi);
	    const float h01 = get (xi, yi + 1);
	    const float h11 = get (xi + 1, yi + 1);

	    // Bilinear height and gradient under the droplet
	    const float gradx =
	      (h10 - h00) * (1 - fy) + (h11 - h01) * fy;
	    const float grady =
	      (h01 - h00) * (1 - fx) + (h11 - h10) * fx;
	    const float height =
	      h00 * (1 - fx) * (1 - fy) + h10 * fx * (1 - fy) +
	      h01 * (1 - fx) * fy + h11 * fx * fy;

	    // Inertia blends old direction with the slope
	    dirx = dirx * inertia - gradx * (1 - inertia);
	    diry = diry * inertia - grady * (1 - inertia);
	    const float len = std::sqrt (dirx * dirx + diry * diry);
	    if (len < 1e-10f)
	      break;
	    dirx /= len;
	    diry /= len;

	    const float nx = px + dirx;
	    const float ny = py + diry;
	    if (nx < 1 || nx >= m_width - 2 ||
		ny < 1 || ny >= m_height - 2)
	      break;

	    const int nxi = (int) nx, nyi = (int) ny;
	    const float nfx = nx - nxi, nfy = ny - nyi;
	    const float nheight =
	      get (nxi, nyi) * (1 - nfx) * (1 - nfy) +
	      get (nxi + 1, nyi) * nfx * (1 - nfy) +
	      get (nxi, nyi + 1) * (1 - nfx) * nfy +
	      get (nxi + 1, nyi + 1) * nfx * nfy;

	    const float dh = nheight - height;

	    const float capacity =
	      std::max (-dh, min_slope) * speed * water * capacity_k;

	    if (sediment > capacity || dh > 0)
	      {
		// Slowing down (or ran uphill into a pit): deposit
		const float amount = (dh > 0)
		  ? std::min (dh, sediment)
		  : (sediment - capacity) * deposit_k;
		sediment -= amount;

		set (xi, yi,         h00 + amount * (1 - fx) * (1 - fy));
		set (xi + 1, yi,     h10 + amount * fx * (1 - fy));
		set (xi, yi + 1,     h01 + amount * (1 - fx) * fy);
		set (xi + 1, yi + 1, h11 + amount * fx * fy);
	      }
	    else
	      {
		// Accelerating: scoop material, but never dig
		// deeper than the drop just travelled
		const float amount =
		  std::min ((capacity - sediment) * erode_k, -dh);
		sediment += amount;

		set (xi, yi,         h00 - amount * (1 - fx) * (1 - fy));
		set (xi + 1, yi,     h10 - amount * fx * (1 - fy));
		set (xi, yi + 1,     h01 - amount * (1 - fx) * fy);
		set (xi + 1, yi + 1, h11 - amount * fx * fy);
	      }

	    speed = std::sqrt (std::max (0.0f,
					 speed * speed - dh * gravity));
	    water *= (1 - evaporate);
	    px = nx;
	    py = ny;
	  }
      }
  }

  void
  RandomHeightMap::erode_thermally (int iterations, float talus)
  {
    static const int dx[4] = { 1, -1, 0, 0 };
    static const int dy[4] = { 0, 0, 1, -1 };

    for (int it = 0; it < iterations; ++it)
      for (int y = 1; y < m_height - 1; ++y)
	for (int x = 1; x < m_width - 1; ++x)
	  {
	    const float h = get (x, y);
	    int   best  = -1;
	    float bestd = talus;

	    for (int k = 0; k < 4; ++k)
	      {
		const float d = h - get (x + dx[k], y + dy[k]);
		if (d > bestd)
		  {
		    bestd = d;
		    best  = k;
		  }
	      }

	    if (best >= 0)
	      {
		const float move = 0.5f * (bestd - talus);
		set (x, y, h - move);
		set (x + dx[best], y + dy[best],
		     get (x + dx[best], y + dy[best]) + move);
	      }
	  }
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

    // Each vertex accumulates the normals of every triangle that
    // touches it (up to six), which smooths adequately on a dense
    // grid; anything fancier is far too slow at 4M vertices.
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
