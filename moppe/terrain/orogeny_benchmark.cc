#include <moppe/map/surface.hh>

#include <bit>
#include <chrono>
#include <cstdint>
#include <iomanip>
#include <iostream>
#include <stdexcept>
#include <string>
#include <string_view>

namespace {
  int parse_positive_int (std::string_view text, const char* name) {
    std::size_t consumed = 0;
    const int value = std::stoi (std::string (text), &consumed);
    if (consumed != text.size () || value <= 0)
      throw std::invalid_argument (std::string (name) +
                                   " must be a positive integer");
    return value;
  }

  int parse_non_negative_int (std::string_view text, const char* name) {
    std::size_t consumed = 0;
    const int value = std::stoi (std::string (text), &consumed);
    if (consumed != text.size () || value < 0)
      throw std::invalid_argument (std::string (name) +
                                   " must be a non-negative integer");
    return value;
  }

  std::uint64_t height_hash (const moppe::map::SurfaceGeometry& surface) {
    // FNV-1a over the exact float representation makes benchmark output a
    // cheap numerical-regression ledger as well as a timing record.
    std::uint64_t hash = 14695981039346656037ull;
    const std::size_t count = surface.domain ().size ();
    const auto& elevations =
      moppe::spatial::get<moppe::terrain::surface_elevation> (surface);
    for (std::size_t cell = 0; cell < count; ++cell) {
      std::uint32_t bits = std::bit_cast<std::uint32_t> (
        moppe::terrain::surface_elevation_value (elevations[cell]));
      for (int byte = 0; byte < 4; ++byte) {
        hash ^= bits & 0xffu;
        hash *= 1099511628211ull;
        bits >>= 8;
      }
    }
    return hash;
  }
}

int main (int argc, char** argv) {
  using namespace moppe;
  using namespace moppe::terrain;

  try {
    if (argc < 4 || argc > 5)
      throw std::invalid_argument (
        "usage: terrain-orogeny-benchmark SIZE SEED STEPS [REPEATS]");
    const int resolution = parse_positive_int (argv[1], "size");
    const int seed = parse_non_negative_int (argv[2], "seed");
    const int steps = parse_positive_int (argv[3], "steps");
    const int repeats = argc >= 5 ? parse_positive_int (argv[4], "repeats") : 1;
    if (resolution < 3)
      throw std::invalid_argument ("size must be at least three");

    StreamPowerEvolution evolution;
    evolution.diffusivity = 0.0001f * mp_units::si::metre *
                            mp_units::si::metre /
                            mp_units::astronomy::Julian_year;
    evolution.duration = static_cast<float> (steps) * evolution.time_step;
    const Seed terrain_seed { static_cast<std::uint32_t> (seed) };

    std::cout << "resolution,cells,seed,steps,repeat,"
                 "elapsed_ms,height_hash,final_mean_change_m,"
                 "final_max_change_m\n";
    for (int repeat = 0; repeat < repeats; ++repeat) {
      map::SurfaceGeometry surface (TerrainDomain (
        static_cast<std::size_t> (resolution),
        static_cast<std::size_t> (resolution),
        spatial_extent_in_metres (Vec3 (11000.0f, 650.0f, 11000.0f))));
      const auto uplift =
        map::initialize_terrain (surface, terrain_seed, 50.0f * u::m);
      const auto start = std::chrono::steady_clock::now ();
      const StreamPowerEvolutionReport report =
        map::evolve_terrain (surface, uplift, evolution);
      const auto stop = std::chrono::steady_clock::now ();
      const double elapsed_ms =
        std::chrono::duration<double, std::milli> (stop - start).count ();

      std::cout << resolution << ','
                << static_cast<std::size_t> (resolution) *
                     static_cast<std::size_t> (resolution)
                << ',' << seed << ',' << steps << ',' << repeat << ','
                << std::fixed << std::setprecision (3) << elapsed_ms << ","
                << std::hex << height_hash (surface) << std::dec << ','
                << report.final_step_mean_change.numerical_value_in (u::m)
                << ','
                << report.final_step_maximum_change.numerical_value_in (u::m)
                << '\n';
    }
  } catch (const std::exception& error) {
    std::cerr << "terrain orogeny benchmark: " << error.what () << '\n';
    return -1;
  }
}
