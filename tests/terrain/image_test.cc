#include <moppe/terrain/cpu_evaluator.hh>
#include <moppe/terrain/image.hh>

#include <tests/test.hh>

#include <sstream>
#include <string>
#include <vector>

using namespace moppe::terrain;

MOPPE_TEST (grayscale_png_has_expected_header) {
  const Domain2D domain { .width = 2, .height = 2 };
  const ScalarRaster raster
    (domain, std::vector<float> { 0.0f, 0.5f, 1.0f, 2.0f });
  std::ostringstream output (std::ios::binary);
  write_grayscale_png (output, raster);
  const std::string bytes = output.str ();

  MOPPE_CHECK (bytes.size () > 50);
  MOPPE_CHECK (static_cast<unsigned char> (bytes[0]) == 0x89);
  MOPPE_CHECK (bytes.substr (1, 3) == "PNG");
  MOPPE_CHECK (bytes.substr (12, 4) == "IHDR");
  MOPPE_CHECK (static_cast<unsigned char> (bytes[19]) == 2);
  MOPPE_CHECK (static_cast<unsigned char> (bytes[23]) == 2);
  MOPPE_CHECK (static_cast<unsigned char> (bytes[24]) == 8);
  MOPPE_CHECK (static_cast<unsigned char> (bytes[25]) == 0);
  MOPPE_CHECK (bytes.substr (bytes.size () - 8, 4) == "IEND");
}
