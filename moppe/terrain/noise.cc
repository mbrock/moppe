#include <moppe/terrain/noise.hh>

#include <algorithm>
#include <array>
#include <cmath>
#include <random>

namespace moppe::terrain {
  namespace {
    std::uint32_t bounded_random (std::mt19937& rng, std::uint32_t bound) {
      // Match libc++'s unbiased bit-mask rejection, which the original
      // macOS generator used.  Keeping it explicit also makes geological
      // seeds independent of the standard-library implementation.
      std::uint32_t mask = bound - 1;
      mask |= mask >> 1;
      mask |= mask >> 2;
      mask |= mask >> 4;
      mask |= mask >> 8;
      mask |= mask >> 16;
      std::uint32_t value;
      do
        value = rng () & mask;
      while (value >= bound);
      return value;
    }

    int wrap_lattice (int value, int period) {
      const int wrapped = value % period;
      return (wrapped < 0 ? wrapped + period : wrapped) & 255;
    }

    float fade (float value) {
      return value * value * value * (value * (value * 6.0f - 15.0f) + 10.0f);
    }

    float lerp (float a, float b, float amount) {
      return a + amount * (b - a);
    }

    float gradient (int hash, float x, float y) {
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
  }

  PerlinPermutation make_perlin_permutation (std::uint32_t seed) {
    std::mt19937 rng (seed);
    std::array<std::int32_t, 256> shuffled;
    for (std::int32_t i = 0; i < 256; ++i)
      shuffled[static_cast<std::size_t> (i)] = i;
    for (int i = 255; i > 0; --i) {
      const int j = static_cast<int> (
        bounded_random (rng, static_cast<std::uint32_t> (i + 1)));
      std::swap (shuffled[static_cast<std::size_t> (i)],
                 shuffled[static_cast<std::size_t> (j)]);
    }

    PerlinPermutation result;
    for (int i = 0; i < 512; ++i)
      result[static_cast<std::size_t> (i)] =
        shuffled[static_cast<std::size_t> (i & 255)];
    return result;
  }

  noise_signal_t
  PeriodicNoise::fbm (float x, float y, const NoiseShape& parameters) const {
    float sum = 0.0f;
    float amplitude = 1.0f;
    float norm = 0.0f;
    float frequency = 1.0f;
    int period = parameters.cycles;
    for (int octave = 0; octave < parameters.octaves; ++octave) {
      sum += amplitude * noise (x * frequency, y * frequency, period, period);
      norm += amplitude;
      amplitude *= parameters.gain;
      frequency *= static_cast<float> (parameters.lacunarity);
      period *= parameters.lacunarity;
    }
    return sum / norm * noise_signal[mp_units::one];
  }

  noise_signal_t
  PeriodicNoise::ridged (float x, float y, const NoiseShape& parameters) const {
    float sum = 0.0f;
    float amplitude = 0.5f;
    float frequency = 1.0f;
    float weight = 1.0f;
    float norm = 0.0f;
    int period = parameters.cycles;
    for (int octave = 0; octave < parameters.octaves; ++octave) {
      float value =
        1.0f - std::fabs (noise (x * frequency, y * frequency, period, period));
      value *= value;
      value *= weight;
      weight = std::clamp (value * 2.0f, 0.0f, 1.0f);
      sum += value * amplitude;
      norm += amplitude;
      amplitude *= parameters.gain;
      frequency *= static_cast<float> (parameters.lacunarity);
      period *= parameters.lacunarity;
    }
    return sum / norm * noise_signal[mp_units::one];
  }

  float
  PeriodicNoise::noise (float x, float y, int period_x, int period_y) const {
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

    return lerp (lerp (gradient (aa, xf, yf), gradient (ba, xf - 1.0f, yf), u),
                 lerp (gradient (ab, xf, yf - 1.0f),
                       gradient (bb, xf - 1.0f, yf - 1.0f),
                       u),
                 v);
  }
}
