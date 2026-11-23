#include <moppe/map/surface.hh>

#include <moppe/terrain/benchmark_memory.hh>
#include <moppe/terrain/local_evolution.hh>
#include <moppe/terrain/world_recipe.hh>

#include <bit>
#include <chrono>
#include <cstdint>
#include <fstream>
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
    if (argc < 4 || argc > 7)
      throw std::invalid_argument ("usage: terrain-orogeny-benchmark SIZE SEED "
                                   "STEPS [REPEATS [BACKEND [HEIGHTS.f32]]]");
    const int resolution = parse_positive_int (argv[1], "size");
    const int seed = parse_non_negative_int (argv[2], "seed");
    const int steps = parse_positive_int (argv[3], "steps");
    const int repeats = argc >= 5 ? parse_positive_int (argv[4], "repeats") : 1;
    if (resolution < 3)
      throw std::invalid_argument ("size must be at least three");

    const Seed terrain_seed { static_cast<std::uint32_t> (seed) };
    const auto extent = spatial_extent_in_metres (Vec3 (10000, 320, 10000));
    auto evolution = make_world_recipe (extent,
                                        resolution,
                                        terrain_seed,
                                        50 * u::m,
                                        TerrainGenerationProfile::Play)
                       .evolution ();
    evolution.duration = static_cast<float> (steps) * evolution.time_step;
    const std::string backend = argc >= 6 ? argv[5] : "cpu";
    EvolutionBackend mode = EvolutionBackend::Cpu;
    if (backend == "metal")
      mode = EvolutionBackend::Metal;
    else if (backend == "metal-multigrid")
      mode = EvolutionBackend::MetalMultigrid;
    else if (backend != "cpu")
      throw std::invalid_argument ("unknown backend " + backend);

    std::cout
      << "resolution,cells,seed,steps,repeat,"
         "backend,elapsed_ms,peak_rss_mib,height_hash,uplift_m3,exported_m3,"
         "balance_error_m3,drainage_area_error_m2\n";
    for (int repeat = 0; repeat < repeats; ++repeat) {
      map::SurfaceGeometry surface (
        TerrainDomain (static_cast<std::size_t> (resolution),
                       static_cast<std::size_t> (resolution),
                       extent));
      const auto uplift =
        map::initialize_terrain (surface, terrain_seed, 50.0f * u::m);
      const auto start = std::chrono::steady_clock::now ();
      const auto report = evolve_local (surface, uplift, evolution, mode);
      const auto stop = std::chrono::steady_clock::now ();
      const quantity<si::milli<si::second>, double> elapsed =
        std::chrono::duration<double, std::milli> (stop - start).count () *
        si::milli<si::second>;

      if (argc == 7) {
        std::ofstream file (argv[6], std::ios::binary);
        file.exceptions (std::ios::badbit | std::ios::failbit);
        const auto& heights = spatial::get<surface_elevation> (surface);
        const auto bytes = std::as_bytes (std::span (heights));
        file.write (reinterpret_cast<const char*> (bytes.data ()),
                    bytes.size ());
      }
      std::cout
        << resolution << ','
        << static_cast<std::size_t> (resolution) *
             static_cast<std::size_t> (resolution)
        << ',' << seed << ',' << steps << ',' << repeat << ',' << backend << ','
        << std::fixed << std::setprecision (3)
        << elapsed.numerical_value_in (si::milli<si::second>) << ","
        << benchmark::peak_rss_mib () << ',' << std::hex
        << height_hash (surface) << std::dec << ','
        << report.tectonic_uplift.numerical_value_in (u::m * u::m * u::m) << ','
        << report.exported_sediment.numerical_value_in (u::m * u::m * u::m)
        << ',' << report.balance_error.numerical_value_in (u::m * u::m * u::m)
        << ',';
      if (report.drainage_area_error)
        std::cout << report.drainage_area_error->numerical_value_in (u::m *
                                                                     u::m);
      std::cout << '\n';
    }
  } catch (const std::exception& error) {
    std::cerr << "terrain orogeny benchmark: " << error.what () << '\n';
    return -1;
  }
}
