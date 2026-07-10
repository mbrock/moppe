#include <moppe/map/generate.hh>
#include <moppe/terrain/image.hh>
#include <moppe/terrain/pipeline.hh>

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

  void apply_option (moppe::terrain::TerrainPipeline& pipeline,
		     std::string_view option) {
    using namespace moppe::terrain;
    if (option == "world") {
      const GeologicalLayer layer = pipeline.layer;
      pipeline = make_default_world_pipeline (pipeline.randomness.seed);
      pipeline.layer = layer;
      return;
    }
    if (option == "raw") {
      pipeline.stages.clear ();
      return;
    }
    if (option == "normalize") {
      pipeline.stages.emplace_back (NormalizeHeights { });
      return;
    }

    const std::size_t equals = option.find ('=');
    if (equals == std::string_view::npos)
      throw std::invalid_argument
	("unknown pipeline option: " + std::string (option));
    const std::string_view name = option.substr (0, equals);
    const std::string_view value = option.substr (equals + 1);

    if (name == "power")
      pipeline.stages.emplace_back (PowerHeights { parse_float (value) });
    else if (name == "hydraulic")
      pipeline.stages.emplace_back
	(HydraulicErosion { parse_int (value) });
    else if (name == "thermal") {
      const std::size_t comma = value.find (',');
      if (comma == std::string_view::npos)
	throw std::invalid_argument
	  ("thermal expects iterations,talus");
      pipeline.stages.emplace_back (ThermalErosion {
	parse_int (value.substr (0, comma)),
	parse_float (value.substr (comma + 1))
      });
    } else if (name == "warp-amplitude")
      pipeline.recipe.warp.amplitude = parse_float (value);
    else if (name == "continent-frequency")
      pipeline.recipe.continent.noise.cycles = parse_int (value);
    else if (name == "plains-frequency")
      pipeline.recipe.plains.noise.cycles = parse_int (value);
    else if (name == "mountain-frequency")
      pipeline.recipe.mountains.cycles = parse_int (value);
    else if (name == "mask-low")
      pipeline.recipe.blend.mask_low = parse_float (value);
    else if (name == "mask-high")
      pipeline.recipe.blend.mask_high = parse_float (value);
    else if (name == "continent-weight")
      pipeline.recipe.blend.continent_weight = parse_float (value);
    else if (name == "plains-weight")
      pipeline.recipe.blend.plains_weight = parse_float (value);
    else if (name == "mountain-weight")
      pipeline.recipe.blend.mountain_weight = parse_float (value);
    else
      throw std::invalid_argument
	("unknown pipeline option: " + std::string (name));
  }
}

int main (int argc, char** argv) {
  using namespace moppe;
  using namespace moppe::terrain;

  try {
    const std::string path = argc > 1 ? argv[1] : "pipeline-demo.png";
    const int resolution = argc > 2 ? parse_int (argv[2]) : 257;
    const auto seed = static_cast<std::uint32_t>
      (argc > 3 ? std::stoul (argv[3]) : 0);
    const std::string_view layer_id = argc > 4 ? argv[4] : "combined";
    const auto layer = geological_layer_from_id (layer_id);
    if (resolution < 2)
      throw std::invalid_argument ("resolution must be at least two");
    if (!layer)
      throw std::invalid_argument
	("unknown layer: " + std::string (layer_id));

    TerrainPipeline pipeline = make_geological_pipeline (seed, *layer);
    for (int i = 5; i < argc; ++i)
      apply_option (pipeline, argv[i]);

    map::RandomHeightMap map
      (resolution, resolution, Vector3D (1, 1, 1), seed);
    map.run_pipeline (pipeline);
    const std::size_t count = static_cast<std::size_t> (resolution)
      * static_cast<std::size_t> (resolution);
    std::vector<float> values
      (map.raw_heights (), map.raw_heights () + count);
    const ScalarRaster raster (
      { .width = static_cast<std::size_t> (resolution),
	.height = static_cast<std::size_t> (resolution) },
      std::move (values));

    std::ofstream output (path, std::ios::binary);
    if (!output)
      throw std::runtime_error ("cannot open output: " + path);
    write_grayscale_png (output, raster);

    std::cout << "wrote " << path << " (" << layer_id << ", seed "
	      << seed << ", " << resolution << "x" << resolution
	      << ", stages";
    for (const PipelineStage& stage : pipeline.stages)
      std::cout << " " << pipeline_stage_id (stage);
    std::cout << ")\n";
  } catch (const std::exception& error) {
    std::cerr << "terrain pipeline demo: " << error.what () << "\n";
    std::cerr << "usage: terrain-pipeline-demo OUTPUT SIZE SEED LAYER "
	      << "[world|raw|normalize|NAME=VALUE ...]\n";
    return -1;
  }

  return 0;
}
