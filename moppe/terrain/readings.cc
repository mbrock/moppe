#include <moppe/terrain/readings.hh>

#include <algorithm>

namespace moppe::terrain {
  HeightRange measure_height_range (const TerrainView& terrain) {
    const auto heights = terrain.heights ();
    const auto [minimum, maximum] =
      std::minmax_element (heights.begin (), heights.end ());
    return { *minimum, *maximum };
  }
}
