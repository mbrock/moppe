#include <moppe/terrain/cpu_evaluator.hh>
#include <moppe/terrain/field.hh>
#include <moppe/terrain/geological.hh>
#include <moppe/terrain/image.hh>

#include <cstdint>
#include <cstdlib>
#include <fstream>
#include <iostream>
#include <numbers>
#include <optional>
#include <stdexcept>
#include <string>
#include <string_view>

namespace {
  moppe::terrain::ScalarField wave_field () {
    using namespace moppe::terrain;
    const ScalarField x = coordinate_x ();
    const ScalarField y = coordinate_y ();
    const float tau = 2.0f * std::numbers::pi_v<float>;
    return 0.5f
      + 0.25f * sin ((3.0f * tau) * x)
      + 0.25f * sin ((2.0f * tau) * y);
  }

  moppe::terrain::ScalarField preset_field
    (std::string_view preset, std::uint32_t seed) {
    using namespace moppe::terrain;
    if (preset == "waves")
      return wave_field ();

    const std::optional<GeologicalLayer> layer =
      geological_layer_from_id (preset);
    if (!layer)
      throw std::invalid_argument
	("unknown preset: " + std::string (preset));
    const GeologicalFields fields = make_geological_fields
      (derive_geological_seeds (seed));
    return geological_layer (fields, *layer);
  }
}

int main (int argc, char** argv) {
  using namespace moppe::terrain;

  try {
    const std::string path = argc > 1 ? argv[1] : "field-demo.png";
    const int resolution = argc > 2 ? std::atoi (argv[2]) : 512;
    const std::string preset = argc > 3 ? argv[3] : "waves";
    const auto seed = static_cast<std::uint32_t>
      (argc > 4 ? std::strtoul (argv[4], nullptr, 10) : 0);
    if (resolution < 2)
      throw std::invalid_argument ("resolution must be at least two");

    const ScalarField field = preset_field (preset, seed);

    const Domain2D domain {
      .width = static_cast<std::size_t> (resolution),
      .height = static_cast<std::size_t> (resolution)
    };
    const ScalarRaster raster = normalize
      (CpuEvaluator ().evaluate (field, domain));

    std::ofstream output (path, std::ios::binary);
    if (!output)
      throw std::runtime_error ("cannot open output: " + path);
    write_grayscale_png (output, raster);

    std::cout << "wrote " << path << " (" << preset << ", seed "
	      << seed << ", " << resolution << "x" << resolution << ", "
	      << unique_node_count (field)
	      << " DAG nodes, range " << raster.min_value () << ".."
	      << raster.max_value () << ")\n";
  } catch (const std::exception& error) {
    std::cerr << "terrain field demo: " << error.what () << "\n";
    return -1;
  }

  return 0;
}
