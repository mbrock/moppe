
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
    HeightMap (int width, int height, const Vector3D& scale)
      : m_width (width), m_height (height), m_scale (scale)
    { }
    
    virtual ~HeightMap () { }
    
    virtual float    get    (int x, int y) const = 0;
    virtual Vector3D normal (int x, int y) const = 0;
    
    Vector3D vertex          (int x, int y) const;
    Vector3D triangle_normal (int x1, int y1,
			      int x2, int y2,
			      int x3, int y3) const;
    
    inline int      width  () const { return m_width; }
    inline int      height () const { return m_height; }
    inline Vector3D scale  () const { return m_scale; }
    
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
			      Vector3D scale)
      : HeightMap (width, height, scale),
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
		     const Vector3D& scale,
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
			    const Vector3D& scale)
      : HeightMap (from->width (), from->height (), scale),
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
