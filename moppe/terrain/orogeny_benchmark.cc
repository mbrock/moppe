#include <moppe/map/surface.hh>
#ifdef MOPPE_OROGENY_METAL_BACKEND
#include <moppe/terrain/metal/metal_stream_power_evolution.hh>
#endif

#include <algorithm>
#include <bit>
#include <chrono>
#include <cmath>
#include <cstdint>
#include <iomanip>
#include <iostream>
#include <memory>
#include <span>
#include <stdexcept>
#include <string>
#include <string_view>
#include <vector>

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
    const std::size_t count =
      static_cast<std::size_t> (surface.width ()) * surface.height ();
    const auto& elevations =
      moppe::spatial::get<moppe::terrain::surface_elevation> (
        surface.geometry ());
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

  struct HeightDifference {
    double mean_m;
    double p99_m;
    double p999_m;
    double maximum_m;
    std::size_t cells_over_one_meter;
  };

  HeightDifference
  height_difference (const moppe::map::SurfaceGeometry& surface,
                     std::span<const moppe::map::SurfaceElevation> reference) {
    const std::size_t count =
      static_cast<std::size_t> (surface.width ()) * surface.height ();
    if (reference.size () != count)
      throw std::logic_error ("reference surface size changed");
    double total = 0.0;
    double maximum = 0.0;
    std::size_t cells_over_one_meter = 0;
    std::vector<double> differences;
    differences.reserve (count);
    const auto& elevations =
      moppe::spatial::get<moppe::terrain::surface_elevation> (
        surface.geometry ());
    for (std::size_t cell = 0; cell < count; ++cell) {
      const double difference = std::fabs (
        (elevations[cell] - reference[cell]).numerical_value_in (moppe::u::m));
      total += difference;
      maximum = std::max (maximum, difference);
      cells_over_one_meter += difference > 1.0;
      differences.push_back (difference);
    }
    std::ranges::sort (differences);
    const auto percentile = [&] (double fraction) {
      const std::size_t index =
        static_cast<std::size_t> (fraction * static_cast<double> (count - 1));
      return differences[index];
    };
    return { .mean_m = total / count,
             .p99_m = percentile (0.99),
             .p999_m = percentile (0.999),
             .maximum_m = maximum,
             .cells_over_one_meter = cells_over_one_meter };
  }
}

int main (int argc, char** argv) {
  using namespace moppe;
  using namespace moppe::terrain;

  try {
    if (argc < 4 || argc > 6)
      throw std::invalid_argument (
        "usage: terrain-orogeny-benchmark SIZE SEED STEPS "
        "[REPEATS] [cpu|metal]");
    const int resolution = parse_positive_int (argv[1], "size");
    const int seed = parse_non_negative_int (argv[2], "seed");
    const int steps = parse_positive_int (argv[3], "steps");
    const int repeats = argc >= 5 ? parse_positive_int (argv[4], "repeats") : 1;
    const std::string_view backend_id = argc == 6 ? argv[5] : "cpu";
    if (resolution < 3)
      throw std::invalid_argument ("size must be at least three");

    StreamPowerEvolution evolution;
    evolution.diffusivity = 0.0001f * mp_units::si::metre *
                            mp_units::si::metre /
                            mp_units::astronomy::Julian_year;
    evolution.duration = static_cast<float> (steps) * evolution.time_step;
    const Seed terrain_seed { static_cast<std::uint32_t> (seed) };

    std::unique_ptr<StreamPowerEvolutionBackend> backend;
    if (backend_id == "metal") {
#ifdef MOPPE_OROGENY_METAL_BACKEND
      backend = std::make_unique<metal::MetalStreamPowerEvolutionBackend> (
        MOPPE_SHADER_ASSET_PATH);
#else
      throw std::invalid_argument ("Metal orogeny backend is unavailable");
#endif
    } else if (backend_id != "cpu") {
      throw std::invalid_argument ("backend must be cpu or metal");
    }

    std::vector<map::SurfaceElevation> reference_heights;
    if (backend) {
      map::SurfaceGeometry reference_map (
        resolution, resolution, Vec3 (11000.0f, 650.0f, 11000.0f));
      const auto uplift =
        map::initialize_terrain (reference_map, terrain_seed, 50.0f * u::m);
      map::evolve_terrain (reference_map, uplift, evolution);
      reference_heights =
        moppe::spatial::get<moppe::terrain::surface_elevation> (
          reference_map.geometry ());
    }

    std::cout << "resolution,cells,seed,backend,steps,repeat,"
                 "elapsed_ms,height_hash,final_mean_change_m,"
                 "final_max_change_m,reference_mean_difference_m,"
                 "reference_p99_difference_m,"
                 "reference_p999_difference_m,"
                 "reference_max_difference_m,"
                 "reference_cells_over_1m\n";
    for (int repeat = 0; repeat < repeats; ++repeat) {
      map::SurfaceGeometry surface (
        resolution, resolution, Vec3 (11000.0f, 650.0f, 11000.0f));
      const auto uplift =
        map::initialize_terrain (surface, terrain_seed, 50.0f * u::m);
      const auto start = std::chrono::steady_clock::now ();
      const StreamPowerEvolutionReport report =
        map::evolve_terrain (surface, uplift, evolution, backend.get ());
      const auto stop = std::chrono::steady_clock::now ();
      const double elapsed_ms =
        std::chrono::duration<double, std::milli> (stop - start).count ();
      const HeightDifference difference =
        reference_heights.empty ()
          ? HeightDifference {}
          : height_difference (surface, reference_heights);

      std::cout << resolution << ','
                << static_cast<std::size_t> (resolution) *
                     static_cast<std::size_t> (resolution)
                << ',' << seed << ',' << backend_id << ',' << steps << ','
                << repeat << ',' << std::fixed << std::setprecision (3)
                << elapsed_ms << "," << std::hex << height_hash (surface)
                << std::dec << ','
                << meters_value (report.final_step_mean_change) << ','
                << meters_value (report.final_step_maximum_change) << ','
                << difference.mean_m << ',' << difference.p99_m << ','
                << difference.p999_m << ',' << difference.maximum_m << ','
                << difference.cells_over_one_meter << '\n';
    }
  } catch (const std::exception& error) {
    std::cerr << "terrain orogeny benchmark: " << error.what () << '\n';
    return -1;
  }
}
