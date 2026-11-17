#ifndef MOPPE_TERRAIN_NOISE_HH
#define MOPPE_TERRAIN_NOISE_HH

#include <moppe/gfx/math.hh>

#include <array>
#include <cstdint>

namespace moppe::terrain {
  using PerlinPermutation = std::array<std::int32_t, 512>;

  // Produces the canonical permutation table shared by evaluator backends.
  // Keeping this host-side step common makes CPU and GPU noise differ only
  // in floating-point evaluation, not in their seeded lattice.
  PerlinPermutation make_perlin_permutation (std::uint32_t seed);

  // How a periodic fractal is built: cycles of its coarsest octave across
  // the lattice's period, how many octaves, how much finer each is, and
  // how much weaker.
  struct NoiseShape {
    int cycles;
    int octaves;
    int lacunarity;
    float gain;
  };

  // Gradient noise on a lattice that wraps after a whole number of cells,
  // summed in octaves. Places are lattice coordinates: unitless, counted
  // in cells of the coarsest octave. What comes out is a noise signal.
  class PeriodicNoise {
  public:
    explicit PeriodicNoise (std::uint32_t seed)
        : m_permutation (make_perlin_permutation (seed)) {}

    noise_signal_t fbm (float x, float y, const NoiseShape& parameters) const;
    noise_signal_t
    ridged (float x, float y, const NoiseShape& parameters) const;

  private:
    float noise (float x, float y, int period_x, int period_y) const;

    PerlinPermutation m_permutation;
  };
}

#endif
