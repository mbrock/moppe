#ifndef MOPPE_TERRAIN_READINGS_HH
#define MOPPE_TERRAIN_READINGS_HH

#include <moppe/terrain/domain.hh>

namespace moppe::terrain {
  // The lowest and highest ground on the lattice.
  struct ElevationRange {
    SurfaceElevation minimum;
    SurfaceElevation maximum;
  };

  // The same in bare metres, for callers not yet typed; delete when the
  // last one reads an ElevationRange.
  struct HeightRange {
    float minimum;
    float maximum;
  };

  namespace detail {
    ElevationRange
    measure_elevation_range (const TerrainDomain& domain,
                             std::span<const SurfaceElevation> elevations);
  }

  template <TerrainElevations Terrain>
  ElevationRange measure_elevation_range (const Terrain& terrain) {
    return detail::measure_elevation_range (terrain.domain (),
                                            elevations (terrain));
  }

  template <TerrainElevations Terrain>
  HeightRange measure_height_range (const Terrain& terrain) {
    const ElevationRange range = measure_elevation_range (terrain);
    return { surface_elevation_value (range.minimum),
             surface_elevation_value (range.maximum) };
  }
}

#endif
