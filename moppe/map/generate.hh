
#ifndef MOPPE_GENERATE_HH
#define MOPPE_GENERATE_HH

#include <moppe/gfx/math.hh>

#include <boost/multi_array.hpp>
#include <boost/random.hpp>
#include <boost/shared_ptr.hpp>

#include <iostream>

namespace moppe {
namespace map {
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

  class HeightMap {
  public:
    HeightMap (int width, int height, const Vector3D& size)
      : m_width (width), m_height (height),
	m_scale (size.x / width, size.y, size.z / height)
    { }
    
    virtual ~HeightMap () { }
    
    virtual float    get    (int x, int y) const = 0;
    virtual Vector3D normal (int x, int y) const = 0;
    
    Vector3D vertex          (int x, int y) const;
    Vector3D triangle_normal (int x1, int y1,
			      int x2, int y2,
			      int x3, int y3) const;
    Vector3D center          () const
    { return vertex (m_width / 2, m_height / 2); }

    bool in_bounds (float x, float y) const {
      int xi = x / m_scale.x;
      int yi = y / m_scale.z;
      return !(xi < 2 || xi > m_width - 2 || yi < 2 || yi > m_height - 2);
    }

    float interpolated_height (float x, float y) const {
      int xi = x / m_scale.x, yi = y / m_scale.z;

      clamp (xi, 0, m_width - 2);
      clamp (yi, 0, m_height - 2);

      float ax = x / m_scale.x - xi;
      float ay = y / m_scale.z - yi;

      float r1 = linear_interpolate (get (xi, yi),
				     get (xi + 1, yi),
				     ax);
      float r2 = linear_interpolate (get (xi, yi + 1),
				     get (xi + 1, yi + 1),
				     ax);
      return m_scale.y * linear_interpolate (r1, r2, ay);

      //      return m_scale.y * get (x / m_scale.x, y / m_scale.z);
    }

    Vector3D interpolated_normal (float x, float y) const {
      int xi = x / m_scale.x, yi = y / m_scale.z;

      clamp (xi, 0, m_width - 1);
      clamp (yi, 0, m_height - 1);

      float ax = x / m_scale.x - xi;
      float ay = y / m_scale.z - yi;

      Vector3D r1 = linear_vector_interpolate (normal (xi, yi),
					       normal (xi + 1, yi),
					       ax);
      Vector3D r2 = linear_vector_interpolate (normal (xi, yi + 1),
					       normal (xi + 1, yi + 1),
					       ax);
      return linear_vector_interpolate (r1, r2, ay);
    }
    
    inline int      width  () const { return m_width; }
    inline int      height () const { return m_height; }

    inline Vector3D scale  () const { return m_scale; }
    inline Vector3D size   () const
    { return Vector3D (m_scale.x * m_width, 
		       m_scale.y, 
		       m_scale.z * m_height); }
    
    float min_value () const;
    float max_value () const;
    
  protected:
    const int      m_width;
    const int      m_height;
    const Vector3D m_scale;
  };
  
  void compute_normal_map (const HeightMap& height_map,
			   NormalMap& normal_map);
  
  class NormalComputingHeightMap: public HeightMap {
  public:
    NormalComputingHeightMap (int width, int height,
			      Vector3D size)
      : HeightMap (width, height, size),
	m_normals (width, height)
    { }

    void recompute_normals () 
    { compute_normal_map (*this, m_normals); }
    
    Vector3D normal (int x, int y) const
    { return m_normals.at (x, y); }

  private:
    NormalMap m_normals;
  };
  
  class RandomHeightMap: public NormalComputingHeightMap {
  public:
    RandomHeightMap (int width, int height,
		     const Vector3D& size,
		     int seed = 0);
    
    inline float get (int x, int y) const
    { return m_data[y][x]; }
    
    inline void set (int x, int y, float value)
    { m_data[y][x] = value; }
    
    void normalize           ();
    void translate           (float d);
    void rescale             (float k);
    
    void randomize_uniformly ();
    void randomize_plasmally (float roughness);
    
  private:
    typedef boost::multi_array<float, 2> array_t;
    typedef array_t::index               index_t;
    
    array_t m_data;
    
    boost::mt19937 m_rng;
  };
  
  class InterpolatingHeightMap: public HeightMap {
  public:
    InterpolatingHeightMap (boost::shared_ptr<HeightMap> from,
			    boost::shared_ptr<HeightMap> to,
			    const Vector3D& size)
      : HeightMap (from->width (), from->height (), size),
	m_from  (from),
	m_to    (to),
	m_alpha (0.0)
    { }
    
    float    get    (int x, int y) const;
    Vector3D normal (int x, int y) const;
    
    void set_blending_factor      (float alpha);
    void increase_blending_factor (float delta);

    void change_maps (boost::shared_ptr<HeightMap> from,
		      boost::shared_ptr<HeightMap> to)
    {
      m_from = from;
      m_to = to;
    }
    
    bool done () const { return m_alpha >= 1.0; }
    
  private:
    boost::shared_ptr<HeightMap> m_from;
    boost::shared_ptr<HeightMap> m_to;
    
    float m_alpha;
  };
  
  void
  write_tga (std::ostream& stream, const HeightMap& map);
}
}

#endif
