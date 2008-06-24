
#ifndef MOPPE_GENERATE_HH
#define MOPPE_GENERATE_HH

#include <boost/multi_array.hpp>
#include <boost/random.hpp>
#include <boost/shared_ptr.hpp>

#include <iostream>

namespace moppe {
  namespace map {
    class HeightMap {
    public:
      HeightMap (int width, int height)
	: m_width (width), m_height (height)
      { }

      virtual ~HeightMap () { }

      virtual float get (int x, int y) const = 0;

      inline int width  () const { return m_width; }
      inline int height () const { return m_height; }

      float min_value () const;
      float max_value () const;

    protected:
      const int m_width;
      const int m_height;
    };

    class RandomHeightMap: public HeightMap {
    public:
      RandomHeightMap (int width, int height, int seed = 0);

      inline float get (int x, int y) const
      { return m_data[y][x]; }

      inline void set (int x, int y, float value)
      { m_data[y][x] = value; }

      void normalize           ();
      void translate           (float d);
      void scale               (float k);

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
			      boost::shared_ptr<HeightMap> to)
	: HeightMap (from->width (), from->height ()),
	  m_from  (from),
	  m_to    (to),
	  m_alpha (0.0)
      { }

      float get (int x, int y) const;
      
      void set_blending_factor      (float alpha);
      void increase_blending_factor (float delta);

      bool done () const { return m_alpha >= 1.0; }

    private:
      const boost::shared_ptr<HeightMap> m_from;
      const boost::shared_ptr<HeightMap> m_to;

      float m_alpha;
    };

    void
    write_tga (std::ostream& stream, const HeightMap& map);
  }
}

#endif
