#ifndef MOPPE_TERRAIN_TERRAIN_QUANTITIES_HH
#define MOPPE_TERRAIN_TERRAIN_QUANTITIES_HH

#include <moppe/gfx/math.hh>
#include <moppe/quantities.hh>

#include <type_traits>

namespace moppe::terrain {
  // A point in the world's vertical reference frame. Terrain storage uses
  // metres directly; differences between elevations are ordinary lengths.
  inline constexpr struct surface_elevation
      : quantity_spec<mp_units::isq::height, mp_units::is_kind> {
  } surface_elevation;

  using SurfaceElevation =
    quantity_point<surface_elevation[u::m],
                   default_point_origin (surface_elevation[u::m]),
                   float>;

  inline float surface_elevation_value (SurfaceElevation value) {
    return value.quantity_from_zero ().numerical_value_in (u::m);
  }

  inline constexpr struct terrain_normal
      : quantity_spec<mp_units::dimensionless,
                      mp_units::quantity_tensor_order::vector,
                      mp_units::is_kind> {
  } terrain_normal;

  using TerrainNormal = mp_units::quantity<terrain_normal[mp_units::one], Vec3>;

  static_assert (sizeof (SurfaceElevation) == sizeof (float));
  static_assert (alignof (SurfaceElevation) == alignof (float));
  static_assert (std::is_trivially_copyable_v<SurfaceElevation>);
  static_assert (sizeof (TerrainNormal) == sizeof (Vec3));
  static_assert (alignof (TerrainNormal) == alignof (Vec3));
  static_assert (std::is_trivially_copyable_v<TerrainNormal>);
}

#endif
