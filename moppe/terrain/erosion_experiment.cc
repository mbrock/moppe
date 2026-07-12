#include <moppe/map/generate.hh>
#include <moppe/map/terrain_evaluator.hh>
#include <moppe/terrain/drainage.hh>
#include <moppe/terrain/flood.hh>
#include <moppe/terrain/program.hh>

#include <algorithm>
#include <chrono>
#include <cstdint>
#include <iostream>
#include <sstream>
#include <string>
#include <vector>

namespace {
  using Clock = std::chrono::steady_clock;

  double milliseconds_since (Clock::time_point start) {
    return std::chrono::duration<double, std::milli> (Clock::now () - start)
      .count ();
  }

  std::vector<int> parse_steps (const std::string& text) {
    std::vector<int> result;
    std::istringstream input (text);
    std::string part;
    while (std::getline (input, part, ','))
      result.push_back (std::stoi (part));
    return result;
  }

  std::size_t longest_path (const moppe::terrain::DrainageGraph& graph) {
    std::vector<std::size_t> lengths (graph.receiver.size (), 0);
    std::vector<std::uint32_t> path;
    std::size_t maximum = 0;
    for (std::uint32_t start = 0; start < graph.receiver.size (); ++start) {
      if (lengths[start])
        continue;
      path.clear ();
      std::uint32_t cell = start;
      while (!lengths[cell] && graph.receiver[cell] != cell) {
        path.push_back (cell);
        cell = graph.receiver[cell];
      }
      std::size_t length = lengths[cell] ? lengths[cell] : 1;
      lengths[cell] = length;
      for (auto i = path.rbegin (); i != path.rend (); ++i) {
        lengths[*i] = ++length;
        maximum = std::max (maximum, length);
      }
      maximum = std::max (maximum, lengths[start]);
    }
    return maximum;
  }
}

int main (int argc, char** argv) {
  using namespace moppe;
  using namespace moppe::terrain;

  try {
    const int resolution = argc > 1 ? std::stoi (argv[1]) : 257;
    const std::uint32_t seed =
      argc > 2 ? static_cast<std::uint32_t> (std::stoul (argv[2])) : 123;
    const int droplets = argc > 3 ? std::stoi (argv[3]) : 100000;
    const int batch_size = argc > 4 ? std::stoi (argv[4]) : 1;
    const std::vector<int> lifetimes =
      parse_steps (argc > 5 ? argv[5] : "64,128,256,512");
    const float minimum_water = argc > 6 ? std::stof (argv[6]) : 0.0f;
    const bool settle = argc > 7 && std::string (argv[7]) == "settle";
    const CarvingRule carving_rule =
      argc > 8 && std::string (argv[8]) == "unconstrained"
        ? CarvingRule::Unconstrained
        : CarvingRule::PathMonotone;

    map::RandomHeightMap map (
      resolution, resolution, Vec3 (5000, 650, 5000), seed, Topology::Torus);
    map::TerrainEvaluator evaluator (map);
    const TerrainProgram source = make_geological_program (seed);
    evaluator.begin (source);
    evaluator.apply (NormalizeHeights {});
    evaluator.apply (PowerHeights { 1.15f });
    const map::TerrainCheckpoint base = evaluator.checkpoint ();

    std::cout
      << "steps,erosion_ms,drainage_ms,sinks,stream_cells,max_area_cells,"
      << "longest_path,eroded,deposited,discarded,lost_fraction,mean_steps,"
      << "mean_final_water,cap,water,flat,puddles,ponds,lakes,water_m3\n";
    for (const int max_steps : lifetimes) {
      evaluator.restore (base);
      const Clock::time_point erosion_start = Clock::now ();
      const TerrainTransformReport result = evaluator.apply (HydraulicErosion {
        .droplets = droplet_count (droplets),
        .batch_size = terrain::batch_size (batch_size),
        .max_steps = step_count (max_steps),
        .minimum_water = minimum_water,
        .sediment_at_termination =
          settle ? SedimentDisposition::Deposit : SedimentDisposition::Discard,
        .carving_rule = carving_rule });
      const double erosion_ms = milliseconds_since (erosion_start);
      const auto& report = std::get<HydraulicErosionReport> (result);

      const Clock::time_point drainage_start = Clock::now ();
      const DrainageGraph graph = analyze_drainage (map.terrain_view ());
      const double drainage_ms = milliseconds_since (drainage_start);
      const float cell_area =
        square_meters_value (graph.source_grid.cell_area ());
      float maximum_area = 0.0f;
      std::size_t stream_cells = 0;
      for (const float area : graph.contributing_area.values ()) {
        maximum_area = std::max (maximum_area, area);
        if (area >= 64.0f * cell_area)
          ++stream_cells;
      }
      const FloodField flood =
        analyze_standing_water (map.terrain_view (), 50.0f / 650.0f);
      const LakeCensus census = census_lakes (flood);
      int puddles = 0, ponds = 0, lakes = 0;
      double water_volume = 0.0;
      for (const WaterBody& body : census.bodies) {
        switch (body.classification) {
        case WaterBodyClass::Puddle:
          ++puddles;
          water_volume += cubic_meters_value (body.volume);
          break;
        case WaterBodyClass::Pond:
          ++ponds;
          water_volume += cubic_meters_value (body.volume);
          break;
        case WaterBodyClass::Lake:
          ++lakes;
          water_volume += cubic_meters_value (body.volume);
          break;
        case WaterBodyClass::Sea:
          break;
        }
      }

      std::cout << max_steps << ',' << erosion_ms << ',' << drainage_ms << ','
                << graph.sinks.size () << ',' << stream_cells << ','
                << maximum_area / cell_area << ',' << longest_path (graph)
                << ',' << report.eroded << ',' << report.deposited << ','
                << report.discarded_sediment << ','
                << report.discarded_fraction () << ',' << report.mean_steps ()
                << ',' << report.mean_final_water () << ','
                << report.stopped_at_step_limit << ','
                << report.stopped_at_water_cutoff << ',' << report.stopped_flat
                << ',' << puddles << ',' << ponds << ',' << lakes << ','
                << water_volume << '\n';
    }
  } catch (const std::exception& error) {
    std::cerr << "terrain erosion experiment: " << error.what () << '\n';
    return -1;
  }
  return 0;
}
