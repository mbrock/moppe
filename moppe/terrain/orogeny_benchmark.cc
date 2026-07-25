#include <moppe/map/surface.hh>
#include <moppe/map/terrain_evaluator.hh>
#include <moppe/terrain/program.hh>
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

  std::uint64_t height_hash (const moppe::map::Surface& map) {
    // FNV-1a over the exact float representation makes benchmark output a
    // cheap numerical-regression ledger as well as a timing record.
    std::uint64_t hash = 14695981039346656037ull;
    const std::size_t count =
      static_cast<std::size_t> (map.width ()) * map.height ();
    for (std::size_t cell = 0; cell < count; ++cell) {
      std::uint32_t bits = std::bit_cast<std::uint32_t> (
        moppe::terrain::surface_elevation_value (map.elevations ()[cell]));
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
  height_difference (const moppe::map::Surface& map,
                     std::span<const moppe::map::SurfaceElevation> reference) {
    const std::size_t count =
      static_cast<std::size_t> (map.width ()) * map.height ();
    if (reference.size () != count)
      throw std::logic_error ("reference surface size changed");
    double total = 0.0;
    double maximum = 0.0;
    std::size_t cells_over_one_meter = 0;
    std::vector<double> differences;
    differences.reserve (count);
    for (std::size_t cell = 0; cell < count; ++cell) {
      const double difference =
        std::fabs ((map.elevations ()[cell] - reference[cell])
                     .numerical_value_in (moppe::u::m));
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

    TerrainProgram program = make_orogeny_program (
      static_cast<std::uint32_t> (seed), TerrainGenerationProfile::Research);
    auto& orogeny = std::get<OrogenyEvolution> (program.transforms.front ());
    orogeny.evolution.duration =
      static_cast<float> (steps) * orogeny.evolution.time_step;

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
      map::Surface reference_map (
        resolution, resolution, Vec3 (11000.0f, 650.0f, 11000.0f));
      map::TerrainEvaluator reference_evaluator (reference_map);
      reference_evaluator.begin (program);
      reference_evaluator.apply (program.transforms.front ());
      reference_heights = reference_map.elevations ();
    }

    std::cout << "resolution,cells,seed,backend,steps,repeat,"
                 "elapsed_ms,height_hash,final_mean_change_m,"
                 "final_max_change_m,reference_mean_difference_m,"
                 "reference_p99_difference_m,"
                 "reference_p999_difference_m,"
                 "reference_max_difference_m,"
                 "reference_cells_over_1m\n";
    for (int repeat = 0; repeat < repeats; ++repeat) {
      map::Surface map (
        resolution, resolution, Vec3 (11000.0f, 650.0f, 11000.0f));
      map::TerrainEvaluator evaluator (map, nullptr, backend.get ());
      evaluator.begin (program);
      const auto start = std::chrono::steady_clock::now ();
      const TerrainTransformReport transform_report =
        evaluator.apply (program.transforms.front ());
      const auto stop = std::chrono::steady_clock::now ();
      const double elapsed_ms =
        std::chrono::duration<double, std::milli> (stop - start).count ();
      const auto& report =
        std::get<StreamPowerEvolutionReport> (transform_report);
      const HeightDifference difference =
        reference_heights.empty () ? HeightDifference {}
                                   : height_difference (map, reference_heights);

      std::cout << resolution << ','
                << static_cast<std::size_t> (resolution - 1) *
                     static_cast<std::size_t> (resolution - 1)
                << ',' << seed << ',' << backend_id << ',' << steps << ','
                << repeat << ',' << std::fixed << std::setprecision (3)
                << elapsed_ms << "," << std::hex << height_hash (map)
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
