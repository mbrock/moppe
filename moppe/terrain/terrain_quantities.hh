#ifndef MOPPE_TERRAIN_TERRAIN_QUANTITIES_HH
#define MOPPE_TERRAIN_TERRAIN_QUANTITIES_HH

#include <moppe/gfx/math.hh>
#include <moppe/quantities.hh>

#include <type_traits>

namespace moppe::terrain {
  // Elevation as a fraction of a terrain's configured vertical scale.
  inline constexpr struct relative_terrain_elevation
      : quantity_spec<mp_units::dimensionless, mp_units::is_kind> {
  } relative_terrain_elevation;

  using RelativeTerrainElevation =
    mp_units::quantity<relative_terrain_elevation[mp_units::one], float>;

  inline constexpr struct terrain_normal
      : quantity_spec<mp_units::dimensionless,
                      mp_units::quantity_tensor_order::vector,
                      mp_units::is_kind> {
  } terrain_normal;

  using TerrainNormal = mp_units::quantity<terrain_normal[mp_units::one], Vec3>;

  static_assert (sizeof (RelativeTerrainElevation) == sizeof (float));
  static_assert (alignof (RelativeTerrainElevation) == alignof (float));
  static_assert (std::is_trivially_copyable_v<RelativeTerrainElevation>);
  static_assert (sizeof (TerrainNormal) == sizeof (Vec3));
  static_assert (alignof (TerrainNormal) == alignof (Vec3));
  static_assert (std::is_trivially_copyable_v<TerrainNormal>);
}

#endif
