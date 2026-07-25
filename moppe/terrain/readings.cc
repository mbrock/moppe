#include <moppe/terrain/readings.hh>

#include <algorithm>

namespace moppe::terrain {
  HeightRange
  detail::measure_height_range (const TerrainDomain& domain,
                                std::span<const SurfaceElevation> elevations) {
    float minimum = elevation_at (domain, elevations, 0, 0);
    float maximum = minimum;
    for (std::size_t row = 0; row < domain.height (); ++row)
      for (std::size_t column = 0; column < domain.width (); ++column) {
        const float value = elevation_at (domain, elevations, column, row);
        minimum = std::min (minimum, value);
        maximum = std::max (maximum, value);
      }
    return { minimum, maximum };
  }
}
