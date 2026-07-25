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

  inline SurfaceElevation surface_elevation_point (meters_t value) {
    return SurfaceElevation (meters_value (value) * surface_elevation[u::m]);
  }

  inline float surface_elevation_value (SurfaceElevation value) {
    return value.quantity_from_zero ().numerical_value_in (u::m);
  }

  inline constexpr struct terrain_normal
      : quantity_spec<mp_units::dimensionless,
                      mp_units::quantity_tensor_order::vector,
                      mp_units::is_kind> {
  } terrain_normal;

  using TerrainNormal = mp_units::quantity<terrain_normal[mp_units::one], Vec3>;

  inline constexpr struct surface_moisture
      : quantity_spec<mp_units::dimensionless> {
  } surface_moisture;

  inline constexpr struct waterline_distance
      : quantity_spec<mp_units::isq::length, mp_units::is_kind> {
  } waterline_distance;

  inline constexpr struct standing_water_depth
      : quantity_spec<mp_units::isq::length, mp_units::non_negative> {
  } standing_water_depth;

  inline constexpr struct wave_amplitude
      : quantity_spec<mp_units::dimensionless> {
  } wave_amplitude;

  inline constexpr struct water_velocity
      : quantity_spec<mp_units::isq::speed,
                      mp_units::quantity_tensor_order::vector,
                      mp_units::is_kind> {
  } water_velocity;

  inline constexpr struct trail_influence
      : quantity_spec<mp_units::dimensionless> {
  } trail_influence;

  inline constexpr struct home_base_influence
      : quantity_spec<mp_units::dimensionless> {
  } home_base_influence;

  using SurfaceMoisture = quantity<surface_moisture[one], float>;
  using WaterlineDistance = quantity<waterline_distance[u::m], float>;
  using StandingWaterDepth = quantity<standing_water_depth[u::m], float>;
  using WaveAmplitude = quantity<wave_amplitude[one], float>;
  using WaterVelocity = quantity<water_velocity[u::m / u::s], Vec3>;
  using TrailInfluence = quantity<trail_influence[one], float>;
  using HomeBaseInfluence = quantity<home_base_influence[one], float>;

  static_assert (sizeof (SurfaceElevation) == sizeof (float));
  static_assert (alignof (SurfaceElevation) == alignof (float));
  static_assert (std::is_trivially_copyable_v<SurfaceElevation>);
  static_assert (sizeof (TerrainNormal) == sizeof (Vec3));
  static_assert (alignof (TerrainNormal) == alignof (Vec3));
  static_assert (std::is_trivially_copyable_v<TerrainNormal>);
}

#endif
