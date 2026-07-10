
#ifndef MOPPE_GENERATE_HH
#define MOPPE_GENERATE_HH

#include <moppe/gfx/math.hh>
#include <moppe/terrain/geological.hh>

#include <iostream>
#include <memory>
#include <random>
#include <string>
#include <vector>

namespace moppe {
namespace map {
  // Contiguous row-major 2D array; at (y, x) preserves the old
  // boost::multi_array m_data[y][x] indexing.
  template <typename T>
  class Array2D {
  public:
    Array2D (int width, int height)
      : m_width (width),
	m_data ((size_t) width * height)
    { }

    inline T& at (int y, int x)
    { return m_data[(size_t) y * m_width + x]; }

    inline const T& at (int y, int x) const
    { return m_data[(size_t) y * m_width + x]; }

    inline const T* raw () const { return m_data.data (); }

  private:
    int m_width;
    std::vector<T> m_data;
  };

  class NormalMap {
  public:
    NormalMap (int width, int height);

    inline const Vector3D& at (int x, int y) const
    { return m_data.at (y, x); }

    inline const Vector3D* raw () const { return m_data.raw (); }

    void reset         ();
    void add           (int x, int y, const Vector3D& v);
    void normalize_all ();

  private:
    Array2D<Vector3D> m_data;

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
      // Test the floats: integer truncation would admit a strip of
      // slightly-negative coordinates on two edges
      return x >= 0 && y >= 0 &&
	x <= (m_width - 2) * m_scale.x &&
	y <= (m_height - 2) * m_scale.z;
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

      clamp (xi, 0, m_width - 2);
      clamp (yi, 0, m_height - 2);

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

    // Contiguous width*height array, row 0 first; the renderer
    // uploads it as a texture.
    const Vector3D* raw_normals () const
    { return m_normals.raw (); }

  private:
    NormalMap m_normals;
  };

  class RandomHeightMap: public NormalComputingHeightMap {
  public:
    RandomHeightMap (int width, int height,
		     const Vector3D& size,
		     int seed = 0);

    inline float get (int x, int y) const
    { return m_data.at (y, x); }

    inline void set (int x, int y, float value)
    { m_data.at (y, x) = value; }

    // Contiguous width*height array, row 0 first; the renderer
    // uploads it as a texture.
    const float* raw_heights () const
    { return m_data.raw (); }

    void normalize           ();
    void translate           (float d);
    void rescale             (float k);
    void exponentiate        (float k);

    // Restart all procedural choices from a known seed.  Terrain
    // tools use this before selecting a component so every view is
    // sampled from the same underlying noise fields.
    void reseed (int seed)
    { m_rng.seed (std::mt19937::result_type (seed)); }

    void randomize_uniformly ();
    void randomize_plasmally (float roughness);

    // Noise-composed terrain: smooth warped plains low down,
    // ridged mountains up high.  Leaves normals to the caller so
    // further shaping passes can run first.
    void randomize_geologically
      (terrain::GeologicalLayer layer = terrain::GeologicalLayer::Combined);

    // Load raw little-endian uint16 heights (width x height, row 0
    // first); value * meters_per_unit gives meters, normalized
    // against max_height_m.
    void load_raw_u16 (const std::string& path,
		       float meters_per_unit,
		       float max_height_m);

    // Development cache: save/load the finished heightfield so a
    // boot can skip the erosion simulation (see MOPPE_MAPCACHE).
    // Loading fails quietly (returns false) on a missing file or a
    // dimension mismatch.
    bool try_load_cache (const std::string& path);
    void save_cache     (const std::string& path) const;

    // Particle-based hydraulic erosion: carves gullies into the
    // slopes and settles sediment on the plains.
    void erode_hydraulically (int droplets);

    // Talus relaxation: material on too-steep slopes slides to the
    // foot, smoothing single-cell erosion spikes into scree.
    void erode_thermally (int iterations, float talus);

  private:
    Array2D<float> m_data;

    std::mt19937 m_rng;
  };

  void
  write_tga (std::ostream& stream, const HeightMap& map);
}
}

#endif
