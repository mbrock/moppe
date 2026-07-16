#ifndef MOPPE_TERRAIN_RIVER_HH
#define MOPPE_TERRAIN_RIVER_HH

#include <moppe/terrain/terrain_view.hh>

namespace moppe::terrain {
  // Hydraulic geometry for the visible water surface. Orogeny owns the
  // valley shape; these laws only decide how much of its drainage axis reads
  // as running water. Keeping them outside a terrain transform prevents the
  // renderer from depending on the retired raster channel carve.
  meters_t river_width (square_meters_t contributing_area) noexcept;
  meters_t river_depth (square_meters_t contributing_area) noexcept;

  // A visible channel begins when the width law reaches two terrain cells.
  // Expressing that threshold through the source grid keeps the same physical
  // river network across generation profiles.
  square_meters_t
  visible_river_minimum_area (const TerrainGrid& grid) noexcept;
}

#endif
