#include <moppe/terrain/readings.hh>

#include <algorithm>
#include <vector>

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

  meters_t
  detail::measure_climatic_relief (const TerrainDomain& domain,
                                   std::span<const SurfaceElevation> elevations,
                                   SurfaceElevation sea_level) {
    const std::size_t width = domain.width ();
    const std::size_t height = domain.height ();
    const std::size_t step_x = std::max<std::size_t> (1, width / 256);
    const std::size_t step_z = std::max<std::size_t> (1, height / 256);
    std::vector<meters_t> above;
    above.reserve (((width + step_x - 1) / step_x) *
                   ((height + step_z - 1) / step_z));
    meters_t maximum = 0.0f * u::m;
    for (std::size_t row = 0; row < height; row += step_z)
      for (std::size_t column = 0; column < width; column += step_x) {
        const SurfaceElevation value =
          elevation_point_at (domain, elevations, column, row);
        if (value > sea_level) {
          const meters_t rise = quantity_cast<isq::length> (value - sea_level);
          above.push_back (rise);
          maximum = std::max (maximum, rise);
        }
      }
    if (above.empty ())
      return 1.0f * u::m;
    // Use the 93rd percentile of above-sea land scaled to massif relief so even
    // a broad alpine range covering 3-6% of the map keeps the tree line and
    // snowline anchored below the kalfjäll snowfield plateau.
    const std::size_t index = (above.size () - 1) * 930 / 1000;
    std::nth_element (above.begin (), above.begin () + index, above.end ());
    const meters_t quantile_relief = above[index] / 0.68f;
    return std::max (1.0f * u::m, std::min (maximum, quantile_relief));
  }
}
