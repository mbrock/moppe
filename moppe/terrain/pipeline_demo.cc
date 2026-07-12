#include <moppe/map/generate.hh>
#include <moppe/map/terrain_evaluator.hh>
#include <moppe/terrain/image.hh>
#include <moppe/terrain/program.hh>

#include <cstdint>
#include <fstream>
#include <iostream>
#include <stdexcept>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace {
  float parse_float (std::string_view text) {
    std::size_t consumed = 0;
    const float value = std::stof (std::string (text), &consumed);
    if (consumed != text.size ())
      throw std::invalid_argument ("invalid number: " + std::string (text));
    return value;
  }

  int parse_int (std::string_view text) {
    std::size_t consumed = 0;
    const int value = std::stoi (std::string (text), &consumed);
    if (consumed != text.size ())
      throw std::invalid_argument ("invalid integer: " + std::string (text));
    return value;
  }

  void apply_option (moppe::terrain::TerrainProgram& program,
                     std::string_view option) {
    using namespace moppe::terrain;
    if (option == "world") {
      const GeologicalLayer layer = program.source.layer;
      program = make_default_world_program (program.randomness.seed);
      program.source.layer = layer;
      return;
    }
    if (option == "raw") {
      program.transforms.clear ();
      return;
    }
    if (option == "normalize") {
      program.transforms.emplace_back (NormalizeHeights {});
      return;
    }
    if (option == "carve") {
      program.transforms.emplace_back (ChannelCarving {});
      return;
    }

    const std::size_t equals = option.find ('=');
    if (equals == std::string_view::npos)
      throw std::invalid_argument ("unknown pipeline option: " +
                                   std::string (option));
    const std::string_view name = option.substr (0, equals);
    const std::string_view value = option.substr (equals + 1);

    if (name == "power")
      program.transforms.emplace_back (PowerHeights { parse_float (value) });
    else if (name == "analytical") {
      std::vector<std::string_view> parts;
      std::size_t start = 0;
      while (start <= value.size ()) {
        const std::size_t comma = value.find (',', start);
        parts.push_back (value.substr (start,
                                       comma == std::string_view::npos
                                         ? value.size () - start
                                         : comma - start));
        if (comma == std::string_view::npos)
          break;
        start = comma + 1;
      }
      if (parts.empty () || parts.size () > 7)
        throw std::invalid_argument (
          "analytical expects age[,uplift,k,m,sea,iterations,relaxation]");
      AnalyticalErosion erosion;
      erosion.time_years = parse_float (parts[0]);
      if (parts.size () > 1)
        erosion.uplift_m_per_year = parse_float (parts[1]);
      if (parts.size () > 2)
        erosion.erodibility = parse_float (parts[2]);
      if (parts.size () > 3)
        erosion.area_exponent = parse_float (parts[3]);
      if (parts.size () > 4)
        erosion.sea_level = parse_float (parts[4]);
      if (parts.size () > 5)
        erosion.fixed_point_iterations = parse_int (parts[5]);
      if (parts.size () > 6)
        erosion.relaxation = parse_float (parts[6]);
      program.transforms.emplace_back (erosion);
    } else if (name == "hydraulic") {
      std::vector<std::string_view> parts;
      std::size_t start = 0;
      while (start <= value.size ()) {
        const std::size_t comma = value.find (',', start);
        parts.push_back (value.substr (start,
                                       comma == std::string_view::npos
                                         ? value.size () - start
                                         : comma - start));
        if (comma == std::string_view::npos)
          break;
        start = comma + 1;
      }
      if (parts.empty () || parts.size () > 5)
        throw std::invalid_argument (
          "hydraulic expects drops[,batch,steps,water,discard|deposit]");
      const int droplets = parse_int (parts[0]);
      const int batch_size = parts.size () > 1 ? parse_int (parts[1]) : 256;
      const int max_steps = parts.size () > 2 ? parse_int (parts[2]) : 64;
      const float minimum_water =
        parts.size () > 3 ? parse_float (parts[3]) : 0.0f;
      SedimentDisposition disposition = SedimentDisposition::Discard;
      if (parts.size () > 4) {
        if (parts[4] == "deposit")
          disposition = SedimentDisposition::Deposit;
        else if (parts[4] != "discard")
          throw std::invalid_argument (
            "hydraulic sediment policy must be discard or deposit");
      }
      program.transforms.emplace_back (
        HydraulicErosion { .droplets = droplets,
                           .batch_size = batch_size,
                           .max_steps = max_steps,
                           .minimum_water = minimum_water,
                           .sediment_at_termination = disposition });
    } else if (name == "carve") {
      std::vector<std::string_view> parts;
      std::size_t start = 0;
      while (start <= value.size ()) {
        const std::size_t comma = value.find (',', start);
        parts.push_back (value.substr (start,
                                       comma == std::string_view::npos
                                         ? value.size () - start
                                         : comma - start));
        if (comma == std::string_view::npos)
          break;
        start = comma + 1;
      }
      if (parts.empty () || parts.size () > 6)
        throw std::invalid_argument (
          "carve expects area_cells[,depth_scale,min_depth,max_depth,"
          "sea,blend]");
      ChannelCarving carving;
      carving.minimum_area_cells = parse_float (parts[0]);
      if (parts.size () > 1)
        carving.depth_per_sqrt_m2 = parse_float (parts[1]);
      if (parts.size () > 2)
        carving.minimum_depth_m = parse_float (parts[2]);
      if (parts.size () > 3)
        carving.maximum_depth_m = parse_float (parts[3]);
      if (parts.size () > 4)
        carving.sea_level = parse_float (parts[4]);
      if (parts.size () > 5)
        carving.bank_blend_m = parse_float (parts[5]);
      program.transforms.emplace_back (carving);
    } else if (name == "thermal") {
      const std::size_t comma = value.find (',');
      if (comma == std::string_view::npos)
        throw std::invalid_argument ("thermal expects iterations,talus");
      program.transforms.emplace_back (
        ThermalErosion { parse_int (value.substr (0, comma)),
                         parse_float (value.substr (comma + 1)) });
    } else if (name == "warp-amplitude")
      program.source.recipe.warp.amplitude = parse_float (value);
    else if (name == "continent-frequency")
      program.source.recipe.continent.noise.cycles = parse_int (value);
    else if (name == "plains-frequency")
      program.source.recipe.plains.noise.cycles = parse_int (value);
    else if (name == "mountain-frequency")
      program.source.recipe.mountains.cycles = parse_int (value);
    else if (name == "mask-low")
      program.source.recipe.blend.mask_low = parse_float (value);
    else if (name == "mask-high")
      program.source.recipe.blend.mask_high = parse_float (value);
    else if (name == "continent-weight")
      program.source.recipe.blend.continent_weight = parse_float (value);
    else if (name == "plains-weight")
      program.source.recipe.blend.plains_weight = parse_float (value);
    else if (name == "mountain-weight")
      program.source.recipe.blend.mountain_weight = parse_float (value);
    else
      throw std::invalid_argument ("unknown pipeline option: " +
                                   std::string (name));
  }
}

int main (int argc, char** argv) {
  using namespace moppe;
  using namespace moppe::terrain;

  try {
    const std::string path = argc > 1 ? argv[1] : "pipeline-demo.png";
    const int resolution = argc > 2 ? parse_int (argv[2]) : 257;
    const auto seed =
      static_cast<std::uint32_t> (argc > 3 ? std::stoul (argv[3]) : 0);
    const std::string_view layer_id = argc > 4 ? argv[4] : "combined";
    const auto layer = geological_layer_from_id (layer_id);
    if (resolution < 2)
      throw std::invalid_argument ("resolution must be at least two");
    if (!layer)
      throw std::invalid_argument ("unknown layer: " + std::string (layer_id));

    TerrainProgram program = make_geological_program (seed, *layer);
    for (int i = 5; i < argc; ++i)
      apply_option (program, argv[i]);

    map::RandomHeightMap map (
      resolution, resolution, Vec3 (1, 1, 1), seed, Topology::Torus);
    map::TerrainEvaluator evaluator (map);
    evaluator.begin (program);
    std::vector<TerrainTransformReport> reports;
    for (const TerrainTransform& transform : program.transforms)
      reports.push_back (evaluator.apply (transform));
    const std::size_t count = static_cast<std::size_t> (resolution) *
                              static_cast<std::size_t> (resolution);
    std::vector<float> values (map.raw_heights (), map.raw_heights () + count);
    const ScalarRaster raster (
      { .width = static_cast<std::size_t> (resolution),
        .height = static_cast<std::size_t> (resolution) },
      std::move (values));

    std::ofstream output (path, std::ios::binary);
    if (!output)
      throw std::runtime_error ("cannot open output: " + path);
    write_grayscale_png (output, raster);

    std::cout << "wrote " << path << " (" << layer_id << ", seed " << seed
              << ", " << resolution << "x" << resolution << ", stages";
    for (const TerrainTransform& transform : program.transforms)
      std::cout << " " << terrain_transform_id (transform);
    std::cout << ")\n";
    for (const TerrainTransformReport& result : reports)
      if (const auto* report = std::get_if<AnalyticalErosionReport> (&result))
        std::cout << "analytical: fixed=" << report->fixed_boundaries
                  << " lowered_m3=" << report->lowered_volume_m3
                  << " raised_m3=" << report->raised_volume_m3
                  << " mean_change_m=" << report->mean_absolute_change_m
                  << " max_change_m=" << report->maximum_absolute_change_m
                  << "\n";
      else if (const auto* report = std::get_if<ChannelCarvingReport> (&result))
        std::cout << "carve: reaches=" << report->reaches
                  << " cells=" << report->carved_cells
                  << " lowered_m3=" << report->lowered_volume_m3
                  << " mean_m=" << report->mean_lowering_m
                  << " max_m=" << report->maximum_lowering_m << "\n";
      else if (const auto* report =
                 std::get_if<HydraulicErosionReport> (&result))
        std::cout << "hydraulic: eroded=" << report->eroded
                  << " deposited=" << report->deposited
                  << " discarded=" << report->discarded_sediment
                  << " discarded_fraction=" << report->discarded_fraction ()
                  << " mean_steps=" << report->mean_steps ()
                  << " cap=" << report->stopped_at_step_limit
                  << " water=" << report->stopped_at_water_cutoff
                  << " flat=" << report->stopped_flat << "\n";
  } catch (const std::exception& error) {
    std::cerr << "terrain pipeline demo: " << error.what () << "\n";
    std::cerr << "usage: terrain-pipeline-demo OUTPUT SIZE SEED LAYER "
              << "[world|raw|normalize|NAME=VALUE ...]\n";
    return -1;
  }

  return 0;
}
