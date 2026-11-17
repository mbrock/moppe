#include <moppe/terrain/readings.hh>

#include <algorithm>

namespace moppe::terrain {
  ElevationRange detail::measure_elevation_range (
    const TerrainDomain& domain, std::span<const SurfaceElevation> elevations) {
    SurfaceElevation minimum = elevation_point_at (domain, elevations, 0, 0);
    SurfaceElevation maximum = minimum;
    for (std::size_t row = 0; row < domain.height (); ++row)
      for (std::size_t column = 0; column < domain.width (); ++column) {
        const SurfaceElevation value =
          elevation_point_at (domain, elevations, column, row);
        minimum = std::min (minimum, value);
        maximum = std::max (maximum, value);
      }
    return { minimum, maximum };
  }
}
