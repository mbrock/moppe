#ifndef MOPPE_TERRAIN_READINGS_HH
#define MOPPE_TERRAIN_READINGS_HH

#include <moppe/terrain/domain.hh>

namespace moppe::terrain {
  struct HeightRange {
    float minimum;
    float maximum;
  };

  namespace detail {
    HeightRange
    measure_height_range (const TerrainDomain& domain,
                          std::span<const SurfaceElevation> elevations);
  }

  template <TerrainElevations Terrain>
  HeightRange measure_height_range (const Terrain& terrain) {
    return detail::measure_height_range (terrain.domain (),
                                         elevations (terrain));
  }
}

#endif
