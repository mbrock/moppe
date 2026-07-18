#include <moppe/map/generate.hh>
#include <moppe/map/terrain_evaluator.hh>
#include <moppe/terrain/program.hh>

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

  moppe::terrain::StreamPowerRouting parse_routing (std::string_view text) {
    using moppe::terrain::StreamPowerRouting;
    if (text == "d8")
      return StreamPowerRouting::D8;
    if (text == "d-infinity" || text == "dinf")
      return StreamPowerRouting::DInfinity;
    throw std::invalid_argument ("routing must be d8 or d-infinity");
  }

  const char* routing_id (moppe::terrain::StreamPowerRouting routing) {
    using moppe::terrain::StreamPowerRouting;
    return routing == StreamPowerRouting::D8 ? "d8" : "d-infinity";
  }

  std::uint64_t height_hash (const moppe::map::RandomHeightMap& map) {
    // FNV-1a over the exact float representation makes benchmark output a
    // cheap numerical-regression ledger as well as a timing record.
    std::uint64_t hash = 14695981039346656037ull;
    const std::size_t count =
      static_cast<std::size_t> (map.width ()) * map.height ();
    for (std::size_t cell = 0; cell < count; ++cell) {
      std::uint32_t bits = std::bit_cast<std::uint32_t> (
        map.raw_heights ()[cell]);
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
    if (argc < 5 || argc > 6)
      throw std::invalid_argument (
        "usage: terrain-orogeny-benchmark SIZE SEED STEPS ROUTING [REPEATS]");
    const int resolution = parse_positive_int (argv[1], "size");
    const int seed = parse_non_negative_int (argv[2], "seed");
    const int steps = parse_positive_int (argv[3], "steps");
    const StreamPowerRouting routing = parse_routing (argv[4]);
    const int repeats =
      argc == 6 ? parse_positive_int (argv[5], "repeats") : 1;
    if (resolution < 3)
      throw std::invalid_argument ("size must be at least three");

    TerrainProgram program = make_orogeny_program (
      static_cast<std::uint32_t> (seed), TerrainGenerationProfile::Research);
    auto& orogeny = std::get<OrogenyEvolution> (program.transforms.front ());
    orogeny.evolution.duration =
      static_cast<float> (steps) * orogeny.evolution.time_step;
    orogeny.evolution.routing = routing;

    std::cout << "resolution,unique_cells,seed,routing,steps,repeat,elapsed_ms,"
                 "height_hash,final_mean_change_m,final_max_change_m\n";
    for (int repeat = 0; repeat < repeats; ++repeat) {
      map::RandomHeightMap map (resolution,
                                resolution,
                                Vec3 (11000.0f, 650.0f, 11000.0f),
                                seed,
                                Topology::Torus);
      map::TerrainEvaluator evaluator (map);
      evaluator.begin (program);
      const auto start = std::chrono::steady_clock::now ();
      const TerrainTransformReport transform_report =
        evaluator.apply (program.transforms.front ());
      const auto stop = std::chrono::steady_clock::now ();
      const double elapsed_ms =
        std::chrono::duration<double, std::milli> (stop - start).count ();
      const auto& report =
        std::get<StreamPowerEvolutionReport> (transform_report);

      std::cout << resolution << ','
                << static_cast<std::size_t> (resolution - 1) *
                     static_cast<std::size_t> (resolution - 1)
                << ',' << seed << ',' << routing_id (routing) << ',' << steps
                << ',' << repeat << ',' << std::fixed << std::setprecision (3)
                << elapsed_ms << "," << std::hex << height_hash (map)
                << std::dec << ','
                << meters_value (report.final_step_mean_change)
                << ',' << meters_value (report.final_step_maximum_change)
                << '\n';
    }
  } catch (const std::exception& error) {
    std::cerr << "terrain orogeny benchmark: " << error.what () << '\n';
    return -1;
  }
}
