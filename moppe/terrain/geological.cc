#include <moppe/gfx/signal.hh>
#include <moppe/terrain/geological.hh>

#include <moppe/terrain/noise.hh>

#include <algorithm>
#include <atomic>
#include <cmath>
#include <random>
#include <stdexcept>
#include <thread>
#include <vector>

namespace moppe::terrain {
  namespace {
    struct GeologicalSeeds {
      Seed base;
      Seed ridge;
      Seed warp;
    };

    GeologicalSeeds derive_seeds (Seed root) {
      std::mt19937 rng (root.value);
      // mt19937 yields 32-bit values, but its result_type is wider on Linux.
      auto next = [&rng] { return static_cast<std::uint32_t> (rng ()); };
      return { Seed { next () }, Seed { next () }, Seed { next () } };
    }

    // The shapes are counted in cycles across the five kilometres the
    // world ordinarily runs.
    constexpr meters_t ordinary_period = 5000.0f * u::m;
    constexpr NoiseShape ordinary_warp_shape { 3, 4, 2, 0.5f };
    constexpr NoiseShape ordinary_continent_shape { 3, 4, 2, 0.5f };
    constexpr NoiseShape ordinary_plains_shape { 12, 4, 2, 0.5f };
    constexpr NoiseShape ordinary_mountain_shape { 4, 6, 2, 0.55f };

    // A wider world holds more country rather than the same country
    // stretched: each shape keeps its length on the ground, to the nearest
    // whole cycle, which a periodic world needs. The lattice wraps at 256,
    // so the finest octave bounds how wide that can go.
    NoiseShape across (NoiseShape shape, scale_factor_t scale) {
      shape.cycles = std::max (
        1,
        static_cast<int> (std::lround (static_cast<float> (shape.cycles) *
                                       scale.numerical_value_in (one))));
      int finest = shape.cycles;
      for (int octave = 1; octave < shape.octaves; ++octave)
        finest *= shape.lacunarity;
      if (finest > 256)
        throw std::invalid_argument ("the world is too wide for its geology");
      return shape;
    }

    // A place as a share of the lap, on the lattice of a shape whose
    // coarsest octave repeats `cycles` times around it.
    float on_lattice (proportion_t along, int cycles) {
      return along.numerical_value_in (one) * static_cast<float> (cycles);
    }

    // A signal of -1..1 folded onto 0..1.
    float folded (noise_signal_t signal) {
      return std::fma (signal.numerical_value_in (one), 0.5f, 0.5f);
    }

    // A place on the lap moved by `reach` times a warp signal, fused as
    // one fma. The warp's sample becomes a share of the lap here.
    proportion_t
    warped (proportion_t reach, noise_signal_t signal, proportion_t place) {
      return quantity_cast<proportion> (
        fma (quantity_cast<dimensionless> (reach),
             quantity_cast<dimensionless> (signal),
             quantity_cast<dimensionless> (place)));
    }
  }

  GeologicalSections generate_geology (TerrainDomain domain,
                                       Seed seed,
                                       const GeologicalProgress& progress) {
    GeologicalSections result (std::move (domain));
    auto& continent_column = spatial::get<continent_shape> (result);
    auto& uplift_column = spatial::get<uplift_weight> (result);

    const GeologicalSeeds seeds = derive_seeds (seed);
    const PeriodicNoise base (seeds.base.value);
    const PeriodicNoise ridge (seeds.ridge.value);
    const PeriodicNoise warp (seeds.warp.value);
    const std::size_t width = result.domain ().width ();
    const std::size_t height = result.domain ().height ();
    // Only a wider world: a narrower one, as the tests and laboratories
    // make them, is the ordinary country in small. In quarters, so a
    // period that does not divide evenly by its samples still counts as
    // the round number it was meant as.
    const scale_factor_t scale =
      std::max (
        1.0f,
        std::round ((result.domain ().period_x () / (0.25f * ordinary_period))
                      .numerical_value_in (one)) *
          0.25f) *
      scale_factor[one];
    const NoiseShape warp_shape = across (ordinary_warp_shape, scale);
    const NoiseShape continent_noise_shape =
      across (ordinary_continent_shape, scale);
    const NoiseShape plains_shape = across (ordinary_plains_shape, scale);
    const NoiseShape mountain_shape = across (ordinary_mountain_shape, scale);
    // The warp reaches as far over the ground as it ever did.
    const proportion_t warp_reach =
      0.15f / scale.numerical_value_in (one) * proportion[one];
    // The share of the lap one sample spans, across and down.
    const proportion_t inv_width =
      1.0f / static_cast<float> (width) * proportion[one];
    const proportion_t inv_height =
      1.0f / static_cast<float> (height) * proportion[one];
    std::atomic<std::size_t> next_row = 0;
    std::atomic<std::size_t> completed_rows = 0;

    const auto generate_rows = [&] {
      for (;;) {
        const std::size_t row =
          next_row.fetch_add (1, std::memory_order_relaxed);
        if (row >= height)
          break;
        const proportion_t v = static_cast<float> (row) * inv_height;
        for (std::size_t column = 0; column < width; ++column) {
          const proportion_t u = static_cast<float> (column) * inv_width;
          const int warp_cycles = warp_shape.cycles;
          const noise_signal_t warp_x =
            warp.fbm (on_lattice (u, warp_cycles) + 11.3f,
                      on_lattice (v, warp_cycles) + 7.7f,
                      warp_shape);
          const noise_signal_t warp_y =
            warp.fbm (on_lattice (u, warp_cycles) + 91.1f,
                      on_lattice (v, warp_cycles) + 33.9f,
                      warp_shape);
          const proportion_t warped_x = warped (warp_reach, warp_x, u);
          const proportion_t warped_y = warped (warp_reach, warp_y, v);

          const int continent_cycles = continent_noise_shape.cycles;
          const ContinentShape continent =
            folded (base.fbm (on_lattice (warped_x, continent_cycles),
                              on_lattice (warped_y, continent_cycles),
                              continent_noise_shape)) *
            continent_shape[one];
          const int plains_cycles = plains_shape.cycles;
          const proportion_t plains =
            folded (base.fbm (on_lattice (warped_x, plains_cycles),
                              on_lattice (warped_y, plains_cycles),
                              plains_shape)) *
            proportion[one];
          const int mountain_cycles = mountain_shape.cycles;
          const noise_signal_t mountains =
            ridge.ridged (on_lattice (warped_x, mountain_cycles),
                          on_lattice (warped_y, mountain_cycles),
                          mountain_shape);
          // The share of the ground the mountains stand on.
          const proportion_t mountain_mask =
            smoothstep (0.55f, 0.82f, continent.numerical_value_in (one)) *
            proportion[one];
          const UpliftWeight lowland =
            quantity_cast<uplift_weight> (quantity_cast<dimensionless> (
              plains * 0.12f * (1.0f * proportion[one] - mountain_mask)));
          const UpliftWeight combined = quantity_cast<uplift_weight> (
            fma (quantity_cast<dimensionless> (mountains * 0.45f),
                 quantity_cast<dimensionless> (mountain_mask),
                 quantity_cast<uplift_weight> (
                   fma (continent, 0.55f * one, lowland))));

          const std::size_t offset = row * width + column;
          continent_column[offset] = continent;
          uplift_column[offset] =
            smoothstep (0.0f, 1.0f, combined.numerical_value_in (one)) *
            uplift_weight[one];
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
