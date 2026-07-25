#include <moppe/terrain/readings.hh>

#include <algorithm>

namespace moppe::terrain {
  HeightRange measure_height_range (const TerrainView& terrain) {
    float minimum = terrain.at (0, 0);
    float maximum = minimum;
    for (std::size_t row = 0; row < terrain.domain ().height (); ++row)
      for (std::size_t column = 0; column < terrain.domain ().width ();
           ++column) {
        const float value = terrain.at (column, row);
        minimum = std::min (minimum, value);
        maximum = std::max (maximum, value);
      }
    return { minimum, maximum };
  }
}
