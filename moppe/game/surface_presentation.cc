#include <moppe/game/surface_presentation.hh>

#include <moppe/profile.hh>
#include <moppe/render/texture_pixels.hh>

namespace moppe::game {
  namespace {
    struct TerrainMaterialSource {
      const map::SurfaceGeometry& geometry;
      const map::SurfaceReadings& readings;
      bool include_forest;
    };

    void write_landscape_materials (const void* opaque,
                                    render::PixelFormat format,
                                    std::byte* destination) {
      const auto& source = *static_cast<const TerrainMaterialSource*> (opaque);
      const auto& moisture =
        spatial::get<map::surface_moisture> (source.readings);
      const auto& erosion =
        spatial::get<map::erosion_exposure> (source.readings);
      const auto& deposition =
        spatial::get<map::deposition_cover> (source.readings);
      const auto& forest = spatial::get<map::forest_cover> (source.readings);
      const std::size_t stride = render::bytes_per_pixel (format);
      for (std::size_t pixel = 0; pixel < moisture.size (); ++pixel) {
        std::byte* at = destination + pixel * stride;
        render::detail::write_channel (
          at, format, render::detail::stored_scalar (moisture[pixel]));
        render::detail::write_channel (
          at + 1, format, render::detail::stored_scalar (erosion[pixel]));
        render::detail::write_channel (
          at + 2, format, render::detail::stored_scalar (deposition[pixel]));
        render::detail::write_channel (
          at + 3,
          format,
          source.include_forest ? render::detail::stored_scalar (forest[pixel])
                                : 0.0f);
      }
    }

    void write_ground_materials (const void* opaque,
                                 render::PixelFormat format,
                                 std::byte* destination) {
      const auto& source = *static_cast<const TerrainMaterialSource*> (opaque);
      const auto& shore =
        spatial::get<map::waterline_distance> (source.readings);
      const auto& snow = spatial::get<map::snow_support> (source.geometry);
      const auto& trail = spatial::get<map::trail_influence> (source.readings);
      const auto& home =
        spatial::get<map::home_base_influence> (source.readings);
      const std::size_t stride = render::bytes_per_pixel (format);
      for (std::size_t pixel = 0; pixel < shore.size (); ++pixel) {
        std::byte* at = destination + pixel * stride;
        const float shore_fraction =
          render::detail::stored_scalar (shore[pixel]) /
          render::terrain_shore_band_metres;
        render::detail::write_channel (at, format, shore_fraction);
        render::detail::write_channel (
          at + 1, format, render::detail::stored_scalar (snow[pixel]));
        render::detail::write_channel (
          at + 2, format, render::detail::stored_scalar (trail[pixel]));
        render::detail::write_channel (
          at + 3, format, render::detail::stored_scalar (home[pixel]));
      }
    }

  }

  void upload_surface_readings (render::Renderer& renderer,
                                const map::SurfaceGeometry& geometry,
                                const map::SurfaceReadings& readings,
                                bool include_forest) {
    MOPPE_PROFILE_ZONE ("surface.upload_readings");
    const TerrainMaterialSource source { geometry, readings, include_forest };
    const auto pixels = [&] (render::PixelFormat format,
                             render::TexturePixels::Writer writer) {
      return render::TexturePixels (readings.domain ().width (),
                                    readings.domain ().height (),
                                    format,
                                    &source,
                                    writer);
    };

    renderer.set_terrain_materials (
      pixels (render::PixelFormat::rgba8unorm, &write_landscape_materials),
      pixels (render::PixelFormat::rgba8unorm, &write_ground_materials),
      include_forest);
  }
}
