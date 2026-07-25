#include <moppe/terrain/geological.hh>

#include <moppe/terrain/noise.hh>

#include <algorithm>
#include <atomic>
#include <cmath>
#include <random>
#include <thread>
#include <vector>

namespace moppe::terrain {
  namespace {
    struct NoiseShape {
      int cycles;
      int octaves;
      int lacunarity;
      float gain;
    };

    struct GeologicalSeeds {
      Seed base;
      Seed ridge;
      Seed warp;
    };

    GeologicalSeeds derive_seeds (Seed root) {
      std::mt19937 rng (root.value);
      return { Seed { rng () }, Seed { rng () }, Seed { rng () } };
    }

    class PeriodicNoise {
    public:
      explicit PeriodicNoise (Seed seed)
          : m_permutation (make_perlin_permutation (seed.value)) {}

      float fbm (float x, float y, const NoiseShape& parameters) const {
        float sum = 0.0f;
        float amplitude = 1.0f;
        float norm = 0.0f;
        float frequency = 1.0f;
        int period = parameters.cycles;
        for (int octave = 0; octave < parameters.octaves; ++octave) {
          sum +=
            amplitude * noise (x * frequency, y * frequency, period, period);
          norm += amplitude;
          amplitude *= parameters.gain;
          frequency *= static_cast<float> (parameters.lacunarity);
          period *= parameters.lacunarity;
        }
        return sum / norm;
      }

      float ridged (float x, float y, const NoiseShape& parameters) const {
        float sum = 0.0f;
        float amplitude = 0.5f;
        float frequency = 1.0f;
        float weight = 1.0f;
        float norm = 0.0f;
        int period = parameters.cycles;
        for (int octave = 0; octave < parameters.octaves; ++octave) {
          float value =
            1.0f -
            std::fabs (noise (x * frequency, y * frequency, period, period));
          value *= value;
          value *= weight;
          weight = std::clamp (value * 2.0f, 0.0f, 1.0f);
          sum += value * amplitude;
          norm += amplitude;
          amplitude *= parameters.gain;
          frequency *= static_cast<float> (parameters.lacunarity);
          period *= parameters.lacunarity;
        }
        return sum / norm;
      }

    private:
      float noise (float x, float y, int period_x, int period_y) const {
        const float floor_x = std::floor (x);
        const float floor_y = std::floor (y);
        const int xi = wrap_lattice (static_cast<int> (floor_x), period_x);
        const int yi = wrap_lattice (static_cast<int> (floor_y), period_y);
        const int xj = wrap_lattice (static_cast<int> (floor_x) + 1, period_x);
        const int yj = wrap_lattice (static_cast<int> (floor_y) + 1, period_y);
        const float xf = x - floor_x;
        const float yf = y - floor_y;
        const float u = fade (xf);
        const float v = fade (yf);

        const int aa = m_permutation[m_permutation[xi] + yi];
        const int ab = m_permutation[m_permutation[xi] + yj];
        const int ba = m_permutation[m_permutation[xj] + yi];
        const int bb = m_permutation[m_permutation[xj] + yj];

        return lerp (
          lerp (gradient (aa, xf, yf), gradient (ba, xf - 1.0f, yf), u),
          lerp (gradient (ab, xf, yf - 1.0f),
                gradient (bb, xf - 1.0f, yf - 1.0f),
                u),
          v);
      }

      static int wrap_lattice (int value, int period) {
        const int wrapped = value % period;
        return (wrapped < 0 ? wrapped + period : wrapped) & 255;
      }

      static float fade (float value) {
        return value * value * value * (value * (value * 6.0f - 15.0f) + 10.0f);
      }

      static float lerp (float a, float b, float amount) {
        return a + amount * (b - a);
      }

      static float gradient (int hash, float x, float y) {
        switch (hash & 7) {
        case 0:
          return x + y;
        case 1:
          return x - y;
        case 2:
          return -x + y;
        case 3:
          return -x - y;
        case 4:
          return x;
        case 5:
          return -x;
        case 6:
          return y;
        default:
          return -y;
        }
      }

      PerlinPermutation m_permutation;
    };

    float smoothstep (float edge0, float edge1, float value) {
      float amount = (value - edge0) / (edge1 - edge0);
      amount = std::clamp (amount, 0.0f, 1.0f);
      return amount * amount * (3.0f - 2.0f * amount);
    }

    constexpr NoiseShape warp_shape { 3, 4, 2, 0.5f };
    constexpr NoiseShape continent_noise_shape { 3, 4, 2, 0.5f };
    constexpr NoiseShape plains_shape { 12, 4, 2, 0.5f };
    constexpr NoiseShape mountain_shape { 4, 6, 2, 0.55f };
  }

  GeologicalSections generate_geology (TerrainDomain domain,
                                       Seed seed,
                                       const GeologicalProgress& progress) {
    GeologicalSections result (std::move (domain));
    auto& continent_column = spatial::get<continent_shape> (result);
    auto& uplift_column = spatial::get<uplift_weight> (result);

    const GeologicalSeeds seeds = derive_seeds (seed);
    const PeriodicNoise base (seeds.base);
    const PeriodicNoise ridge (seeds.ridge);
    const PeriodicNoise warp (seeds.warp);
    const std::size_t width = result.domain ().width ();
    const std::size_t height = result.domain ().height ();
    const float inv_width = 1.0f / static_cast<float> (width);
    const float inv_height = 1.0f / static_cast<float> (height);
    std::atomic<std::size_t> next_row = 0;
    std::atomic<std::size_t> completed_rows = 0;

    const auto generate_rows = [&] {
      for (;;) {
        const std::size_t row =
          next_row.fetch_add (1, std::memory_order_relaxed);
        if (row >= height)
          break;
        const float v = static_cast<float> (row) * inv_height;
        for (std::size_t column = 0; column < width; ++column) {
          const float u = static_cast<float> (column) * inv_width;
          const float warp_cycles = static_cast<float> (warp_shape.cycles);
          const float warp_x = warp.fbm (
            u * warp_cycles + 11.3f, v * warp_cycles + 7.7f, warp_shape);
          const float warp_y = warp.fbm (
            u * warp_cycles + 91.1f, v * warp_cycles + 33.9f, warp_shape);
          const float warped_x = std::fma (0.15f, warp_x, u);
          const float warped_y = std::fma (0.15f, warp_y, v);

          const float continent_cycles =
            static_cast<float> (continent_noise_shape.cycles);
          const float continent =
            std::fma (base.fbm (warped_x * continent_cycles,
                                warped_y * continent_cycles,
                                continent_noise_shape),
                      0.5f,
                      0.5f);
          const float plains_cycles = static_cast<float> (plains_shape.cycles);
          const float plains = std::fma (base.fbm (warped_x * plains_cycles,
                                                   warped_y * plains_cycles,
                                                   plains_shape),
                                         0.5f,
                                         0.5f);
          const float mountain_cycles =
            static_cast<float> (mountain_shape.cycles);
          const float mountains = ridge.ridged (warped_x * mountain_cycles,
                                                warped_y * mountain_cycles,
                                                mountain_shape);
          const float mountain_mask = smoothstep (0.55f, 0.82f, continent);
          const float lowland = plains * 0.12f * (1.0f - mountain_mask);
          const float combined =
            std::fma (mountains * 0.45f,
                      mountain_mask,
                      std::fma (continent, 0.55f, lowland));

          const std::size_t offset = row * width + column;
          continent_column[offset] = continent * continent_shape[one];
          uplift_column[offset] =
            smoothstep (0.0f, 1.0f, combined) * uplift_weight[one];
        }
        const std::size_t completed =
          completed_rows.fetch_add (1, std::memory_order_relaxed) + 1;
        if (progress && (completed % 8 == 0 || completed == height))
          progress (completed, height);
      }
    };

    const std::size_t hardware_threads =
      std::max (1u, std::thread::hardware_concurrency ());
    const std::size_t available_threads =
      hardware_threads > 1 ? hardware_threads - 1 : 1;
    const std::size_t worker_count =
      result.size () < 65536 ? 1 : std::min (height, available_threads);
    if (worker_count == 1) {
      generate_rows ();
    } else {
      std::vector<std::jthread> workers;
      workers.reserve (worker_count - 1);
      for (std::size_t worker = 1; worker < worker_count; ++worker)
        workers.emplace_back (generate_rows);
      generate_rows ();
    }

    return result;
  }
}
