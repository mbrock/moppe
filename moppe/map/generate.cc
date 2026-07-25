
#include <moppe/gfx/math.hh>
#include <moppe/map/generate.hh>
#include <moppe/profile.hh>
#include <moppe/terrain/cpu_evaluator.hh>

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstring>
#include <fstream>
#include <stdexcept>
#include <vector>

namespace moppe {
  namespace map {
    NormalMap::NormalMap (int width, int height)
        : m_data (width, height), m_width (width), m_height (height) {
      reset ();
    }

    void NormalMap::reset () {
      for (int y = 0; y < m_height; ++y)
        for (int x = 0; x < m_width; ++x)
          m_data.at (y, x) = Vec3 (0, 0, 0);
    }

    void NormalMap::add (int x, int y, const Vec3& v) {
      if (((x < 0) || (x > m_width - 1) || (y < 0) || (y > m_height - 1)))
        return;

      m_data.at (y, x) += v;
    }

    void NormalMap::set (int x, int y, const Vec3& v) {
      m_data.at (y, x) = v;
    }

    void NormalMap::normalize_all () {
      for (int y = 0; y < m_height; ++y)
        for (int x = 0; x < m_width; ++x)
          normalize (m_data.at (y, x));
    }

    RandomHeightMap::RandomHeightMap (int width, int height, const Vec3& size)
        : NormalComputingHeightMap (width, height, size),
          m_data (width, height), m_eroded ((std::size_t)width * height, 0.0f),
          m_deposited ((std::size_t)width * height, 0.0f) {}

    void RandomHeightMap::reset_sediment_ledger () {
      std::fill (m_eroded.begin (), m_eroded.end (), 0.0f);
      std::fill (m_deposited.begin (), m_deposited.end (), 0.0f);
    }

#define FORALL(x, y)                                                           \
  for (int y = 0; y < m_height; ++y)                                           \
    for (int x = 0; x < m_width; ++x)

    float HeightMap::min_value () const {
      float min = get (0, 0);

      FORALL (x, y) {
        float v = get (x, y);
        min = (v < min) ? v : min;
      }

      return min;
    }

    float HeightMap::max_value () const {
      float max = get (0, 0);

      FORALL (x, y) {
        float v = get (x, y);
        max = (v > max) ? v : max;
      }

      return max;
    }

    Vec3 HeightMap::vertex (int x, int y) const {
      Vec3 r (m_scale[0] * x, m_scale[1] * get (x, y), m_scale[2] * y);
      //    std::cout << x << "," << y << " -> " << r << "\n";
      return r;
    }

    Vec3 HeightMap::triangle_normal (
      int x1, int y1, int x2, int y2, int x3, int y3) const {
      Vec3 a = vertex (x1, y1);
      Vec3 b = vertex (x2, y2);
      Vec3 c = vertex (x3, y3);

      return normalized (cross (b - a, c - a));
    }

    terrain::TerrainView RandomHeightMap::terrain_view () const {
      return terrain::TerrainView (
        discretization ().grid (),
        std::span<const float> (m_data.raw (),
                                static_cast<std::size_t> (m_width) * m_height));
    }

    terrain::TerrainDiscretization RandomHeightMap::discretization () const {
      const std::size_t width = static_cast<std::size_t> (m_width);
      const std::size_t height = static_cast<std::size_t> (m_height);
      return terrain::TerrainDiscretization (
        { .width = width, .height = height },
        { .width = width,
          .height = height,
          .spacing_x = m_scale[0] * mp_units::si::metre,
          .spacing_y = m_scale[2] * mp_units::si::metre,
          .height_scale = m_scale[1] * mp_units::si::metre });
    }

    void RandomHeightMap::materialize (const terrain::ScalarField& field) {
      static const terrain::CpuEvaluator evaluator;
      materialize (field, evaluator);
    }

    void
    RandomHeightMap::materialize (const terrain::ScalarField& field,
                                  const terrain::FieldEvaluator& evaluator) {
      const terrain::TerrainDiscretization sampling = discretization ();
      const terrain::ScalarRaster raster =
        evaluator.evaluate (field, sampling.field_sampling_grid ());

      std::copy (
        raster.values ().begin (), raster.values ().end (), m_data.raw ());
      // A fresh source field starts a fresh sediment history.
      reset_sediment_ledger ();
      // NB: no recompute_normals() here -- the caller shapes further
    }

    // Heightfield cache format: 4-byte magic, int32 width, int32
    // height, then width*height little-endian float32, row 0 first.
    static const char heightfield_magic[4] = { 'M', 'O', 'P', '3' };
    static const char sediment_ledger_magic[4] = { 'L', 'G', 'R', '1' };

    bool RandomHeightMap::try_load_cache (const std::string& path) {
      std::ifstream f (path.c_str (), std::ios::binary);
      if (!f)
        return false;

      char magic[4] = { 0, 0, 0, 0 };
      int32_t w = 0, h = 0;
      f.read (magic, 4);
      f.read ((char*)&w, 4);
      f.read ((char*)&h, 4);
      if (!f || std::memcmp (magic, heightfield_magic, 4) != 0 ||
          w != m_width || h != m_height)
        return false;

      std::vector<float> heights ((size_t)m_width * m_height);
      f.read ((char*)&heights[0], heights.size () * sizeof (float));
      if (f.gcount () != (std::streamsize)(heights.size () * sizeof (float)))
        return false;

      FORALL (x, y)
      set (x, y, heights[(size_t)y * m_width + x]);

      // Sediment ledger: a tagged section after the heights.  A cache
      // without the tag leaves the ledger zeroed.
      reset_sediment_ledger ();
      char ledger_magic[4] = { 0, 0, 0, 0 };
      f.read (ledger_magic, 4);
      if (f.gcount () == 4 &&
          std::memcmp (ledger_magic, sediment_ledger_magic, 4) == 0) {
        const std::streamsize ledger_bytes =
          (std::streamsize)(heights.size () * sizeof (float));
        f.read ((char*)m_eroded.data (), ledger_bytes);
        const bool have_eroded = f.gcount () == ledger_bytes;
        f.read ((char*)m_deposited.data (), ledger_bytes);
        if (!have_eroded || f.gcount () != ledger_bytes)
          reset_sediment_ledger ();
      }
      return true;
    }

    void RandomHeightMap::save_cache (const std::string& path) const {
      std::ofstream f (path.c_str (), std::ios::binary);
      if (!f)
        throw std::runtime_error ("can't write map cache: " + path);

      const int32_t w = m_width, h = m_height;
      f.write (heightfield_magic, 4);
      f.write ((const char*)&w, 4);
      f.write ((const char*)&h, 4);
      f.write ((const char*)raw_heights (),
               (size_t)m_width * m_height * sizeof (float));
      f.write (sediment_ledger_magic, 4);
      f.write ((const char*)m_eroded.data (),
               m_eroded.size () * sizeof (float));
      f.write ((const char*)m_deposited.data (),
               m_deposited.size () * sizeof (float));
    }

    void compute_normal_map (const HeightMap& height_map,
                             NormalMap& normal_map) {
      MOPPE_PROFILE_ZONE ("compute_normal_map");
      normal_map.reset ();
      const int period_x = height_map.width ();
      const int period_y = height_map.height ();
      const Vec3 scale = height_map.scale ();
      // Each vertex accumulates the normals of every triangle that
      // touches it (up to six).  Edge triangles continue past the lattice
      // in space while their heights wrap around the torus.
      const auto vertex = [&] (int x, int y) {
        return Vec3 (scale[0] * x,
                     scale[1] *
                       height_map.get (terrain::wrap_index (x, period_x),
                                       terrain::wrap_index (y, period_y)),
                     scale[2] * y);
      };
      const auto face_normal =
        [&] (int x1, int y1, int x2, int y2, int x3, int y3) {
          const Vec3 a = vertex (x1, y1);
          const Vec3 b = vertex (x2, y2);
          const Vec3 c = vertex (x3, y3);
          return normalized (cross (b - a, c - a));
        };
      for (int y = 0; y < period_y; ++y)
        for (int x = 0; x < period_x; ++x) {
          const Vec3 left = face_normal (x, y, x, y + 1, x + 1, y + 1);
          const Vec3 right = face_normal (x, y, x + 1, y + 1, x + 1, y);
          const int x1 = terrain::wrap_index (x + 1, period_x);
          const int y1 = terrain::wrap_index (y + 1, period_y);
          normal_map.add (x, y, left);
          normal_map.add (x, y1, left);
          normal_map.add (x1, y1, left);
          normal_map.add (x, y, right);
          normal_map.add (x1, y, right);
          normal_map.add (x1, y1, right);
        }
      normal_map.normalize_all ();
    }
  }
}
