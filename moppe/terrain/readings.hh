#ifndef MOPPE_TERRAIN_READINGS_HH
#define MOPPE_TERRAIN_READINGS_HH

#include <moppe/terrain/terrain_view.hh>

namespace moppe::terrain {
  struct HeightRange {
    float minimum;
    float maximum;
  };

  HeightRange measure_height_range (const TerrainView& terrain);
}

#endif
