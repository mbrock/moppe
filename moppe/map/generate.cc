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
  RandomHeightMap::apply_bowl_edge(float edge_height, float smoothness)
  {
    // Generate separate bowl shape and blend with height map
    FORALL(x, y)
    {
      // Calculate normalized coordinates (-1 to 1)
      float center_x = m_width / 2.0f;
      float center_y = m_height / 2.0f;
      float nx = (x - center_x) / center_x;  // Normalized x: -1 to 1
      float ny = (y - center_y) / center_y;  // Normalized y: -1 to 1
      
      // Generate bowl shape
      float bowl_height = 0.0f;
      
      // Calculate distance to each edge and corner
      float dist_edge_left = std::abs(nx + 1.0f);   // Distance to left edge
      float dist_edge_right = std::abs(nx - 1.0f);  // Distance to right edge
      float dist_edge_top = std::abs(ny + 1.0f);    // Distance to top edge
      float dist_edge_bottom = std::abs(ny - 1.0f); // Distance to bottom edge
      
      // Find minimum distance to any edge
      float min_edge_dist = std::min(std::min(dist_edge_left, dist_edge_right), 
                                     std::min(dist_edge_top, dist_edge_bottom));
      
      // Normalize to 0-1 range (0 at edge, 1 at center)
      float normalized_dist = min_edge_dist / 2.0f;
      
      // Control transition width with smoothness parameter
      if (normalized_dist < smoothness) {
        // Create smooth transition at edges
        float t = normalized_dist / smoothness;
        // Use smoothstep function for gradual transition: 3t² - 2t³
        float smooth_t = t * t * (3.0f - 2.0f * t);
        // Edge height increases as we approach the edge
        bowl_height = edge_height * (1.0f - smooth_t);
      }
      
      // Apply the bowl shape by subtracting from the height map
      // This creates a depression around the edges
      set(x, y, get(x, y) + bowl_height);
    }
    
    // Recompute normals after the height adjustment
    normalize();
    recompute_normals();
  }

  void
  compute_normal_map (const HeightMap& height_map,
		      NormalMap& normal_map)
  {
    normal_map.reset ();

    // Calculate weighted normals for smoother curved terrain
    // Use a larger neighborhood and weighted averaging
    for (int y = 0; y < height_map.height () - 1; ++y)
      for (int x = 0; x < height_map.width () - 1; ++x)
	{
	  // Standard triangles
	  Vector3D left =
	    height_map.triangle_normal (x, y,
					x, y + 1,
					x + 1, y + 1);
	  Vector3D right =
	    height_map.triangle_normal (x, y,
					x + 1, y + 1,
					x + 1, y);

	  // Weight by 1.0 for direct triangles
	  normal_map.add (x, y, left);
	  normal_map.add (x, y + 1, left);
	  normal_map.add (x + 1, y + 1, left);

	  normal_map.add (x, y, right);
	  normal_map.add (x + 1, y, right);
	  normal_map.add (x + 1, y + 1, right);
          
          // Add neighbor triangles for smoother transitions
          // Only add to inner vertices to avoid edge issues
          if (x > 0 && y > 0 && x < height_map.width() - 2 && y < height_map.height() - 2) {
            // Add diagonal neighbors with half weight
            for (int dy = -1; dy <= 1; dy++) {
              for (int dx = -1; dx <= 1; dx++) {
                if (dx == 0 && dy == 0) continue; // Skip self
                
                // Only consider valid triangles
                if (x+dx >= 0 && y+dy >= 0 && 
                    x+dx < height_map.width()-1 && y+dy < height_map.height()-1) {
                  
                  Vector3D neighbor_left =
                    height_map.triangle_normal (x+dx, y+dy,
                                            x+dx, y+dy+1,
                                            x+dx+1, y+dy+1);
                  Vector3D neighbor_right =
                    height_map.triangle_normal (x+dx, y+dy,
                                            x+dx+1, y+dy+1,
                                            x+dx+1, y+dy);
                                            
                  // Add with reduced weight (0.3) for smoothing
                  normal_map.add (x, y, neighbor_left * 0.3);
                  normal_map.add (x, y, neighbor_right * 0.3);
                }
              }
            }
          }
	}

    normal_map.normalize_all ();
  }
}
}
