#ifndef MOPPE_TERRAIN_RIVER_HH
#define MOPPE_TERRAIN_RIVER_HH

#include <moppe/terrain/domain.hh>

namespace moppe::terrain {
  // Hydraulic geometry for the visible water surface. Orogeny owns the
  // valley shape; these laws only decide how much of its drainage axis reads
  // as running water. Keeping them outside a terrain transform prevents the
  // renderer from depending on the retired raster channel carve.
  meters_t river_width (square_meters_t contributing_area) noexcept;
  meters_t river_depth (square_meters_t contributing_area) noexcept;

  // A visible river begins at a physical five-metre bankfull width. Terrain
  // resolution may change how finely that water is painted, but must not
  // silently change which tributaries and confluences exist in the world.
  square_meters_t visible_river_minimum_area () noexcept;
}

#endif
