#include <moppe/game/water_presentation.hh>

#include <moppe/profile.hh>
#include <moppe/render/texture_pixels.hh>

namespace moppe::game {
  namespace {
    constexpr meters_t ocean_half_extent = 5500.0f * u::m;

    render::OceanSetup ocean_setup (meters_t water_datum,
                                    const spatial_extent_t& world_extent) {
      const Vec3& extent = extent_value (world_extent);
      return {
        .level = (water_datum).numerical_value_in (moppe::u::m),
        .center = Vec3 (0.5f * extent[0], 0.0f, 0.5f * extent[2]),
        .half_extent = (ocean_half_extent).numerical_value_in (moppe::u::m),
        .cells = 300,
      };
    }
  }

  void upload_water (render::Renderer& renderer,
                     const terrain::WaterSheets& water,
                     meters_t water_datum,
                     const spatial_extent_t& world_extent) {
    MOPPE_PROFILE_ZONE ("water.upload");
    using render::PixelFormat;
    renderer.set_ocean (ocean_setup (water_datum, world_extent),
                        render::texture_pixels<terrain::surface_elevation,
                                               terrain::wave_amplitude> (
                          water, PixelFormat::rg32f));
    renderer.set_water_flow (
      render::planar_texture_pixels<terrain::water_velocity> (
        water, PixelFormat::rg16f));
  }
}
