#ifndef MOPPE_TERRAIN_IMAGE_HH
#define MOPPE_TERRAIN_IMAGE_HH

#include <moppe/terrain/cpu_evaluator.hh>

#include <iosfwd>

namespace moppe::terrain {
  // Writes an 8-bit grayscale PNG.  Values at black
  // and white map to 0 and 255; values outside that interval clamp.
  void write_grayscale_png (std::ostream& stream,
			    const ScalarRaster& raster,
			    float black = 0.0f,
			    float white = 1.0f);
}

#endif
