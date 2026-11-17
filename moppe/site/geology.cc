// The website's geology (docs/site.md, geology.hh). In the browser it runs
// in a Web Worker and hands each step to the page
// (moppe/site/geology-worker.js).

#include <moppe/color.hh>
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
  using terrain::ElevationChange;
  using terrain::SurfaceElevation;

  // Smoothstep from LOW to HIGH, in whatever the three are measured in
  // (metres above the sea, pixels across the picture).
  template <typename Q>
  blend_weight_t smooth (Q low, Q high, Q value) {
    const float t = std::clamp (
      static_cast<float> ((value - low) / (high - low)), 0.0f, 1.0f);
    return t * t * (3.0f - 2.0f * t) * blend_weight[one];
  }

  // The map sheet's inks (moppe/game/relief_map.cc), with the forest left
  // out: no tree has grown yet.
  constexpr DisplayColor deeps { 0.520f, 0.720f, 0.880f };
  constexpr DisplayColor shallows { 0.730f, 0.865f, 0.940f };
  constexpr DisplayColor lowland { 0.955f, 0.930f, 0.800f };
  constexpr DisplayColor upland { 0.925f, 0.870f, 0.730f };
  constexpr DisplayColor fell { 0.900f, 0.860f, 0.790f };
  constexpr DisplayColor summit { 0.965f, 0.960f, 0.945f };
  constexpr DisplayColor shadow_ink { 0.360f, 0.340f, 0.430f };
  constexpr DisplayColor contour_ink { 0.560f, 0.360f, 0.200f };

  // The world's land in the frame of its picture: heights over the
  // periodic lattice (raw metres, the raster's lanes), read between
  // samples at lattice coordinates.
  struct Heights {
    std::span<const float> values;
    int size = 0;

    SurfaceElevation at (int column, int row) const {
      column = ((column % size) + size) % size;
      row = ((row % size) + size) % size;
      return terrain::surface_elevation_point (
        values[static_cast<std::size_t> (row) * size + column] * u::m);
    }

    SurfaceElevation sample (float x, float z) const {
      const int column = static_cast<int> (std::floor (x));
      const int row = static_cast<int> (std::floor (z));
      const float tx = x - static_cast<float> (column);
      const float tz = z - static_cast<float> (row);
      const SurfaceElevation north =
        at (column, row) + (at (column + 1, row) - at (column, row)) * tx;
      const SurfaceElevation south =
        at (column, row + 1) +
        (at (column + 1, row + 1) - at (column, row + 1)) * tx;
      return north + (south - north) * tz;
    }
  };

  // Relief shaded from the north-west over a tint by height, the sea by
  // depth, and a contour every 20 m. The tint's scale is fixed, so the
  // country is seen to rise and wear down rather than be restretched.
  void paint (Heights heights,
              meters_t cell,
              SurfaceElevation sea_level,
              int size,
              std::vector<std::uint8_t>& rgba) {
    rgba.resize (static_cast<std::size_t> (size) * size * 4);
    // Lattice samples per pixel.
    const float scale = static_cast<float> (heights.size) / size;
    // Toward the light, from the north-west and up (unnormalised as
    // the sheet's own shading has it).
    const direction_t light = unit_direction (Vec3 (-0.577f, 0.577f, -0.577f));
    const ElevationChange interval = 20.0f * u::m;
    for (int y = 0; y < size; ++y)
      for (int x = 0; x < size; ++x) {
        const float u = (static_cast<float> (x) + 0.5f) * scale - 0.5f;
        const float v = (static_cast<float> (y) + 0.5f) * scale - 0.5f;
        const SurfaceElevation h = heights.sample (u, v);
        // The ground's rise across one sample, east and south.
        const auto rise = [&] (SurfaceElevation ahead, SurfaceElevation back) {
          return quantity_cast<grade> (
            quantity_cast<isq::height> (ahead - back) / (0.5f * cell));
        };
        const grade_t dx =
          rise (heights.sample (u + 0.5f, v), heights.sample (u - 0.5f, v));
        const grade_t dz =
          rise (heights.sample (u, v + 0.5f), heights.sample (u, v - 0.5f));
        // The surface's normal is (-dx, 1, -dz), unnormalised.
        const float gx = dx.numerical_value_in (one);
        const float gz = dz.numerical_value_in (one);
        const float length = std::sqrt (gx * gx + 1.0f + gz * gz);
        const float lit =
          (-gx * direction_value (light)[0] + direction_value (light)[1] -
           gz * direction_value (light)[2]) /
          length / direction_value (light)[1];

        const ElevationChange above = h - sea_level;
        DisplayColor colour;
        if (above < ElevationChange::zero ()) {
          colour = mix_display (
            shallows,
            deeps,
            smooth<ElevationChange> (0.0f * u::m, 120.0f * u::m, -above));
        } else {
          const auto band = [&] (float low, float high) {
            return smooth<ElevationChange> (low * u::m, high * u::m, above);
          };
          colour = mix_display (lowland, upland, band (0.0f, 90.0f));
          colour = mix_display (colour, fell, band (90.0f, 200.0f));
          colour = mix_display (colour, summit, band (200.0f, 320.0f));
          const float shade = std::clamp (1.0f - lit, -0.6f, 1.0f);
          colour = shade > 0.0f
                     ? mix_display (
                         colour, shadow_ink, 0.55f * shade * blend_weight[one])
                     : mix_display (colour,
                                    DisplayColor { 1.0f, 0.985f, 0.94f },
                                    -shade * blend_weight[one]);
          // A contour where a 20 m level falls within this pixel, left out
          // where the levels crowd closer than a few pixels.
          const meters_t rise_per_pixel =
            std::sqrt (gx * gx + gz * gz) * cell * scale;
          // Pixels between contours, and from this one to the nearest.
          const float spacing = static_cast<float> (
            interval / std::max (rise_per_pixel, 1e-3f * u::m));
          const float level = static_cast<float> (above / interval);
          const float distance =
            std::abs (level - std::round (level)) * spacing;
          const blend_weight_t apart = smooth (2.0f, 5.0f, spacing);
          const blend_weight_t on_line =
            1.0f * blend_weight[one] - smooth (0.3f, 1.0f, distance);
          colour =
            mix_display (colour,
                         contour_ink,
                         0.5f * apart * on_line.numerical_value_in (one));
        }
        const std::size_t offset =
          (static_cast<std::size_t> (y) * size + x) * 4;
        rgba[offset + 0] = static_cast<std::uint8_t> (
          std::clamp (colour.red, 0.0f, 1.0f) * 255.0f + 0.5f);
        rgba[offset + 1] = static_cast<std::uint8_t> (
          std::clamp (colour.green, 0.0f, 1.0f) * 255.0f + 0.5f);
        rgba[offset + 2] = static_cast<std::uint8_t> (
          std::clamp (colour.blue, 0.0f, 1.0f) * 255.0f + 0.5f);
        rgba[offset + 3] = 255;
      }
  }

}

namespace moppe::site {
  void grow_land (int lattice, std::uint32_t seed, const GrowthStep& step) {
    using namespace moppe::terrain;
    // The game's default extent and datum (moppe/terrain/land_bake.cc).
    const WorldRecipe recipe = make_world_recipe (
      spatial_extent_in_metres (moppe::Vec3 (
        meters_value (land_period), 320.0f, meters_value (land_period))),
      lattice,
      Seed { seed },
      land_sea_level,
      TerrainGenerationProfile::Play);
    map::SurfaceGeometry surface (
      TerrainDomain (static_cast<std::size_t> (lattice),
                     static_cast<std::size_t> (lattice),
                     recipe.extent ()));
    const julian_years_f64_t step_years =
      julian_years_f64_t (recipe.evolution ().time_step);
    const auto as_floats = [] (std::span<const SurfaceElevation> heights) {
      static_assert (sizeof (SurfaceElevation) == sizeof (float));
      return std::span<const float> (
        reinterpret_cast<const float*> (heights.data ()), heights.size ());
    };

    const auto uplift =
      map::initialize_terrain (surface, recipe.seed (), recipe.water_datum ());
    const int steps = static_cast<int> (std::ceil (static_cast<double> (
      julian_years_f64_t (recipe.evolution ().duration) / step_years)));
    step (0,
          steps,
          julian_years_f64_t::zero (),
          as_floats (spatial::get<surface_elevation> (surface)));
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
              static_cast<double> (count) * step_years,
              as_floats (heights));
      });
  }

  void paint_relief (std::span<const float> heights,
                     int lattice,
                     int size,
                     std::vector<std::uint8_t>& rgba) {
    paint ({ heights, lattice },
           land_period / static_cast<float> (lattice),
           terrain::surface_elevation_point (land_sea_level),
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
    [&] (int step,
         int steps,
         moppe::julian_years_f64_t years,
         std::span<const float> heights) {
      if (size > 0)
        moppe::site::paint_relief (heights, lattice, size, rgba);
      post_step (step,
                 steps,
                 years.numerical_value_in (mp_units::astronomy::Julian_year),
                 heights.data (),
                 lattice,
                 size > 0 ? rgba.data () : nullptr,
                 size);
    });
}
#endif
