
#include <boost/multi_array.hpp>
#include <boost/random.hpp>

#include <iostream>

namespace moppe {
  namespace map {
    class HeightMap {
    public:
      HeightMap (int width, int height, int seed = 0);

      inline void set (int x, int y, float value)
      { m_data[y][x] = value; }

      inline float get (int x, int y) const
      { return m_data[y][x]; }

      inline int width  () const { return m_width; }
      inline int height () const { return m_height; }

      float min_value          () const;
      float max_value          () const;

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

      const int m_width;
      const int m_height;
    };

    void
    write_tga (std::ostream& stream, const HeightMap& map);
  }
}
