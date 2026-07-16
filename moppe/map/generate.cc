
#include <moppe/gfx/math.hh>
#include <moppe/map/generate.hh>
#include <moppe/map/tga.hh>
#include <moppe/profile.hh>
#include <moppe/terrain/cpu_evaluator.hh>
#include <moppe/terrain/geological.hh>
#include <moppe/terrain/readings.hh>

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstring>
#include <fstream>
#include <iostream>
#include <random>
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

    RandomHeightMap::RandomHeightMap (int width,
                                      int height,
                                      const Vec3& size,
                                      int seed,
                                      terrain::Topology topology)
        : NormalComputingHeightMap (width, height, size, topology),
          m_data (width, height), m_eroded ((std::size_t)width * height, 0.0f),
          m_deposited ((std::size_t)width * height, 0.0f) {
      m_rng.seed (std::mt19937::result_type (seed));
    }

    void RandomHeightMap::reset_sediment_ledger () {
      std::fill (m_eroded.begin (), m_eroded.end (), 0.0f);
      std::fill (m_deposited.begin (), m_deposited.end (), 0.0f);
    }

    void RandomHeightMap::synchronize_periodic_ledger_edges () {
      if (!periodic ())
        return;
      const auto seam = [this] (std::vector<float>& ledger) {
        for (int y = 0; y < unique_height (); ++y)
          ledger[(std::size_t)y * m_width + m_width - 1] =
            ledger[(std::size_t)y * m_width];
        for (int x = 0; x < m_width; ++x)
          ledger[(std::size_t)(m_height - 1) * m_width + x] =
            ledger[(std::size_t)x];
      };
      seam (m_eroded);
      seam (m_deposited);
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

    void RandomHeightMap::normalize () {
      float* values = m_data.raw ();
      const std::size_t count = static_cast<std::size_t> (m_width) * m_height;
      const terrain::HeightRange range =
        terrain::measure_height_range (terrain_view ());
      const float offset = 0.0f - range.minimum;
      // Preserve the old translate-then-max arithmetic exactly while reducing
      // four full raster passes to two contiguous ones.
      const float translated_maximum = range.maximum + offset;
      const float scale =
        translated_maximum != 0.0f ? 1.0f / translated_maximum : 1.0f;
      for (float* value = values; value != values + count; ++value)
        *value = (*value + offset) * scale;
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
          .height_scale = m_scale[1] * mp_units::si::metre,
          .topology = periodic () ? terrain::Topology::Torus
                                  : terrain::Topology::Bounded });
    }

    void RandomHeightMap::translate (float d) {
      FORALL (x, y) set (x, y, d + get (x, y));
    }

    void RandomHeightMap::rescale (float k) {
      FORALL (x, y) set (x, y, k * get (x, y));
    }

    // Raise normalized heights to a power; k > 1 sharpens peaks
    // and flattens valleys.
    void RandomHeightMap::exponentiate (float k) {
      FORALL (x, y) set (x, y, std::pow (get (x, y), k));
    }

    void RandomHeightMap::synchronize_periodic_edges () {
      if (!periodic ())
        return;
      for (int y = 0; y < unique_height (); ++y)
        set (m_width - 1, y, get (0, y));
      for (int x = 0; x < m_width; ++x)
        set (x, m_height - 1, get (x, 0));
      synchronize_periodic_ledger_edges ();
    }

    namespace {
      // Bound uniform-real generator, replacing
      // boost::variate_generator<mt19937&, uniform_real<> >.
      struct realgen_t {
        std::mt19937& rng;
        std::uniform_real_distribution<float> dist;

        realgen_t (std::mt19937& rng, float lo, float hi)
            : rng (rng), dist (lo, hi) {}

        float operator() () {
          return dist (rng);
        }
      };

    }

    void RandomHeightMap::randomize_uniformly () {
      realgen_t g (m_rng, 0, 1);

      for (int y = 0; y < m_height; ++y)
        for (int x = 0; x < m_width; ++x)
          set (x, y, g ());

      synchronize_periodic_edges ();
      recompute_normals ();
    }

    static void do_plasma_step (RandomHeightMap& map,
                                int step,
                                int x,
                                int y,
                                realgen_t& g,
                                float max_displacement,
                                float r,
                                float nw_v,
                                float ne_v,
                                float sw_v,
                                float se_v) {
      const int side = (map.width ()) / (1 << step);

      const int nw_y = y + 0, nw_x = x + 0;
      const int n_y = y + 0, n_x = x + side / 2;
      const int w_y = y + side / 2, w_x = x + 0;
      const int c_y = y + side / 2, c_x = x + side / 2;

      if (side == 1) {
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

      do_plasma_step (map, step + 1, nw_x, nw_y, g, m, r, nw_v, n_v, w_v, c_v);
      do_plasma_step (map, step + 1, n_x, n_y, g, m, r, n_v, ne_v, c_v, e_v);
      do_plasma_step (map, step + 1, w_x, w_y, g, m, r, w_v, c_v, sw_v, s_v);
      do_plasma_step (map, step + 1, c_x, c_y, g, m, r, c_v, e_v, s_v, se_v);
    }

    void RandomHeightMap::randomize_plasmally (float roughness) {
      realgen_t g (m_rng, -1, 1);

      do_plasma_step (*this,
                      0,
                      0,
                      0,
                      g,
                      1.0,
                      std::pow (2, -roughness),
                      g (),
                      g (),
                      g (),
                      g ());
      normalize ();
      synchronize_periodic_edges ();
      recompute_normals ();
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
      synchronize_periodic_edges ();
      // A fresh source field starts a fresh sediment history.
      reset_sediment_ledger ();
      // NB: no recompute_normals() here -- the caller shapes further
    }

    void
    RandomHeightMap::randomize_geologically (terrain::GeologicalLayer layer) {
      terrain::GeologicalRecipe recipe;
      recipe.seeds = { .base = terrain::Seed { m_rng () },
                       .ridge = terrain::Seed { m_rng () },
                       .warp = terrain::Seed { m_rng () } };
      const terrain::GeologicalFields fields =
        terrain::make_geological_fields (recipe);
      materialize (terrain::geological_layer (fields, layer));
      normalize ();
    }

    void RandomHeightMap::load_raw_u16 (const std::string& path,
                                        float meters_per_unit,
                                        float max_height_m) {
      std::ifstream f (path.c_str (), std::ios::binary);
      if (!f)
        throw std::runtime_error ("can't open heightmap: " + path);

      std::vector<unsigned char> bytes (m_width * m_height * 2);
      f.read ((char*)&bytes[0], bytes.size ());
      if (f.gcount () != (std::streamsize)bytes.size ())
        throw std::runtime_error ("heightmap truncated: " + path);

      FORALL (x, y) {
        const size_t i = 2 * ((size_t)y * m_width + x);
        const unsigned v = bytes[i] | (bytes[i + 1] << 8);
        set (x, y, v * meters_per_unit / max_height_m);
      }
    }

    // Heightfield cache format: 4-byte magic, int32 width, int32
    // height, then width*height little-endian float32, row 0 first.
    static const char bounded_heightfield_magic[4] = { 'M', 'O', 'P', 'C' };
    static const char torus_heightfield_magic[4] = { 'M', 'O', 'P', '2' };
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
      const char* expected_magic =
        periodic () ? torus_heightfield_magic : bounded_heightfield_magic;
      if (!f || std::memcmp (magic, expected_magic, 4) != 0 || w != m_width ||
          h != m_height)
        return false;

      std::vector<float> heights ((size_t)m_width * m_height);
      f.read ((char*)&heights[0], heights.size () * sizeof (float));
      if (f.gcount () != (std::streamsize)(heights.size () * sizeof (float)))
        return false;

      FORALL (x, y)
      set (x, y, heights[(size_t)y * m_width + x]);
      synchronize_periodic_edges ();

      // Sediment ledger: a tagged section after the heights (and before
      // the loading-history section game code appends).  A cache without
      // the tag leaves the ledger zeroed.
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
      f.write (
        periodic () ? torus_heightfield_magic : bounded_heightfield_magic, 4);
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

    void RandomHeightMap::erode_thermally (int iterations, float talus) {
      static const int dx[4] = { 1, -1, 0, 0 };
      static const int dy[4] = { 0, 0, 1, -1 };

      if (periodic ()) {
        const int period_x = unique_width ();
        const int period_y = unique_height ();
        for (int it = 0; it < iterations; ++it)
          for (int y = 0; y < period_y; ++y)
            for (int x = 0; x < period_x; ++x) {
              const float h = get (x, y);
              int best = -1;
              float bestd = talus;
              for (int k = 0; k < 4; ++k) {
                const int nx = terrain::wrap_index (x + dx[k], period_x);
                const int ny = terrain::wrap_index (y + dy[k], period_y);
                const float d = h - get (nx, ny);
                if (d > bestd) {
                  bestd = d;
                  best = k;
                }
              }
              if (best >= 0) {
                const int nx = terrain::wrap_index (x + dx[best], period_x);
                const int ny = terrain::wrap_index (y + dy[best], period_y);
                const float move = 0.5f * (bestd - talus);
                set (x, y, h - move);
                set (nx, ny, get (nx, ny) + move);
              }
            }
        synchronize_periodic_edges ();
        return;
      }

      for (int it = 0; it < iterations; ++it)
        for (int y = 1; y < m_height - 1; ++y)
          for (int x = 1; x < m_width - 1; ++x) {
            const float h = get (x, y);
            int best = -1;
            float bestd = talus;

            for (int k = 0; k < 4; ++k) {
              const float d = h - get (x + dx[k], y + dy[k]);
              if (d > bestd) {
                bestd = d;
                best = k;
              }
            }

            if (best >= 0) {
              const float move = 0.5f * (bestd - talus);
              set (x, y, h - move);
              set (x + dx[best],
                   y + dy[best],
                   get (x + dx[best], y + dy[best]) + move);
            }
          }
    }

    terrain::HillslopeDiffusionReport RandomHeightMap::diffuse_hillslopes (
      julian_years_t duration, square_meters_per_julian_year_t diffusivity) {
      const float years = julian_years_value (duration);
      const float d = square_meters_per_julian_year_value (diffusivity);
      if (!std::isfinite (years) || years < 0.0f || !std::isfinite (d) ||
          d < 0.0f)
        throw std::invalid_argument (
          "hillslope diffusion needs non-negative duration and diffusivity");

      const int cells_x = unique_width ();
      const int cells_y = unique_height ();
      terrain::HillslopeDiffusionReport report;
      report.cells =
        terrain::cell_count (static_cast<std::uint64_t> (cells_x) * cells_y);
      if (years == 0.0f || d == 0.0f)
        return report;

      // Stored heights are normalized; physical z is stored * m_scale[1].
      // The equation is linear, so the height scale cancels and the sweep
      // runs on stored values against physical cell spacing.
      const float hx = m_scale[0];
      const float hz = m_scale[2];
      const float dt_stable =
        1.0f / (2.0f * d * (1.0f / (hx * hx) + 1.0f / (hz * hz)));
      // A bounded sweep count keeps a careless duration/diffusivity pair
      // from freezing the pipeline; the report shows what actually ran.
      constexpr int max_sweeps = 20000;
      const int sweeps = static_cast<int> (
        std::min (static_cast<double> (max_sweeps),
                  std::ceil (static_cast<double> (years) / dt_stable)));
      const float dt = years / static_cast<float> (sweeps);
      const float cx = d * dt / (hx * hx);
      const float cz = d * dt / (hz * hz);
      report.sweeps = terrain::iteration_count (sweeps);

      const std::size_t count = static_cast<std::size_t> (cells_x) * cells_y;
      std::vector<float> initial (count);
      std::vector<float> current (count);
      std::vector<float> next (count);
      for (int y = 0; y < cells_y; ++y)
        for (int x = 0; x < cells_x; ++x)
          initial[static_cast<std::size_t> (y) * cells_x + x] = get (x, y);
      current = initial;

      const bool wrap = periodic ();
      for (int sweep = 0; sweep < sweeps; ++sweep) {
        for (int y = 0; y < cells_y; ++y) {
          const int up =
            wrap ? terrain::wrap_index (y - 1, cells_y) : std::max (y - 1, 0);
          const int down = wrap ? terrain::wrap_index (y + 1, cells_y)
                                : std::min (y + 1, cells_y - 1);
          for (int x = 0; x < cells_x; ++x) {
            const int left =
              wrap ? terrain::wrap_index (x - 1, cells_x) : std::max (x - 1, 0);
            const int right = wrap ? terrain::wrap_index (x + 1, cells_x)
                                   : std::min (x + 1, cells_x - 1);
            const std::size_t i = static_cast<std::size_t> (y) * cells_x + x;
            const float center = current[i];
            next[i] =
              center +
              cx * (current[static_cast<std::size_t> (y) * cells_x + left] +
                    current[static_cast<std::size_t> (y) * cells_x + right] -
                    2.0f * center) +
              cz * (current[static_cast<std::size_t> (up) * cells_x + x] +
                    current[static_cast<std::size_t> (down) * cells_x + x] -
                    2.0f * center);
          }
        }
        current.swap (next);
      }

      const double cell_volume =
        static_cast<double> (hx) * hz * m_scale[1]; // m^3 per unit delta
      double lowered = 0.0, raised = 0.0, sum_abs = 0.0, max_abs = 0.0;
      for (int y = 0; y < cells_y; ++y)
        for (int x = 0; x < cells_x; ++x) {
          const std::size_t i = static_cast<std::size_t> (y) * cells_x + x;
          const float delta = current[i] - initial[i];
          set (x, y, current[i]);
          record_material_change (x, y, delta);
          const double physical =
            static_cast<double> (delta) * m_scale[1]; // meters
          if (physical < 0.0)
            lowered -= physical * hx * hz;
          else
            raised += physical * hx * hz;
          sum_abs += std::fabs (physical);
          max_abs = std::max (max_abs, std::fabs (physical));
        }
      (void)cell_volume;
      synchronize_periodic_edges ();

      const auto m3 =
        mp_units::si::metre * mp_units::si::metre * mp_units::si::metre;
      report.lowered_volume = lowered * m3;
      report.raised_volume = raised * m3;
      report.mean_absolute_change =
        (count > 0 ? sum_abs / static_cast<double> (count) : 0.0) *
        mp_units::si::metre;
      report.maximum_absolute_change = max_abs * mp_units::si::metre;
      return report;
    }

    void write_tga (std::ostream& stream, const HeightMap& map) {
      int w = map.width ();
      int h = map.height ();

      std::vector<char> data;
      data.reserve (w * h);

      for (int y = 0; y < h; ++y)
        for (int x = 0; x < w; ++x) {
          char v = map.get (x, y) * 255.0;
          data.push_back (v);
        }

      tga::write_gray8 (stream, data, w, h);
    }

    void compute_normal_map (const HeightMap& height_map,
                             NormalMap& normal_map) {
      MOPPE_PROFILE_ZONE ("compute_normal_map");
      normal_map.reset ();

      if (height_map.periodic ()) {
        const int period_x = height_map.unique_width ();
        const int period_y = height_map.unique_height ();
        for (int y = 0; y < period_y; ++y)
          for (int x = 0; x < period_x; ++x) {
            const Vec3 left =
              height_map.triangle_normal (x, y, x, y + 1, x + 1, y + 1);
            const Vec3 right =
              height_map.triangle_normal (x, y, x + 1, y + 1, x + 1, y);
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
        for (int y = 0; y < period_y; ++y)
          normal_map.set (period_x, y, normal_map.at (0, y));
        for (int x = 0; x <= period_x; ++x)
          normal_map.set (x, period_y, normal_map.at (x, 0));
        return;
      }

      // Each vertex accumulates the normals of every triangle that
      // touches it (up to six), which smooths adequately on a dense
      // grid; anything fancier is far too slow at 4M vertices.
      for (int y = 0; y < height_map.height () - 1; ++y)
        for (int x = 0; x < height_map.width () - 1; ++x) {
          Vec3 left = height_map.triangle_normal (x, y, x, y + 1, x + 1, y + 1);
          Vec3 right =
            height_map.triangle_normal (x, y, x + 1, y + 1, x + 1, y);

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
