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

    // High-quantile terrestrial relief above sea level (92nd percentile of
    // above-sea cells scaled to massif relief, capped at true maximum relief).
    // Keeps biome boundaries (tree line, snowline, frozen alpine tarns)
    // anchored to the massif even when jagged horn peaks tower above it.
    meters_t
    measure_climatic_relief (const TerrainDomain& domain,
                             std::span<const SurfaceElevation> elevations,
                             SurfaceElevation sea_level);
  }

  template <TerrainElevations Terrain>
  ElevationRange measure_elevation_range (const Terrain& terrain) {
    return detail::measure_elevation_range (terrain.domain (),
                                            elevations (terrain));
  }

  template <TerrainElevations Terrain>
  meters_t measure_climatic_relief (const Terrain& terrain,
                                    SurfaceElevation sea_level) {
    return detail::measure_climatic_relief (
      terrain.domain (), elevations (terrain), sea_level);
  }

  template <TerrainElevations Terrain>
  HeightRange measure_height_range (const Terrain& terrain) {
    const ElevationRange range = measure_elevation_range (terrain);
    return { surface_elevation_value (range.minimum),
             surface_elevation_value (range.maximum) };
  }
}

#endif
