#ifndef MOPPE_MAP_WATER_SURFACE_HH
#define MOPPE_MAP_WATER_SURFACE_HH

#include <moppe/map/surface_sections.hh>

#include <span>

namespace moppe::map {
  inline constexpr struct wave_amplitude
      : quantity_spec<mp_units::dimensionless> {
  } wave_amplitude;

  inline constexpr struct water_velocity
      : quantity_spec<mp_units::isq::speed,
                      mp_units::quantity_tensor_order::vector,
                      mp_units::is_kind> {
  } water_velocity;

  using WaveAmplitude = quantity<wave_amplitude[one], float>;
  using WaterVelocity = quantity<water_velocity[u::m / u::s], Vec3>;

  using WaterSurfaceSections = spatial::
    Bundle<SurfaceDomain, SurfaceElevation, WaveAmplitude, WaterVelocity>;

  // Standing and running water sampled over the terrain lattice. Elevation
  // shares the ground's affine frame; amplitude and velocity describe the
  // water itself and therefore live in a distinct bundle.
  class WaterSurface {
  public:
    WaterSurface (SurfaceDomain domain,
                  std::span<const float> normalized_level_and_amplitude,
                  std::span<const float> planar_flow,
                  meters_t terrain_height_scale);

    const WaterSurfaceSections& sections () const noexcept {
      return m_sections;
    }

    SurfaceElevation elevation_at (const position_t& position) const {
      return spatial::sample<surface_elevation> (m_sections, position);
    }

    WaterVelocity velocity_at (const position_t& position) const {
      return spatial::sample<water_velocity> (m_sections, position);
    }

  private:
    WaterSurfaceSections m_sections;
  };
}

#endif
