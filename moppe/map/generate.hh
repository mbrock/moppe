
#ifndef MOPPE_GENERATE_HH
#define MOPPE_GENERATE_HH

#include <moppe/gfx/math.hh>
#include <moppe/terrain/evaluator.hh>
#include <moppe/terrain/terrain_view.hh>
#include <moppe/terrain/topology.hh>

#include <cmath>
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
          : m_width (width), m_data ((size_t)width * height) {}

      inline T& at (int y, int x) {
        return m_data[(size_t)y * m_width + x];
      }

      inline const T& at (int y, int x) const {
        return m_data[(size_t)y * m_width + x];
      }

      inline const T* raw () const {
        return m_data.data ();
      }
      inline T* raw () {
        return m_data.data ();
      }

    private:
      int m_width;
      std::vector<T> m_data;
    };

    class NormalMap {
    public:
      NormalMap (int width, int height);

      inline const Vec3& at (int x, int y) const {
        return m_data.at (y, x);
      }

      inline const Vec3* raw () const {
        return m_data.raw ();
      }

      void reset ();
      void add (int x, int y, const Vec3& v);
      void set (int x, int y, const Vec3& v);
      void normalize_all ();

    private:
      Array2D<Vec3> m_data;

      const int m_width;
      const int m_height;
    };

    class HeightMap {
    public:
      // width x height periodic samples -- no duplicated seam.
      // The lattice period equals the world extent: spacing = size/width.
      HeightMap (int width, int height, const Vec3& size)
          : m_width (width), m_height (height),
            m_scale (size[0] / width, size[1], size[2] / height) {}

      virtual ~HeightMap () {}

      virtual float get (int x, int y) const = 0;
      virtual Vec3 normal (int x, int y) const = 0;

      Vec3 vertex (int x, int y) const;
      Vec3
      triangle_normal (int x1, int y1, int x2, int y2, int x3, int y3) const;
      Vec3 center () const {
        return vertex (m_width / 2, m_height / 2);
      }

      bool in_bounds (float x, float y) const {
        return std::isfinite (x) && std::isfinite (y);
      }

      float interpolated_height (float x, float y) const {
        float gx = terrain::wrap_coordinate (x / m_scale[0],
                                             static_cast<float> (width ()));
        float gy = terrain::wrap_coordinate (y / m_scale[2],
                                             static_cast<float> (height ()));
        const int xi = static_cast<int> (std::floor (gx));
        const int yi = static_cast<int> (std::floor (gy));
        const int xi1 = terrain::wrap_index (xi + 1, m_width);
        const int yi1 = terrain::wrap_index (yi + 1, m_height);

        const float ax = gx - xi;
        const float ay = gy - yi;

        const float r1 = linear_interpolate (get (xi, yi), get (xi1, yi), ax);
        const float r2 = linear_interpolate (get (xi, yi1), get (xi1, yi1), ax);
        return m_scale[1] * linear_interpolate (r1, r2, ay);
      }

      Vec3 interpolated_normal (float x, float y) const {
        float gx = terrain::wrap_coordinate (x / m_scale[0],
                                             static_cast<float> (width ()));
        float gy = terrain::wrap_coordinate (y / m_scale[2],
                                             static_cast<float> (height ()));
        const int xi = static_cast<int> (std::floor (gx));
        const int yi = static_cast<int> (std::floor (gy));
        const int xi1 = terrain::wrap_index (xi + 1, m_width);
        const int yi1 = terrain::wrap_index (yi + 1, m_height);

        const float ax = gx - xi;
        const float ay = gy - yi;

        Vec3 r1 =
          linear_vector_interpolate (normal (xi, yi), normal (xi1, yi), ax);
        Vec3 r2 =
          linear_vector_interpolate (normal (xi, yi1), normal (xi1, yi1), ax);
        return linear_vector_interpolate (r1, r2, ay);
      }

      inline int width () const {
        return m_width;
      }
      inline int height () const {
        return m_height;
      }
      inline Vec3 scale () const {
        return m_scale;
      }
      inline Vec3 size () const {
        return Vec3 (m_scale[0] * width (), m_scale[1], m_scale[2] * height ());
      }

      float min_value () const;
      float max_value () const;

    protected:
      const int m_width;
      const int m_height;
      const Vec3 m_scale;
    };

    void compute_normal_map (const HeightMap& height_map,
                             NormalMap& normal_map);

    class NormalComputingHeightMap : public HeightMap {
    public:
      NormalComputingHeightMap (int width, int height, Vec3 size)
          : HeightMap (width, height, size), m_normals (width, height) {}

      void recompute_normals () {
        compute_normal_map (*this, m_normals);
      }

      Vec3 normal (int x, int y) const {
        return m_normals.at (x, y);
      }

      // Contiguous width*height array, row 0 first; the renderer
      // uploads it as a texture.
      const Vec3* raw_normals () const {
        return m_normals.raw ();
      }

    private:
      NormalMap m_normals;
    };

    class RandomHeightMap : public NormalComputingHeightMap {
    public:
      RandomHeightMap (int width, int height, const Vec3& size);

      inline float get (int x, int y) const {
        return m_data.at (y, x);
      }

      inline void set (int x, int y, float value) {
        m_data.at (y, x) = value;
      }

      // Contiguous width*height array, row 0 first; the renderer
      // uploads it as a texture.
      const float* raw_heights () const {
        return m_data.raw ();
      }

      float* raw_heights () {
        return m_data.raw ();
      }

      terrain::TerrainView terrain_view () const;
      terrain::TerrainDiscretization discretization () const;

      // Lifetime sediment ledger: how much material every cell has lost
      // and gained across all erosive transforms, in storage height
      // units.  Materials, detail displacement, and Lab overlays read
      // it; the true per-cell history beats any curvature proxy.
      const float* raw_eroded () const {
        return m_eroded.data ();
      }
      const float* raw_deposited () const {
        return m_deposited.data ();
      }
      float* raw_eroded () {
        return m_eroded.data ();
      }
      float* raw_deposited () {
        return m_deposited.data ();
      }
      void reset_sediment_ledger ();
      inline void record_material_change (int x, int y, float delta) {
        const std::size_t index = static_cast<std::size_t> (y) * m_width + x;
        if (delta < 0.0f)
          m_eroded[index] -= delta;
        else
          m_deposited[index] += delta;
      }

      // Sample an arbitrary scalar-field value into this storage.  Choosing and
      // expanding a program source belongs to TerrainEvaluator.
      void materialize (const terrain::ScalarField& field);
      void materialize (const terrain::ScalarField& field,
                        const terrain::FieldEvaluator& evaluator);

      // Save/load the finished heightfield. Gameplay supplies an automatic
      // build/profile/seed-keyed path; MOPPE_MAPCACHE can override it.
      // Loading fails quietly (returns false) on a missing file or a
      // dimension mismatch.
      bool try_load_cache (const std::string& path);
      void save_cache (const std::string& path) const;

    private:
      Array2D<float> m_data;
      std::vector<float> m_eroded;
      std::vector<float> m_deposited;
    };
  }
}

#endif
