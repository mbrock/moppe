#include <moppe/game/water_presentation.hh>

#include <moppe/profile.hh>
#include <moppe/render/texture_pixels.hh>

#include <cstdint>
#include <utility>
#include <vector>

namespace moppe::game {
  namespace {
    constexpr meters_t ocean_half_extent = 5500.0f * u::m;

    // The terrain patches where the sheet stands above the ground. The
    // world is periodic, and a sample on a patch's edge belongs to the
    // patches on both sides of it.
    std::vector<std::uint8_t>
    wet_patches (const terrain::WaterSheets& water,
                 std::span<const terrain::SurfaceElevation> ground) {
      const std::size_t width = water.domain ().width ();
      const std::size_t height = water.domain ().height ();
      const std::size_t patch = render::terrain_patch_cells;
      if (ground.size () != width * height || width % patch || height % patch)
        return {};
      const auto levels = spatial::get<terrain::surface_elevation> (water);
      const std::size_t across = width / patch, down = height / patch;
      std::vector<std::uint8_t> wet (across * down);
      for (std::size_t z = 0; z < height; ++z)
        for (std::size_t x = 0; x < width; ++x) {
          if (!(levels[z * width + x] > ground[z * width + x]))
            continue;
          const std::size_t px = x / patch, pz = z / patch;
          const std::size_t before_x =
            x % patch ? px : (px + across - 1) % across;
          const std::size_t before_z =
            z % patch ? pz : (pz + down - 1) % down;
          for (std::size_t row : { pz, before_z })
            for (std::size_t column : { px, before_x })
              wet[row * across + column] = 1;
        }
      return wet;
    }

    render::OceanSetup ocean_setup (meters_t water_datum,
                                    const spatial_extent_t& world_extent,
                                    std::vector<std::uint8_t> wet) {
      const Vec3& extent = extent_value (world_extent);
      return {
        .level = (water_datum).numerical_value_in (moppe::u::m),
        .center = Vec3 (0.5f * extent[0], 0.0f, 0.5f * extent[2]),
        .half_extent = (ocean_half_extent).numerical_value_in (moppe::u::m),
        .cells = 300,
        .wet_patches = std::move (wet),
      };
    }
  }

  void upload_water (render::Renderer& renderer,
                     const terrain::WaterSheets& water,
                     std::span<const terrain::SurfaceElevation> ground,
                     meters_t water_datum,
                     const spatial_extent_t& world_extent) {
    MOPPE_PROFILE_ZONE ("water.upload");
    using render::PixelFormat;
    renderer.set_ocean (ocean_setup (water_datum, world_extent,
                                     wet_patches (water, ground)),
                        render::texture_pixels<terrain::surface_elevation,
                                               terrain::wave_amplitude> (
                          water, PixelFormat::rg32f));
    renderer.set_water_flow (
      render::planar_texture_pixels<terrain::water_velocity> (
        water, PixelFormat::rg16f));
  }
}
