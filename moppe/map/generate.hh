
#ifndef MOPPE_GENERATE_HH
#define MOPPE_GENERATE_HH

#include <moppe/gfx/math.hh>
#include <moppe/terrain/erosion.hh>
#include <moppe/terrain/evaluator.hh>
#include <moppe/terrain/geological.hh>
#include <moppe/terrain/terrain_view.hh>
#include <moppe/terrain/topology.hh>

#include <cmath>
#include <functional>
#include <iostream>
#include <memory>
#include <random>
#include <string>
#include <vector>

namespace moppe {
  namespace map {
    enum class HydraulicDropletTermination {
      Flat,
      Settled,
      Boundary,
      WaterCutoff,
      StepLimit
    };

    struct HydraulicDropletPoint {
      float x = 0.0f;
      float y = 0.0f;
      float height = 0.0f;
      float eroded = 0.0f;
      float deposited = 0.0f;
      float speed = 1.0f;
      float water = 1.0f;
      float sediment = 0.0f;
    };

    struct HydraulicDropletTrace {
      std::vector<HydraulicDropletPoint> points;
      HydraulicDropletTermination termination =
        HydraulicDropletTermination::StepLimit;
      float eroded = 0.0f;
      float deposited = 0.0f;
    };

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
      HeightMap (int width,
                 int height,
                 const Vec3& size,
                 terrain::Topology topology = terrain::Topology::Bounded)
          : m_width (width), m_height (height),
            m_scale (
              size[0] /
                (topology == terrain::Topology::Torus ? width - 1 : width),
              size[1],
              size[2] /
                (topology == terrain::Topology::Torus ? height - 1 : height)),
            m_topology (topology) {}

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
        if (periodic ())
          return std::isfinite (x) && std::isfinite (y);
        // Test the floats: integer truncation would admit a strip of
        // slightly-negative coordinates on two edges
        return x >= 0 && y >= 0 && x <= (m_width - 2) * m_scale[0] &&
               y <= (m_height - 2) * m_scale[2];
      }

      float interpolated_height (float x, float y) const {
        float gx = x / m_scale[0], gy = y / m_scale[2];
        if (periodic ()) {
          gx =
            terrain::wrap_coordinate (gx, static_cast<float> (unique_width ()));
          gy = terrain::wrap_coordinate (gy,
                                         static_cast<float> (unique_height ()));
        }
        int xi = static_cast<int> (std::floor (gx));
        int yi = static_cast<int> (std::floor (gy));

        clamp (xi, 0, m_width - 2);
        clamp (yi, 0, m_height - 2);

        float ax = gx - xi;
        float ay = gy - yi;

        float r1 = linear_interpolate (get (xi, yi), get (xi + 1, yi), ax);
        float r2 =
          linear_interpolate (get (xi, yi + 1), get (xi + 1, yi + 1), ax);
        return m_scale[1] * linear_interpolate (r1, r2, ay);

        //      return m_scale[1] * get (x / m_scale[0], y / m_scale[2]);
      }

      Vec3 interpolated_normal (float x, float y) const {
        float gx = x / m_scale[0], gy = y / m_scale[2];
        if (periodic ()) {
          gx =
            terrain::wrap_coordinate (gx, static_cast<float> (unique_width ()));
          gy = terrain::wrap_coordinate (gy,
                                         static_cast<float> (unique_height ()));
        }
        int xi = static_cast<int> (std::floor (gx));
        int yi = static_cast<int> (std::floor (gy));

        clamp (xi, 0, m_width - 2);
        clamp (yi, 0, m_height - 2);

        float ax = gx - xi;
        float ay = gy - yi;

        Vec3 r1 =
          linear_vector_interpolate (normal (xi, yi), normal (xi + 1, yi), ax);
        Vec3 r2 = linear_vector_interpolate (
          normal (xi, yi + 1), normal (xi + 1, yi + 1), ax);
        return linear_vector_interpolate (r1, r2, ay);
      }

      inline int width () const {
        return m_width;
      }
      inline int height () const {
        return m_height;
      }
      inline int unique_width () const {
        return periodic () ? m_width - 1 : m_width;
      }
      inline int unique_height () const {
        return periodic () ? m_height - 1 : m_height;
      }
      inline bool periodic () const {
        return m_topology == terrain::Topology::Torus;
      }

      inline Vec3 scale () const {
        return m_scale;
      }
      inline Vec3 size () const {
        return Vec3 (m_scale[0] * unique_width (),
                     m_scale[1],
                     m_scale[2] * unique_height ());
      }

      float min_value () const;
      float max_value () const;

    protected:
      const int m_width;
      const int m_height;
      const Vec3 m_scale;
      const terrain::Topology m_topology;
    };

    void compute_normal_map (const HeightMap& height_map,
                             NormalMap& normal_map);

    class NormalComputingHeightMap : public HeightMap {
    public:
      NormalComputingHeightMap (
        int width,
        int height,
        Vec3 size,
        terrain::Topology topology = terrain::Topology::Bounded)
          : HeightMap (width, height, size, topology),
            m_normals (width, height) {}

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
      RandomHeightMap (int width,
                       int height,
                       const Vec3& size,
                       int seed = 0,
                       terrain::Topology topology = terrain::Topology::Bounded);

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

      void normalize ();
      void translate (float d);
      void rescale (float k);
      void exponentiate (float k);
      void synchronize_periodic_edges ();

      // Restart all procedural choices from a known seed.  Terrain
      // tools use this before selecting a component so every view is
      // sampled from the same underlying noise fields.
      void reseed (int seed) {
        m_rng.seed (std::mt19937::result_type (seed));
      }

      void randomize_uniformly ();
      void randomize_plasmally (float roughness);

      // Noise-composed terrain: smooth warped plains low down,
      // ridged mountains up high.  Leaves normals to the caller so
      // further shaping passes can run first.
      void randomize_geologically (
        terrain::GeologicalLayer layer = terrain::GeologicalLayer::Combined);

      // Sample an arbitrary scalar-field value into this storage.  Choosing and
      // expanding a program source belongs to TerrainEvaluator.
      void materialize (const terrain::ScalarField& field);
      void materialize (const terrain::ScalarField& field,
                        const terrain::FieldEvaluator& evaluator);

      // Load raw little-endian uint16 heights (width x height, row 0
      // first); value * meters_per_unit gives meters, normalized
      // against max_height_m.
      void load_raw_u16 (const std::string& path,
                         float meters_per_unit,
                         float max_height_m);

      // Save/load the finished heightfield. Gameplay supplies an automatic
      // build/profile/seed-keyed path; MOPPE_MAPCACHE can override it.
      // Loading fails quietly (returns false) on a missing file or a
      // dimension mismatch.
      bool try_load_cache (const std::string& path);
      void save_cache (const std::string& path) const;

      // Particle-based hydraulic erosion: carves gullies into the
      // slopes and settles sediment on the plains.
      terrain::HydraulicErosionReport erode_hydraulically (
        int droplets,
        int batch_size = 256,
        int max_steps = 64,
        float minimum_water = 0.0f,
        terrain::SedimentDisposition sediment_at_termination =
          terrain::SedimentDisposition::Discard,
        terrain::CarvingRule carving_rule = terrain::CarvingRule::PathMonotone);

      // Run one explicitly placed droplet through the same local hydraulic
      // model as bulk erosion. The returned per-step ledger drives Terrain
      // Lab's hero-droplet spectacle; changes are committed to this map.
      HydraulicDropletTrace trace_hydraulic_droplet (
        float x,
        float y,
        int max_steps = 512,
        float minimum_water = 0.01f,
        terrain::SedimentDisposition sediment_at_termination =
          terrain::SedimentDisposition::Deposit,
        terrain::CarvingRule carving_rule = terrain::CarvingRule::PathMonotone);
      terrain::HydraulicErosionReport erode_hydraulically (
        std::mt19937& randomness,
        int droplets,
        int batch_size = 256,
        int max_steps = 64,
        float minimum_water = 0.0f,
        terrain::SedimentDisposition sediment_at_termination =
          terrain::SedimentDisposition::Discard,
        terrain::CarvingRule carving_rule = terrain::CarvingRule::PathMonotone,
        const std::function<void (int, int)>& progress = {});

      // Talus relaxation: material on too-steep slopes slides to the
      // foot, smoothing single-cell erosion spikes into scree.
      void erode_thermally (int iterations, float talus);

    private:
      void synchronize_periodic_ledger_edges ();

      Array2D<float> m_data;
      std::vector<float> m_eroded;
      std::vector<float> m_deposited;

      std::mt19937 m_rng;
    };

    void write_tga (std::ostream& stream, const HeightMap& map);
  }
}

#endif
