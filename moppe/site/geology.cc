// The website's geology (docs/site.md, geology.hh). In the browser it runs
// in a Web Worker and hands each step to the page
// (moppe/site/geology-worker.js).

#include <moppe/map/surface.hh>
#include <moppe/site/geology.hh>
#include <moppe/terrain/world_recipe.hh>

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <span>
#include <vector>

#ifdef __EMSCRIPTEN__
#include <emscripten/emscripten.h>
#endif

namespace {
  using namespace moppe;

  struct Rgb {
    float r, g, b;
  };

  Rgb mix (Rgb a, Rgb b, float t) {
    return { a.r + (b.r - a.r) * t,
             a.g + (b.g - a.g) * t,
             a.b + (b.b - a.b) * t };
  }

  float smooth (float low, float high, float value) {
    const float t = std::clamp ((value - low) / (high - low), 0.0f, 1.0f);
    return t * t * (3.0f - 2.0f * t);
  }

  // The map sheet's inks (moppe/game/relief_map.cc), with the forest left
  // out: no tree has grown yet.
  constexpr Rgb deeps { 0.520f, 0.720f, 0.880f };
  constexpr Rgb shallows { 0.730f, 0.865f, 0.940f };
  constexpr Rgb lowland { 0.955f, 0.930f, 0.800f };
  constexpr Rgb upland { 0.925f, 0.870f, 0.730f };
  constexpr Rgb fell { 0.900f, 0.860f, 0.790f };
  constexpr Rgb summit { 0.965f, 0.960f, 0.945f };
  constexpr Rgb shadow_ink { 0.360f, 0.340f, 0.430f };
  constexpr Rgb contour_ink { 0.560f, 0.360f, 0.200f };

  // The world's land in the frame of its picture: heights over the
  // periodic lattice, read between samples.
  struct Heights {
    std::span<const float> values;
    int size = 0;

    float at (int column, int row) const {
      column = ((column % size) + size) % size;
      row = ((row % size) + size) % size;
      return values[static_cast<std::size_t> (row) * size + column];
    }

    float sample (float x, float z) const {
      const int column = static_cast<int> (std::floor (x));
      const int row = static_cast<int> (std::floor (z));
      const float tx = x - static_cast<float> (column);
      const float tz = z - static_cast<float> (row);
      const float north =
        at (column, row) + (at (column + 1, row) - at (column, row)) * tx;
      const float south =
        at (column, row + 1) +
        (at (column + 1, row + 1) - at (column, row + 1)) * tx;
      return north + (south - north) * tz;
    }
  };

  // Relief shaded from the north-west over a tint by height, the sea by
  // depth, and a contour every 20 m. The tint's scale is fixed, so the
  // country is seen to rise and wear down rather than be restretched.
  void paint (Heights heights,
              float cell_m,
              float sea_level_m,
              int size,
              std::vector<std::uint8_t>& rgba) {
    rgba.resize (static_cast<std::size_t> (size) * size * 4);
    const float scale = static_cast<float> (heights.size) / size;
    const float light_x = -0.577f, light_y = 0.577f, light_z = -0.577f;
    for (int y = 0; y < size; ++y)
      for (int x = 0; x < size; ++x) {
        const float u = (static_cast<float> (x) + 0.5f) * scale - 0.5f;
        const float v = (static_cast<float> (y) + 0.5f) * scale - 0.5f;
        const float h = heights.sample (u, v);
        const float dx =
          (heights.sample (u + 0.5f, v) - heights.sample (u - 0.5f, v)) /
          (0.5f * cell_m);
        const float dz =
          (heights.sample (u, v + 0.5f) - heights.sample (u, v - 0.5f)) /
          (0.5f * cell_m);
        // The surface's normal is (-dx, 1, -dz), unnormalised.
        const float length = std::sqrt (dx * dx + 1.0f + dz * dz);
        const float lit =
          (-dx * light_x + light_y - dz * light_z) / length / light_y;

        const float above = h - sea_level_m;
        Rgb colour;
        if (above < 0.0f) {
          colour = mix (shallows, deeps, smooth (0.0f, 120.0f, -above));
        } else {
          colour = mix (lowland, upland, smooth (0.0f, 90.0f, above));
          colour = mix (colour, fell, smooth (90.0f, 200.0f, above));
          colour = mix (colour, summit, smooth (200.0f, 320.0f, above));
          const float shade = std::clamp (1.0f - lit, -0.6f, 1.0f);
          colour = shade > 0.0f
                     ? mix (colour, shadow_ink, 0.55f * shade)
                     : mix (colour, Rgb { 1.0f, 0.985f, 0.94f }, -shade);
          // A contour where a 20 m level falls within this pixel, left out
          // where the levels crowd closer than a few pixels.
          const float rise_per_pixel =
            std::sqrt (dx * dx + dz * dz) * cell_m * scale;
          const float spacing = 20.0f / std::max (rise_per_pixel, 1e-3f);
          const float level = above / 20.0f;
          const float distance =
            std::abs (level - std::round (level)) * spacing;
          colour = mix (colour,
                        contour_ink,
                        0.5f * smooth (2.0f, 5.0f, spacing) *
                          (1.0f - smooth (0.3f, 1.0f, distance)));
        }
        const std::size_t offset =
          (static_cast<std::size_t> (y) * size + x) * 4;
        rgba[offset + 0] = static_cast<std::uint8_t> (
          std::clamp (colour.r, 0.0f, 1.0f) * 255.0f + 0.5f);
        rgba[offset + 1] = static_cast<std::uint8_t> (
          std::clamp (colour.g, 0.0f, 1.0f) * 255.0f + 0.5f);
        rgba[offset + 2] = static_cast<std::uint8_t> (
          std::clamp (colour.b, 0.0f, 1.0f) * 255.0f + 0.5f);
        rgba[offset + 3] = 255;
      }
  }

}

namespace moppe::site {
  void grow_land (int lattice, std::uint32_t seed, const GrowthStep& step) {
    using namespace moppe::terrain;
    // The game's default extent and datum (moppe/terrain/land_bake.cc).
    const WorldRecipe recipe =
      make_world_recipe (spatial_extent_in_metres (
                           moppe::Vec3 (land_period_m, 320.0f, land_period_m)),
                         lattice,
                         Seed { seed },
                         land_sea_level_m * u::m,
                         TerrainGenerationProfile::Play);
    map::SurfaceGeometry surface (
      TerrainDomain (static_cast<std::size_t> (lattice),
                     static_cast<std::size_t> (lattice),
                     recipe.extent ()));
    const double step_years = recipe.evolution ().time_step.numerical_value_in (
      mp_units::astronomy::Julian_year);
    const auto as_floats = [] (std::span<const SurfaceElevation> heights) {
      static_assert (sizeof (SurfaceElevation) == sizeof (float));
      return std::span<const float> (
        reinterpret_cast<const float*> (heights.data ()), heights.size ());
    };

    const auto uplift =
      map::initialize_terrain (surface, recipe.seed (), recipe.water_datum ());
    const int steps = static_cast<int> (
      std::ceil (recipe.evolution ().duration.numerical_value_in (
                   mp_units::astronomy::Julian_year) /
                 step_years));
    step (0, steps, 0.0, as_floats (spatial::get<surface_elevation> (surface)));
    map::evolve_terrain (
      surface,
      uplift,
      recipe.evolution (),
      [&] (IterationCount done,
           IterationCount total,
           std::span<const SurfaceElevation> heights) {
        const int count =
          static_cast<int> (done.numerical_value_in (mp_units::one));
        step (count,
              static_cast<int> (total.numerical_value_in (mp_units::one)),
              count * step_years,
              as_floats (heights));
      });
  }

  void paint_relief (std::span<const float> heights,
                     int lattice,
                     int size,
                     std::vector<std::uint8_t>& rgba) {
    paint ({ heights, lattice },
           land_period_m / static_cast<float> (lattice),
           land_sea_level_m,
           size,
           rgba);
  }
}

#ifdef __EMSCRIPTEN__
namespace {
  // Hands a step to the page; the worker script (geology-worker.js)
  // defines Module.step. The picture is there only when asked for.
  EM_JS (void,
         post_step,
         (int step,
          int steps,
          double years,
          const float* heights,
          int lattice,
          const std::uint8_t* rgba,
          int size),
         {
           const count = lattice * lattice;
           Module.step (step,
                        steps,
                        years,
                        HEAPF32.slice (heights >> 2, (heights >> 2) + count),
                        lattice,
                        rgba ? HEAPU8.slice (rgba, rgba + size * size * 4)
                             : null,
                        size);
         });
}

// Grows world `seed` on a `lattice`; SIZE, when not zero, also paints each
// step as a picture that size (for a page without WebGPU).
extern "C" EMSCRIPTEN_KEEPALIVE void
moppe_geology_grow (int lattice, unsigned seed, int size) {
  std::vector<std::uint8_t> rgba;
  moppe::site::grow_land (
    lattice,
    seed,
    [&] (int step, int steps, double years, std::span<const float> heights) {
      if (size > 0)
        moppe::site::paint_relief (heights, lattice, size, rgba);
      post_step (step,
                 steps,
                 years,
                 heights.data (),
                 lattice,
                 size > 0 ? rgba.data () : nullptr,
                 size);
    });
}
#endif
