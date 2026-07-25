#include <moppe/terrain/geological.hh>

#include <moppe/terrain/noise.hh>

#include <algorithm>
#include <atomic>
#include <cmath>
#include <random>
#include <stdexcept>
#include <string>
#include <thread>
#include <vector>

namespace moppe::terrain {
  namespace {
    class PeriodicNoise {
    public:
      explicit PeriodicNoise (Seed seed)
          : m_permutation (make_perlin_permutation (seed.value)) {}

      float
      fbm (float x, float y, const FractalNoiseParameters& parameters) const {
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

      float ridged (float x,
                    float y,
                    const FractalNoiseParameters& parameters) const {
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

    void validate_noise (const FractalNoiseParameters& noise,
                         const char* name) {
      if (noise.cycles <= 0 || noise.octaves <= 0 || noise.lacunarity <= 0 ||
          !std::isfinite (noise.gain) || noise.gain <= 0.0f)
        throw std::invalid_argument (std::string (name) +
                                     " noise parameters are invalid");

      int octave_cycles = noise.cycles;
      for (int octave = 1; octave < noise.octaves; ++octave) {
        if (octave_cycles > 256 / noise.lacunarity)
          throw std::invalid_argument (std::string (name) +
                                       " noise exceeds the 256-cell lattice");
        octave_cycles *= noise.lacunarity;
      }
    }

    void require_finite (float value, const char* name) {
      if (!std::isfinite (value))
        throw std::invalid_argument (std::string (name) + " must be finite");
    }
  }

  GeologicalSeeds derive_geological_seeds (std::uint32_t root_seed) {
    std::mt19937 rng (root_seed);
    return { .base = Seed { rng () },
             .ridge = Seed { rng () },
             .warp = Seed { rng () } };
  }

  GeologicalRecipe make_geological_recipe (std::uint32_t root_seed) {
    return { .seeds = derive_geological_seeds (root_seed) };
  }

  void validate_geological_recipe (const GeologicalRecipe& recipe) {
    validate_noise (recipe.warp.noise, "warp");
    validate_noise (recipe.continent.noise, "continent");
    validate_noise (recipe.plains.noise, "plains");
    validate_noise (recipe.mountains, "mountains");
    require_finite (recipe.warp.amplitude, "warp amplitude");
    require_finite (recipe.warp.x_offset.x, "warp X offset X");
    require_finite (recipe.warp.x_offset.y, "warp X offset Y");
    require_finite (recipe.warp.y_offset.x, "warp Y offset X");
    require_finite (recipe.warp.y_offset.y, "warp Y offset Y");
    require_finite (recipe.continent.scale, "continent scale");
    require_finite (recipe.continent.bias, "continent bias");
    require_finite (recipe.plains.scale, "plains scale");
    require_finite (recipe.plains.bias, "plains bias");
    require_finite (recipe.blend.continent_weight, "continent weight");
    require_finite (recipe.blend.plains_weight, "plains weight");
    require_finite (recipe.blend.mountain_weight, "mountain weight");
    if (!std::isfinite (recipe.blend.mask_low) ||
        !std::isfinite (recipe.blend.mask_high) ||
        recipe.blend.mask_high <= recipe.blend.mask_low)
      throw std::invalid_argument ("mountain mask edges must increase");
  }

  GeologicalSections generate_geology (TerrainDomain domain,
                                       const GeologicalRecipe& recipe,
                                       const GeologicalProgress& progress) {
    validate_geological_recipe (recipe);
    GeologicalSections result (std::move (domain));
    auto& continent_column = spatial::get<continent_shape> (result);
    auto& uplift_column = spatial::get<uplift_weight> (result);

    const PeriodicNoise base (recipe.seeds.base);
    const PeriodicNoise ridge (recipe.seeds.ridge);
    const PeriodicNoise warp (recipe.seeds.warp);
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
          const float warp_cycles =
            static_cast<float> (recipe.warp.noise.cycles);
          const float warp_x =
            warp.fbm (u * warp_cycles + recipe.warp.x_offset.x,
                      v * warp_cycles + recipe.warp.x_offset.y,
                      recipe.warp.noise);
          const float warp_y =
            warp.fbm (u * warp_cycles + recipe.warp.y_offset.x,
                      v * warp_cycles + recipe.warp.y_offset.y,
                      recipe.warp.noise);
          const float warped_x = std::fma (recipe.warp.amplitude, warp_x, u);
          const float warped_y = std::fma (recipe.warp.amplitude, warp_y, v);

          const float continent_cycles =
            static_cast<float> (recipe.continent.noise.cycles);
          const float continent =
            std::fma (base.fbm (warped_x * continent_cycles,
                                warped_y * continent_cycles,
                                recipe.continent.noise),
                      recipe.continent.scale,
                      recipe.continent.bias);
          const float plains_cycles =
            static_cast<float> (recipe.plains.noise.cycles);
          const float plains = std::fma (base.fbm (warped_x * plains_cycles,
                                                   warped_y * plains_cycles,
                                                   recipe.plains.noise),
                                         recipe.plains.scale,
                                         recipe.plains.bias);
          const float mountain_cycles =
            static_cast<float> (recipe.mountains.cycles);
          const float mountains = ridge.ridged (warped_x * mountain_cycles,
                                                warped_y * mountain_cycles,
                                                recipe.mountains);
          const float mountain_mask = smoothstep (
            recipe.blend.mask_low, recipe.blend.mask_high, continent);
          const float lowland =
            plains * recipe.blend.plains_weight * (1.0f - mountain_mask);
          const float combined = std::fma (
            mountains * recipe.blend.mountain_weight,
            mountain_mask,
            std::fma (continent, recipe.blend.continent_weight, lowland));

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
