#include <moppe/map/generate.hh>
#include <moppe/map/terrain_evaluator.hh>
#include <moppe/terrain/drainage.hh>
#include <moppe/terrain/flood.hh>
#include <moppe/terrain/image.hh>
#include <moppe/terrain/program.hh>

#include <algorithm>
#include <chrono>
#include <cstdint>
#include <fstream>
#include <iostream>
#include <stdexcept>
#include <string>
#include <vector>

namespace {
  using Clock = std::chrono::steady_clock;

  struct Mode {
    std::string name;
    std::vector<moppe::terrain::TerrainTransform> transforms;
  };

  double elapsed_ms (Clock::time_point start) {
    return std::chrono::duration<double, std::milli> (Clock::now () - start)
      .count ();
  }

  std::size_t longest_path (const moppe::terrain::DrainageGraph& graph) {
    std::vector<std::size_t> length (graph.receiver.size (), 0);
    std::vector<std::uint32_t> path;
    std::size_t maximum = 0;
    for (std::uint32_t origin = 0; origin < graph.receiver.size (); ++origin) {
      if (length[origin])
        continue;
      path.clear ();
      std::uint32_t cell = origin;
      while (!length[cell] && graph.receiver[cell] != cell) {
        path.push_back (cell);
        cell = graph.receiver[cell];
      }
      std::size_t value = length[cell] ? length[cell] : 1;
      length[cell] = value;
      for (auto i = path.rbegin (); i != path.rend (); ++i) {
        length[*i] = ++value;
        maximum = std::max (maximum, value);
      }
    }
    return maximum;
  }

  void write_map (const std::string& path,
                  const moppe::map::RandomHeightMap& map) {
    const std::size_t count =
      static_cast<std::size_t> (map.width ()) * map.height ();
    const moppe::terrain::ScalarRaster raster (
      { .width = static_cast<std::size_t> (map.width ()),
        .height = static_cast<std::size_t> (map.height ()) },
      std::vector<float> (map.raw_heights (), map.raw_heights () + count));
    std::ofstream output (path, std::ios::binary);
    if (!output)
      throw std::runtime_error ("cannot open experiment image: " + path);
    moppe::terrain::write_grayscale_png (output, raster);
  }
}

int main (int argc, char** argv) {
  using namespace moppe;
  using namespace moppe::terrain;

  try {
    const int resolution = argc > 1 ? std::stoi (argv[1]) : 257;
    const auto seed =
      static_cast<std::uint32_t> (argc > 2 ? std::stoul (argv[2]) : 123);
    const float age = argc > 3 ? std::stof (argv[3]) : 200000.0f;
    const int routing_passes = argc > 4 ? std::stoi (argv[4]) : 1;
    const int droplets = argc > 5 ? std::stoi (argv[5]) : 30000;
    const std::string image_prefix = argc > 6 ? argv[6] : "";
    const float uplift = argc > 7 ? std::stof (argv[7]) : 0.0f;

    const AnalyticalErosion analytical {
      .duration = age * mp_units::astronomy::Julian_year,
      .uplift_rate =
        uplift * mp_units::si::metre / mp_units::astronomy::Julian_year,
      .erodibility = 2e-5f,
      .area_exponent = 0.4f,
      .sea_level = 50.0f / 650.0f,
      .fixed_point_iterations = routing_passes,
      .relaxation = routing_passes > 1 ? 0.5f : 1.0f
    };
    const HydraulicErosion hydraulic { .droplets = droplets,
                                       .batch_size = 256,
                                       .max_steps = 512,
                                       .minimum_water = 0.01f,
                                       .sediment_at_termination =
                                         SedimentDisposition::Deposit };
    const ThermalErosion thermal { 2, 0.003f };
    const std::vector<Mode> modes {
      { "source", {} },
      { "analytical", { analytical } },
      { "analytical_talus", { analytical, thermal } },
      { "droplets", { hydraulic } },
      { "analytical_droplets", { analytical, hydraulic } },
      { "analytical_droplets_talus", { analytical, hydraulic, thermal } }
    };

    map::RandomHeightMap map (
      resolution, resolution, Vec3 (5000, 650, 5000), seed, Topology::Torus);
    map::TerrainEvaluator evaluator (map);
    const TerrainProgram source = make_geological_program (seed);
    evaluator.begin (source);
    evaluator.apply (NormalizeHeights {});
    evaluator.apply (PowerHeights { 1.15f });
    const map::TerrainCheckpoint base = evaluator.checkpoint ();

    std::cout
      << "mode,total_ms,sinks,stream_cells,max_area_cells,longest_path,"
      << "puddles,ponds,lakes,waterfalls,water_m3,lowered_m3,raised_m3,"
      << "mean_change_m,max_change_m,droplet_eroded,droplet_deposited\n";
    for (const Mode& mode : modes) {
      evaluator.restore (base);
      AnalyticalErosionReport analytical_report;
      HydraulicErosionReport hydraulic_report;
      const Clock::time_point start = Clock::now ();
      for (const TerrainTransform& transform : mode.transforms) {
        const TerrainTransformReport result = evaluator.apply (transform);
        if (const auto* report = std::get_if<AnalyticalErosionReport> (&result))
          analytical_report = *report;
        if (const auto* report = std::get_if<HydraulicErosionReport> (&result))
          hydraulic_report = *report;
      }
      const double total_ms = elapsed_ms (start);

      const DrainageGraph drainage = analyze_drainage (map.terrain_view ());
      const float cell_area =
        square_meters_value (drainage.source_grid.cell_area ());
      float maximum_area = 0.0f;
      std::size_t stream_cells = 0;
      for (const float area : drainage.contributing_area.values ()) {
        maximum_area = std::max (maximum_area, area);
        if (area >= 1024.0f * cell_area)
          ++stream_cells;
      }
      const FloodField flood =
        analyze_standing_water (map.terrain_view (), analytical.sea_level);
      const LakeCensus census = census_lakes (flood);
      const DrainageGraph wet =
        analyze_wet_drainage (map.terrain_view (), flood, census);
      const RiverNetwork rivers =
        extract_river_network (flood, census, wet, 1024.0f * cell_area);
      int puddles = 0, ponds = 0, lakes = 0;
      double water = 0.0;
      for (const WaterBody& body : census.bodies) {
        if (body.classification == WaterBodyClass::Puddle)
          ++puddles;
        if (body.classification == WaterBodyClass::Pond)
          ++ponds;
        if (body.classification == WaterBodyClass::Lake)
          ++lakes;
        if (body.classification != WaterBodyClass::Sea)
          water += cubic_meters_value (body.volume);
      }

      std::cout << mode.name << ',' << total_ms << ',' << drainage.sinks.size ()
                << ',' << stream_cells << ',' << maximum_area / cell_area << ','
                << longest_path (drainage) << ',' << puddles << ',' << ponds
                << ',' << lakes << ',' << rivers.waterfalls.size () << ','
                << water << ',' << analytical_report.lowered_volume_m3 << ','
                << analytical_report.raised_volume_m3 << ','
                << analytical_report.mean_absolute_change_m << ','
                << analytical_report.maximum_absolute_change_m << ','
                << hydraulic_report.eroded << ',' << hydraulic_report.deposited
                << '\n';
      if (!image_prefix.empty ())
        write_map (image_prefix + "-" + mode.name + ".png", map);
    }
  } catch (const std::exception& error) {
    std::cerr << "stream-power experiment: " << error.what () << '\n';
    return -1;
  }
  return 0;
}
