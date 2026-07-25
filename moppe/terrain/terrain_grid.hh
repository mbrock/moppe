#ifndef MOPPE_TERRAIN_TERRAIN_GRID_HH
#define MOPPE_TERRAIN_TERRAIN_GRID_HH

#include <moppe/quantities.hh>
#include <moppe/terrain/topology.hh>

#include <cstddef>

namespace moppe::terrain {
  // Transitional analysis geometry. TerrainDomain is the authoritative
  // lattice; this value remains while analysis results move onto bundles.
  struct TerrainGrid {
    std::size_t width;
    std::size_t height;
    meters_t spacing_x = 1.0f * mp_units::si::metre;
    meters_t spacing_y = 1.0f * mp_units::si::metre;

    friend bool operator== (const TerrainGrid&, const TerrainGrid&) = default;

    float spacing_x_m () const {
      return meters_value (spacing_x);
    }
    float spacing_y_m () const {
      return meters_value (spacing_y);
    }
    square_meters_t cell_area () const {
      return spacing_x * spacing_y;
    }
  };
}

#endif
