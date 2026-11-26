#include <moppe/correct_math.hh>
#include <moppe/gfx/signal.hh>
#include <moppe/terrain/geological.hh>

#include <moppe/terrain/noise.hh>

#include <algorithm>
#include <atomic>
#include <cmath>
#include <numbers>
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

    // Cycles are counted across the original five-kilometre domain. At ten
    // kilometres the finest mountain octave uses all 256 lattice entries.
    constexpr meters_t ordinary_period = 5000.0f * u::m;
    constexpr meters_t legacy_maximum_period = 10000.0f * u::m;
    constexpr NoiseShape ordinary_warp_shape { 3, 4, 2, 0.5f };
    constexpr NoiseShape ordinary_continent_shape { 3, 4, 2, 0.5f };
    constexpr NoiseShape ordinary_plains_shape { 12, 4, 2, 0.5f };
    constexpr NoiseShape ordinary_mountain_shape { 4, 6, 2, 0.55f };

    // Macro tectonic noise shapes for very large worlds (> 10 km): wavelengths
    // grow with the torus so a 20-40 km world holds broad continents, basins,
    // and multi-kilometre ranges instead of repeating 5 km hills.
    constexpr NoiseShape macro_warp_shape { 2, 4, 2, 0.5f };
    constexpr NoiseShape macro_continent_shape { 2, 5, 2, 0.5f };
    constexpr NoiseShape macro_plains_shape { 8, 4, 2, 0.5f };
    constexpr NoiseShape macro_mountain_shape { 4, 6, 2, 0.55f };

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

    // Minimum-image signed offset on a unit periodic lap in [-0.5, 0.5].
    proportion_t wrap_lap_delta (proportion_t delta) {
      const float raw = delta.numerical_value_in (one);
      return (raw - std::floor (raw + 0.5f)) * proportion[one];
    }

    struct MacroMassif {
      proportion_t center_u = 0.25f * proportion[one];
      proportion_t center_v = 0.25f * proportion[one];
      proportion_t cos_angle = 1.0f * proportion[one];
      proportion_t sin_angle = 0.0f * proportion[one];
      proportion_t radius_along = 0.34f * proportion[one];
      proportion_t radius_across = 0.21f * proportion[one];
      scale_factor_t peak_boost = 1.0f * scale_factor[one];
    };

    MacroMassif make_macro_massif (Seed seed, meters_t period) {
      std::mt19937 rng (seed.value ^ 0x51a9d0e3u);
      const auto unit = [&rng] {
        return static_cast<float> (rng () >> 8) * (1.0f / 16777216.0f);
      };
      const radians_t angle =
        unit () * 2.0f * std::numbers::pi_v<float> * u::rad;
      const scale_factor_t extent_ratio =
        quantity_cast<scale_factor> (period / legacy_maximum_period);
      // Grow peak tectonic uplift smoothly from ~1.5x at 20 km to ~2.4x at
      // 40 km so a broad tectonic range rises ~1,000 m over the sea while
      // foothills and foreland valleys grade gently over tens of kilometres.
      const proportion_t growth =
        std::clamp (
          (extent_ratio.numerical_value_in (one) - 1.0f) / 3.0f, 0.0f, 1.5f) *
        proportion[one];
      return {
        .center_u = unit () * proportion[one],
        .center_v = unit () * proportion[one],
        .cos_angle = cr::cos (radians_value (angle)) * proportion[one],
        .sin_angle = cr::sin (radians_value (angle)) * proportion[one],
        .radius_along = 0.36f * proportion[one],
        .radius_across = 0.22f * proportion[one],
        .peak_boost =
          (0.85f + 1.35f * growth.numerical_value_in (one)) * scale_factor[one],
      };
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
    const meters_t period =
      std::max (result.domain ().period_x (), result.domain ().period_z ());
    const bool macro_world = period > legacy_maximum_period;
    const MacroMassif massif = make_macro_massif (seeds.ridge, period);
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
    const NoiseShape warp_shape =
      macro_world ? macro_warp_shape : across (ordinary_warp_shape, scale);
    const NoiseShape continent_noise_shape =
      macro_world ? macro_continent_shape
                  : across (ordinary_continent_shape, scale);
    const NoiseShape plains_shape =
      macro_world ? macro_plains_shape : across (ordinary_plains_shape, scale);
    const NoiseShape mountain_shape =
      macro_world ? macro_mountain_shape
                  : across (ordinary_mountain_shape, scale);
    // The warp reaches as far over the ground as it ever did.
    const proportion_t warp_reach =
      (macro_world ? 0.13f : 0.15f / scale.numerical_value_in (one)) *
      proportion[one];
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
          ContinentShape continent =
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

          const std::size_t offset = row * width + column;
          if (!macro_world) {
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
            continent_column[offset] = continent;
            uplift_column[offset] =
              smoothstep (0.0f, 1.0f, combined.numerical_value_in (one)) *
              uplift_weight[one];
          } else {
            // Broad tectonic mountain belt, stepping down through warped
            // foothills into long tilted river valleys and a wide antipodal
            // coastal/fjord sea basin.
            const proportion_t du = wrap_lap_delta (warped_x - massif.center_u);
            const proportion_t dv = wrap_lap_delta (warped_y - massif.center_v);
            const proportion_t along =
              (du.numerical_value_in (one) *
                 massif.cos_angle.numerical_value_in (one) +
               dv.numerical_value_in (one) *
                 massif.sin_angle.numerical_value_in (one)) *
              proportion[one];
            const proportion_t across =
              (-du.numerical_value_in (one) *
                 massif.sin_angle.numerical_value_in (one) +
               dv.numerical_value_in (one) *
                 massif.cos_angle.numerical_value_in (one)) *
              proportion[one];
            const scale_factor_t range_r =
              cr::hypot (
                (along / massif.radius_along).numerical_value_in (one),
                (across / massif.radius_across).numerical_value_in (one)) *
              scale_factor[one];
            const proportion_t range_bell =
              smoothstep (1.0f, 0.0f, range_r.numerical_value_in (one)) *
              proportion[one];
            // A broad outer foothill apron and a multi-peaked inner massif
            // sculpted by the warped continent and ridged mountain fields.
            const proportion_t ridge_val =
              std::clamp (mountains.numerical_value_in (one), 0.0f, 1.0f) *
              proportion[one];
            const proportion_t inner_massif =
              smoothstep (
                0.28f,
                0.92f,
                range_bell.numerical_value_in (one) *
                  (0.65f + 0.55f * continent.numerical_value_in (one))) *
              proportion[one];

            const proportion_t bu = wrap_lap_delta (
              warped_x - (massif.center_u + 0.5f * proportion[one]));
            const proportion_t bv = wrap_lap_delta (
              warped_y - (massif.center_v + 0.5f * proportion[one]));
            const scale_factor_t basin_r =
              cr::hypot (bu.numerical_value_in (one) / 0.30f,
                         bv.numerical_value_in (one) / 0.26f) *
              scale_factor[one];
            const proportion_t basin =
              smoothstep (1.0f, 0.0f, basin_r.numerical_value_in (one)) *
              proportion[one];

            const ContinentShape shaped_continent =
              std::clamp (continent.numerical_value_in (one) +
                            0.28f * range_bell.numerical_value_in (one) -
                            0.34f * basin.numerical_value_in (one),
                          0.0f,
                          1.0f) *
              continent_shape[one];
            continent = shaped_continent;

            const proportion_t regional_mask =
              smoothstep (
                0.42f, 0.82f, shaped_continent.numerical_value_in (one)) *
              proportion[one];
            // Foreland rises smoothly from sea level (no coastal escarpment)
            // through wooded hills and secondary ridges toward the main
            // tectonic mountain belt, so trunk valleys run gently to the sea.
            const proportion_t above_coast =
              smoothstep (
                0.36f, 0.78f, shaped_continent.numerical_value_in (one)) *
              proportion[one];
            const UpliftWeight foreland =
              (0.28f * above_coast.numerical_value_in (one) +
               0.26f * range_bell.numerical_value_in (one) *
                 above_coast.numerical_value_in (one) +
               0.14f * plains.numerical_value_in (one) *
                 above_coast.numerical_value_in (one) *
                 (1.0f - inner_massif.numerical_value_in (one)) +
               0.48f * ridge_val.numerical_value_in (one) *
                 regional_mask.numerical_value_in (one)) *
              uplift_weight[one];
            const UpliftWeight massif_core =
              (inner_massif.numerical_value_in (one) *
               massif.peak_boost.numerical_value_in (one) *
               (0.40f + 0.90f * ridge_val.numerical_value_in (one))) *
              uplift_weight[one];
            continent_column[offset] = continent;
            uplift_column[offset] =
              std::max (0.0f,
                        foreland.numerical_value_in (one) +
                          massif_core.numerical_value_in (one)) *
              uplift_weight[one];
          }
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
