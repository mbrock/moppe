// The website's geology (docs/site.md): the default world's land grown on a
// small lattice, a shaded picture of it painted after every geological
// step. In the browser it runs in a Web Worker and hands each picture to
// the page (moppe/site/geology-worker.js); natively it times the steps and
// writes the pictures as OUTPUT-STEP.ppm, to judge a lattice without a
// browser:
//
//   moppe-site-geology [RESOLUTION [SEED [OUTPUT]]]

#include <moppe/map/surface.hh>
#include <moppe/terrain/world_recipe.hh>

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdint>
#include <span>
#include <vector>

#ifdef __EMSCRIPTEN__
#include <emscripten/emscripten.h>
#else
#include <fstream>
#include <iostream>
#include <string>
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

#ifdef __EMSCRIPTEN__
  // Hands a picture to the page; the worker script (geology-worker.js)
  // defines Module.frame.
  EM_JS (
    void,
    post_frame,
    (int step, int steps, double years, const std::uint8_t* rgba, int size),
    {
      Module.frame (
        step, steps, years, HEAPU8.slice (rgba, rgba + size * size * 4), size);
    });
#endif

  // Grows the world and calls `frame (step, steps, years, rgba, size)`
  // with its first picture and after every geological step.
  template <typename Frame>
  void grow (int resolution, std::uint32_t seed, int size, Frame frame) {
    using namespace moppe::terrain;
    // The game's default extent and datum (moppe/terrain/land_bake.cc).
    const WorldRecipe recipe = make_world_recipe (
      spatial_extent_in_metres (Vec3 (5000.0f, 320.0f, 5000.0f)),
      resolution,
      Seed { seed },
      50.0f * u::m,
      TerrainGenerationProfile::Play);
    map::SurfaceGeometry surface (
      TerrainDomain (static_cast<std::size_t> (resolution),
                     static_cast<std::size_t> (resolution),
                     recipe.extent ()));
    const float cell_m = 5000.0f / static_cast<float> (resolution);
    const float sea_level_m = recipe.water_datum ().numerical_value_in (u::m);
    const double step_years = recipe.evolution ().time_step.numerical_value_in (
      mp_units::astronomy::Julian_year);
    std::vector<std::uint8_t> rgba;

    const auto as_floats = [] (std::span<const SurfaceElevation> heights) {
      static_assert (sizeof (SurfaceElevation) == sizeof (float));
      return std::span<const float> (
        reinterpret_cast<const float*> (heights.data ()), heights.size ());
    };

    const auto uplift =
      map::initialize_terrain (surface, recipe.seed (), recipe.water_datum ());
    const auto& start = spatial::get<surface_elevation> (surface);
    const int steps = static_cast<int> (
      std::ceil (recipe.evolution ().duration.numerical_value_in (
                   mp_units::astronomy::Julian_year) /
                 step_years));
    paint ({ as_floats (start), resolution }, cell_m, sea_level_m, size, rgba);
    frame (0, steps, 0.0, rgba, size);

    map::evolve_terrain (
      surface,
      uplift,
      recipe.evolution (),
      [&] (IterationCount step,
           IterationCount total,
           std::span<const SurfaceElevation> heights) {
        const int done =
          static_cast<int> (step.numerical_value_in (mp_units::one));
        paint (
          { as_floats (heights), resolution }, cell_m, sea_level_m, size, rgba);
        frame (done,
               static_cast<int> (total.numerical_value_in (mp_units::one)),
               done * step_years,
               rgba,
               size);
      });
  }
}

#ifdef __EMSCRIPTEN__
extern "C" EMSCRIPTEN_KEEPALIVE void
moppe_geology_grow (int resolution, unsigned seed, int size) {
  grow (resolution,
        seed,
        size,
        [] (int step,
            int steps,
            double years,
            const std::vector<std::uint8_t>& rgba,
            int size) { post_frame (step, steps, years, rgba.data (), size); });
}
#else
int main (int argc, char** argv) {
  const int resolution = argc > 1 ? std::stoi (argv[1]) : 256;
  const auto seed =
    static_cast<std::uint32_t> (argc > 2 ? std::stoul (argv[2]) : 123);
  const std::string output = argc > 3 ? argv[3] : "";
  auto last = std::chrono::steady_clock::now ();
  grow (resolution,
        seed,
        512,
        [&] (int step,
             int steps,
             double years,
             const std::vector<std::uint8_t>& rgba,
             int size) {
          const auto now = std::chrono::steady_clock::now ();
          std::cout
            << "step " << step << "/" << steps << " at " << years << " years: "
            << std::chrono::duration<double, std::milli> (now - last).count ()
            << " ms\n";
          last = now;
          if (output.empty ())
            return;
          std::ofstream file (output + "-" + std::to_string (step) + ".ppm",
                              std::ios::binary);
          file << "P6\n" << size << ' ' << size << "\n255\n";
          for (std::size_t i = 0; i < rgba.size (); i += 4)
            file.write (reinterpret_cast<const char*> (&rgba[i]), 3);
        });
  return 0;
}
#endif
