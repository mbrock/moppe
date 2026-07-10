#include <moppe/terrain/cpu_evaluator.hh>
#include <moppe/terrain/field.hh>
#include <moppe/terrain/image.hh>

#include <cstdlib>
#include <fstream>
#include <iostream>
#include <numbers>
#include <stdexcept>
#include <string>

int main (int argc, char** argv) {
  using namespace moppe::terrain;

  try {
    const std::string path = argc > 1 ? argv[1] : "field-demo.png";
    const int resolution = argc > 2 ? std::atoi (argv[2]) : 512;
    if (resolution < 2)
      throw std::invalid_argument ("resolution must be at least two");

    const ScalarField x = coordinate_x ();
    const ScalarField y = coordinate_y ();
    const float tau = 2.0f * std::numbers::pi_v<float>;
    const ScalarField field = 0.5f
      + 0.25f * sin ((3.0f * tau) * x)
      + 0.25f * sin ((2.0f * tau) * y);

    const Domain2D domain {
      .width = static_cast<std::size_t> (resolution),
      .height = static_cast<std::size_t> (resolution)
    };
    const ScalarRaster raster = CpuEvaluator ().evaluate (field, domain);

    std::ofstream output (path, std::ios::binary);
    if (!output)
      throw std::runtime_error ("cannot open output: " + path);
    write_grayscale_png (output, raster);

    std::cout << "wrote " << path << " (" << resolution << "x"
	      << resolution << ", " << unique_node_count (field)
	      << " DAG nodes, range " << raster.min_value () << ".."
	      << raster.max_value () << ")\n";
  } catch (const std::exception& error) {
    std::cerr << "terrain field demo: " << error.what () << "\n";
    return -1;
  }

  return 0;
}
