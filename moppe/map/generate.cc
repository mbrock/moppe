
#include <moppe/gfx/math.hh>
#include <moppe/map/generate.hh>
#include <moppe/map/tga.hh>
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
          m_data (width, height) {
      m_rng.seed (std::mt19937::result_type (seed));
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
        { .width = static_cast<std::size_t> (m_width),
          .height = static_cast<std::size_t> (m_height),
          .spacing_x = m_scale[0] * mp_units::si::metre,
          .spacing_y = m_scale[2] * mp_units::si::metre,
          .height_scale = m_scale[1] * mp_units::si::metre,
          .topology = periodic () ? terrain::Topology::Torus
                                  : terrain::Topology::Bounded },
        std::span<const float> (m_data.raw (),
                                static_cast<std::size_t> (m_width) * m_height));
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
      const terrain::Domain2D domain { .width =
                                         static_cast<std::size_t> (m_width),
                                       .height =
                                         static_cast<std::size_t> (m_height) };
      const terrain::ScalarRaster raster = evaluator.evaluate (field, domain);

      std::copy (
        raster.values ().begin (), raster.values ().end (), m_data.raw ());
      synchronize_periodic_edges ();
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
    }

    terrain::HydraulicErosionReport RandomHeightMap::erode_hydraulically (
      int droplets,
      int batch_size,
      int max_steps,
      float minimum_water,
      terrain::SedimentDisposition sediment_at_termination,
      terrain::CarvingRule carving_rule) {
      return erode_hydraulically (m_rng,
                                  droplets,
                                  batch_size,
                                  max_steps,
                                  minimum_water,
                                  sediment_at_termination,
                                  carving_rule);
    }

    HydraulicDropletTrace RandomHeightMap::trace_hydraulic_droplet (
      float px,
      float py,
      int max_steps,
      float minimum_water,
      terrain::SedimentDisposition sediment_at_termination,
      terrain::CarvingRule carving_rule) {
      constexpr float inertia = 0.05f;
      constexpr float capacity_k = 4.0f;
      constexpr float min_slope = 0.005f;
      constexpr float erode_k = 0.3f;
      constexpr float deposit_k = 0.3f;
      constexpr float evaporate = 0.015f;
      constexpr float gravity = 4.0f;
      // Interactive-trace momentum: rolling friction bleeds speed every
      // step so a coasting droplet cannot wander forever, climbing costs
      // extra energy beyond the plain gravity exchange, and below the
      // rest speed the droplet settles where it stands.
      constexpr float friction = 0.035f;
      constexpr float climb_drag = 3.0f;
      constexpr float rest_speed = 0.05f;
      if (max_steps <= 0 || !std::isfinite (minimum_water) ||
          minimum_water < 0.0f || minimum_water >= 1.0f)
        throw std::invalid_argument ("invalid traced hydraulic droplet");

      struct Sample {
        int x0, x1, y0, y1;
        float fx, fy, height, gradx, grady;
      };
      const int grid_width = periodic () ? unique_width () : m_width;
      const int grid_height = periodic () ? unique_height () : m_height;
      const auto sample = [&] (float x, float y) {
        Sample s;
        const float floor_x = std::floor (x);
        const float floor_y = std::floor (y);
        int ix = static_cast<int> (floor_x);
        int iy = static_cast<int> (floor_y);
        if (periodic ()) {
          s.x0 = terrain::wrap_index (ix, grid_width);
          s.y0 = terrain::wrap_index (iy, grid_height);
          s.x1 = terrain::wrap_index (s.x0 + 1, grid_width);
          s.y1 = terrain::wrap_index (s.y0 + 1, grid_height);
        } else {
          s.x0 = std::clamp (ix, 0, m_width - 2);
          s.y0 = std::clamp (iy, 0, m_height - 2);
          s.x1 = s.x0 + 1;
          s.y1 = s.y0 + 1;
        }
        s.fx = x - floor_x;
        s.fy = y - floor_y;
        const float h00 = get (s.x0, s.y0);
        const float h10 = get (s.x1, s.y0);
        const float h01 = get (s.x0, s.y1);
        const float h11 = get (s.x1, s.y1);
        s.gradx = (h10 - h00) * (1 - s.fy) + (h11 - h01) * s.fy;
        s.grady = (h01 - h00) * (1 - s.fx) + (h11 - h10) * s.fx;
        s.height = h00 * (1 - s.fx) * (1 - s.fy) + h10 * s.fx * (1 - s.fy) +
                   h01 * (1 - s.fx) * s.fy + h11 * s.fx * s.fy;
        return s;
      };
      const auto add =
        [&] (const Sample& s, float amount, float minimum_height) {
          float applied = 0.0f;
          const int xs[] = { s.x0, s.x1, s.x0, s.x1 };
          const int ys[] = { s.y0, s.y0, s.y1, s.y1 };
          const float weights[] = { (1 - s.fx) * (1 - s.fy),
                                    s.fx * (1 - s.fy),
                                    (1 - s.fx) * s.fy,
                                    s.fx * s.fy };
          for (int i = 0; i < 4; ++i) {
            float change = amount * weights[i];
            if (change < 0.0f)
              change = std::max (
                change, -std::max (0.0f, get (xs[i], ys[i]) - minimum_height));
            set (xs[i], ys[i], get (xs[i], ys[i]) + change);
            applied += change;
          }
          return applied;
        };

      if (periodic ()) {
        px = terrain::wrap_coordinate (px, static_cast<float> (grid_width));
        py = terrain::wrap_coordinate (py, static_cast<float> (grid_height));
      } else {
        px = std::clamp (px, 1.0f, static_cast<float> (m_width - 2));
        py = std::clamp (py, 1.0f, static_cast<float> (m_height - 2));
      }

      HydraulicDropletTrace trace;
      float dirx = 0.0f, diry = 0.0f;
      float speed = 1.0f, water = 1.0f, sediment = 0.0f;
      const Sample start = sample (px, py);
      trace.points.push_back (
        { px, py, start.height, 0, 0, speed, water, sediment });

      for (int step = 0; step < max_steps; ++step) {
        const Sample here = sample (px, py);
        // A fast droplet holds its heading; a slow one defers to the
        // gradient, so it turns downhill rather than plowing upslope.
        const float heading_inertia =
          std::clamp (inertia + speed * 0.45f, inertia, 0.85f);
        dirx = dirx * heading_inertia - here.gradx * (1 - heading_inertia);
        diry = diry * heading_inertia - here.grady * (1 - heading_inertia);
        const float length = std::hypot (dirx, diry);
        if (length < 1e-10f) {
          trace.termination = HydraulicDropletTermination::Flat;
          break;
        }
        dirx /= length;
        diry /= length;

        float nx = px + dirx, ny = py + diry;
        if (periodic ()) {
          nx = terrain::wrap_coordinate (nx, static_cast<float> (grid_width));
          ny = terrain::wrap_coordinate (ny, static_cast<float> (grid_height));
        } else if (nx < 1.0f || nx >= m_width - 2 || ny < 1.0f ||
                   ny >= m_height - 2) {
          trace.termination = HydraulicDropletTermination::Boundary;
          break;
        }

        const Sample next = sample (nx, ny);
        const float dh = next.height - here.height;
        if (dh > 0.0f && speed * speed < dh * gravity * climb_drag) {
          // Not enough kinetic energy to crest this rise: shed the
          // blocked momentum and slosh back; friction settles the
          // droplet at the basin floor within a few reversals.
          dirx = -dirx;
          diry = -diry;
          speed *= 0.5f;
          if (speed < rest_speed) {
            trace.termination = HydraulicDropletTermination::Settled;
            break;
          }
          trace.points.push_back (
            { px, py, here.height, 0, 0, speed, water, sediment });
          continue;
        }
        const float capacity =
          std::max (-dh, min_slope) * speed * water * capacity_k;
        float eroded = 0.0f, deposited = 0.0f;
        if (sediment > capacity || dh > 0.0f) {
          float amount = dh > 0.0f ? std::min (dh, sediment)
                                   : (sediment - capacity) * deposit_k;
          amount = add (here, amount, -std::numeric_limits<float>::infinity ());
          sediment -= amount;
          deposited = amount;
          trace.deposited += amount;
        } else {
          float amount = -std::min ((capacity - sediment) * erode_k, -dh);
          const float floor = carving_rule == terrain::CarvingRule::PathMonotone
                                ? next.height
                                : -std::numeric_limits<float>::infinity ();
          amount = add (here, amount, floor);
          sediment -= amount;
          eroded = -amount;
          trace.eroded -= amount;
        }

        const float slope_drag = dh > 0.0f ? climb_drag : 1.0f;
        speed = std::sqrt (
          std::max (0.0f, speed * speed - dh * gravity * slope_drag));
        speed *= 1.0f - friction;
        water *= 1.0f - evaporate;
        px = nx;
        py = ny;
        trace.points.push_back ({ px,
                                  py,
                                  sample (px, py).height,
                                  eroded,
                                  deposited,
                                  speed,
                                  water,
                                  sediment });
        if (speed < rest_speed && dh >= -1e-6f) {
          trace.termination = HydraulicDropletTermination::Settled;
          break;
        }
        if (minimum_water > 0.0f && water <= minimum_water) {
          trace.termination = HydraulicDropletTermination::WaterCutoff;
          break;
        }
        if (step == max_steps - 1)
          trace.termination = HydraulicDropletTermination::StepLimit;
      }

      if (sediment_at_termination == terrain::SedimentDisposition::Deposit &&
          sediment > 0.0f) {
        const Sample last = sample (px, py);
        const float deposited =
          add (last, sediment, -std::numeric_limits<float>::infinity ());
        trace.deposited += deposited;
        trace.points.back ().deposited += deposited;
        trace.points.back ().sediment = 0.0f;
      }
      synchronize_periodic_edges ();
      return trace;
    }

    terrain::HydraulicErosionReport RandomHeightMap::erode_hydraulically (
      std::mt19937& randomness,
      int droplets,
      int batch_size,
      int max_steps,
      float minimum_water,
      terrain::SedimentDisposition sediment_at_termination,
      terrain::CarvingRule carving_rule,
      const std::function<void (int, int)>& progress) {
      // Droplet erosion after Beyer (2015): each raindrop rolls
      // downhill, picking up sediment while it accelerates and
      // dropping it as it slows, carving gullies into mountainsides
      // and building alluvial flats where valleys open out.
      const float inertia = 0.05f;
      const float capacity_k = 4.0f;
      const float min_slope = 0.005f;
      const float erode_k = 0.3f;
      const float deposit_k = 0.3f;
      const float evaporate = 0.015f;
      const float gravity = 4.0f;
      if (droplets < 0 || batch_size <= 0 || max_steps <= 0 ||
          !std::isfinite (minimum_water) || minimum_water < 0.0f ||
          minimum_water >= 1.0f)
        throw std::invalid_argument (
          "hydraulic erosion batch size and lifetime must be positive");

      terrain::HydraulicErosionReport report;
      report.droplets = static_cast<std::uint64_t> (droplets);

      realgen_t g (randomness, 0, 1);

      if (periodic ()) {
        const int period_x = unique_width ();
        const int period_y = unique_height ();

        struct Sample {
          int x0, x1, y0, y1;
          float fx, fy, h00, h10, h01, h11;
          float height, gradx, grady;
        };
        const auto sample = [this, period_x, period_y] (float x, float y) {
          Sample s;
          const float floor_x = std::floor (x);
          const float floor_y = std::floor (y);
          s.x0 = terrain::wrap_index ((int)floor_x, period_x);
          s.y0 = terrain::wrap_index ((int)floor_y, period_y);
          s.x1 = terrain::wrap_index (s.x0 + 1, period_x);
          s.y1 = terrain::wrap_index (s.y0 + 1, period_y);
          s.fx = x - floor_x;
          s.fy = y - floor_y;
          s.h00 = get (s.x0, s.y0);
          s.h10 = get (s.x1, s.y0);
          s.h01 = get (s.x0, s.y1);
          s.h11 = get (s.x1, s.y1);
          s.gradx = (s.h10 - s.h00) * (1 - s.fy) + (s.h11 - s.h01) * s.fy;
          s.grady = (s.h01 - s.h00) * (1 - s.fx) + (s.h11 - s.h10) * s.fx;
          s.height = s.h00 * (1 - s.fx) * (1 - s.fy) +
                     s.h10 * s.fx * (1 - s.fy) + s.h01 * (1 - s.fx) * s.fy +
                     s.h11 * s.fx * s.fy;
          return s;
        };

        struct Droplet {
          float px, py;
          float dirx = 0, diry = 0;
          float speed = 1, water = 1, sediment = 0;
          bool active = true;
        };

        const std::size_t cell_count =
          static_cast<std::size_t> (m_width) * m_height;
        std::vector<float> changes (cell_count, 0.0f);
        std::vector<std::uint8_t> marked (cell_count, 0);
        std::vector<std::size_t> touched;
        touched.reserve (static_cast<std::size_t> (batch_size) * 16);

        const auto add_change =
          [&] (int x, int y, float amount, float minimum_height) {
            const std::size_t index =
              static_cast<std::size_t> (y) * m_width + x;
            if (!marked[index]) {
              marked[index] = 1;
              touched.push_back (index);
            }
            float applied = amount;
            if (amount < 0.0f) {
              const float current = get (x, y) + changes[index];
              const float available = std::max (0.0f, current - minimum_height);
              applied = std::max (amount, -available);
            }
            changes[index] += applied;
            return applied;
          };
        const auto add_at_sample =
          [&] (const Sample& at,
               float amount,
               float minimum_height =
                 -std::numeric_limits<float>::infinity ()) {
            float applied = 0.0f;
            applied += add_change (
              at.x0, at.y0, amount * (1 - at.fx) * (1 - at.fy), minimum_height);
            applied += add_change (
              at.x1, at.y0, amount * at.fx * (1 - at.fy), minimum_height);
            applied += add_change (
              at.x0, at.y1, amount * (1 - at.fx) * at.fy, minimum_height);
            applied +=
              add_change (at.x1, at.y1, amount * at.fx * at.fy, minimum_height);
            return applied;
          };
        const auto commit_changes = [&] {
          for (std::size_t index : touched) {
            const int x = static_cast<int> (index % m_width);
            const int y = static_cast<int> (index / m_width);
            set (x, y, get (x, y) + changes[index]);
            changes[index] = 0.0f;
            marked[index] = 0;
          }
        };
        const auto finish =
          [&] (Droplet& drop, const Sample& at, terrain::EventCount& reason) {
            if (sediment_at_termination ==
                terrain::SedimentDisposition::Deposit) {
              add_at_sample (at, drop.sediment);
              report.deposited += drop.sediment;
            } else {
              report.discarded_sediment += drop.sediment;
            }
            report.final_water += drop.water;
            ++reason;
            drop.sediment = 0.0f;
            drop.active = false;
          };

        for (int first = 0; first < droplets; first += batch_size) {
          const int count = std::min (batch_size, droplets - first);
          std::vector<Droplet> batch;
          batch.reserve (count);
          for (int i = 0; i < count; ++i)
            batch.push_back ({ .px = g () * period_x, .py = g () * period_y });

          for (int step = 0; step < max_steps; ++step) {
            bool any_active = false;
            touched.clear ();
            for (Droplet& drop : batch) {
              if (!drop.active)
                continue;
              const Sample here = sample (drop.px, drop.py);
              drop.dirx = drop.dirx * inertia - here.gradx * (1 - inertia);
              drop.diry = drop.diry * inertia - here.grady * (1 - inertia);
              const float len =
                std::sqrt (drop.dirx * drop.dirx + drop.diry * drop.diry);
              if (len < 1e-10f) {
                finish (drop, here, report.stopped_flat);
                continue;
              }
              drop.dirx /= len;
              drop.diry /= len;

              const float nx = terrain::wrap_coordinate (
                drop.px + drop.dirx, static_cast<float> (period_x));
              const float ny = terrain::wrap_coordinate (
                drop.py + drop.diry, static_cast<float> (period_y));
              const Sample next = sample (nx, ny);
              const float dh = next.height - here.height;
              if (carving_rule == terrain::CarvingRule::PathMonotone &&
                  dh >= 0.0f) {
                finish (drop, here, report.stopped_flat);
                continue;
              }
              const float capacity = std::max (-dh, min_slope) * drop.speed *
                                     drop.water * capacity_k;

              float amount;
              if (drop.sediment > capacity || dh > 0) {
                amount = dh > 0 ? std::min (dh, drop.sediment)
                                : (drop.sediment - capacity) * deposit_k;
                amount = add_at_sample (here, amount);
                drop.sediment -= amount;
                report.deposited += amount;
              } else {
                amount = -std::min ((capacity - drop.sediment) * erode_k, -dh);
                // A bilinear footprint may not undercut the elevation at which
                // the droplet hands off downstream. This keeps batched carving
                // path-monotone and prevents a trajectory from minting a pit
                // behind it.
                const float floor =
                  carving_rule == terrain::CarvingRule::PathMonotone
                    ? next.height
                    : -std::numeric_limits<float>::infinity ();
                amount = add_at_sample (here, amount, floor);
                drop.sediment -= amount;
                report.eroded -= amount;
              }

              drop.speed = std::sqrt (
                std::max (0.0f, drop.speed * drop.speed - dh * gravity));
              drop.water *= (1 - evaporate);
              drop.px = nx;
              drop.py = ny;
              ++report.steps;
              if (minimum_water > 0.0f && drop.water <= minimum_water)
                finish (drop, next, report.stopped_at_water_cutoff);
              else
                any_active = true;
            }

            commit_changes ();
            if (!any_active)
              break;
          }
          touched.clear ();
          for (Droplet& drop : batch)
            if (drop.active)
              finish (
                drop, sample (drop.px, drop.py), report.stopped_at_step_limit);
          commit_changes ();
          if (progress)
            progress (first + count, droplets);
        }
        synchronize_periodic_edges ();
        return report;
      }

      for (int d = 0; d < droplets; ++d) {
        float px = 1.0f + g () * (m_width - 3);
        float py = 1.0f + g () * (m_height - 3);
        float dirx = 0, diry = 0;
        float speed = 1, water = 1, sediment = 0;
        bool terminated = false;
        const auto finish = [&] (terrain::EventCount& reason) {
          if (sediment_at_termination ==
              terrain::SedimentDisposition::Deposit) {
            const int x = static_cast<int> (px);
            const int y = static_cast<int> (py);
            const float fx = px - x, fy = py - y;
            set (x, y, get (x, y) + sediment * (1 - fx) * (1 - fy));
            set (x + 1, y, get (x + 1, y) + sediment * fx * (1 - fy));
            set (x, y + 1, get (x, y + 1) + sediment * (1 - fx) * fy);
            set (x + 1, y + 1, get (x + 1, y + 1) + sediment * fx * fy);
            report.deposited += sediment;
          } else {
            report.discarded_sediment += sediment;
          }
          report.final_water += water;
          ++reason;
          sediment = 0.0f;
          terminated = true;
        };

        for (int step = 0; step < max_steps; ++step) {
          const int xi = (int)px, yi = (int)py;
          const float fx = px - xi, fy = py - yi;

          const float h00 = get (xi, yi);
          const float h10 = get (xi + 1, yi);
          const float h01 = get (xi, yi + 1);
          const float h11 = get (xi + 1, yi + 1);

          // Bilinear height and gradient under the droplet
          const float gradx = (h10 - h00) * (1 - fy) + (h11 - h01) * fy;
          const float grady = (h01 - h00) * (1 - fx) + (h11 - h10) * fx;
          const float height = h00 * (1 - fx) * (1 - fy) + h10 * fx * (1 - fy) +
                               h01 * (1 - fx) * fy + h11 * fx * fy;

          // Inertia blends old direction with the slope
          dirx = dirx * inertia - gradx * (1 - inertia);
          diry = diry * inertia - grady * (1 - inertia);
          const float len = std::sqrt (dirx * dirx + diry * diry);
          if (len < 1e-10f) {
            finish (report.stopped_flat);
            break;
          }
          dirx /= len;
          diry /= len;

          const float nx = px + dirx;
          const float ny = py + diry;
          if (nx < 1 || nx >= m_width - 2 || ny < 1 || ny >= m_height - 2) {
            finish (report.stopped_at_boundary);
            break;
          }

          const int nxi = (int)nx, nyi = (int)ny;
          const float nfx = nx - nxi, nfy = ny - nyi;
          const float nheight = get (nxi, nyi) * (1 - nfx) * (1 - nfy) +
                                get (nxi + 1, nyi) * nfx * (1 - nfy) +
                                get (nxi, nyi + 1) * (1 - nfx) * nfy +
                                get (nxi + 1, nyi + 1) * nfx * nfy;

          const float dh = nheight - height;
          if (carving_rule == terrain::CarvingRule::PathMonotone &&
              dh >= 0.0f) {
            finish (report.stopped_flat);
            break;
          }

          const float capacity =
            std::max (-dh, min_slope) * speed * water * capacity_k;

          if (sediment > capacity || dh > 0) {
            // Slowing down (or ran uphill into a pit): deposit
            const float amount = (dh > 0) ? std::min (dh, sediment)
                                          : (sediment - capacity) * deposit_k;
            sediment -= amount;
            report.deposited += amount;

            set (xi, yi, h00 + amount * (1 - fx) * (1 - fy));
            set (xi + 1, yi, h10 + amount * fx * (1 - fy));
            set (xi, yi + 1, h01 + amount * (1 - fx) * fy);
            set (xi + 1, yi + 1, h11 + amount * fx * fy);
          } else {
            // Accelerating: scoop material without leaving any node in the
            // bilinear footprint below the downstream handoff elevation.
            const float requested =
              std::min ((capacity - sediment) * erode_k, -dh);
            const float floor =
              carving_rule == terrain::CarvingRule::PathMonotone
                ? nheight
                : -std::numeric_limits<float>::infinity ();
            const float a00 = std::min (requested * (1 - fx) * (1 - fy),
                                        std::max (0.0f, h00 - floor));
            const float a10 = std::min (requested * fx * (1 - fy),
                                        std::max (0.0f, h10 - floor));
            const float a01 = std::min (requested * (1 - fx) * fy,
                                        std::max (0.0f, h01 - floor));
            const float a11 =
              std::min (requested * fx * fy, std::max (0.0f, h11 - floor));
            const float amount = a00 + a10 + a01 + a11;
            sediment += amount;
            report.eroded += amount;

            set (xi, yi, h00 - a00);
            set (xi + 1, yi, h10 - a10);
            set (xi, yi + 1, h01 - a01);
            set (xi + 1, yi + 1, h11 - a11);
          }

          speed = std::sqrt (std::max (0.0f, speed * speed - dh * gravity));
          water *= (1 - evaporate);
          px = nx;
          py = ny;
          ++report.steps;
          if (minimum_water > 0.0f && water <= minimum_water) {
            finish (report.stopped_at_water_cutoff);
            break;
          }
        }
        if (!terminated)
          finish (report.stopped_at_step_limit);
        if (progress && ((d + 1) % batch_size == 0 || d + 1 == droplets))
          progress (d + 1, droplets);
      }
      return report;
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
