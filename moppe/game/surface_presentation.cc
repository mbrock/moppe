#include <moppe/game/surface_presentation.hh>

#include <moppe/game/river_stones.hh>

#include <moppe/profile.hh>
#include <moppe/render/texture_pixels.hh>

namespace moppe::game {
  namespace {
    struct TerrainMaterialSource {
      const map::SurfaceGeometry& geometry;
      const map::SurfaceReadings& readings;
      const terrain::WaterSheets& water;
      bool include_forest;
      RiverBedReading river_bed;
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
        // Under running water and on the strands beside it the ground is
        // what the current leaves: the ground draws the deposition lane
        // by the water as sand where it is fine and cobbles where it is
        // coarse, so there the lane carries the bed's fineness instead of
        // the land's deposition.
        const float fineness = source.river_bed.fineness[pixel];
        const float laid =
          fineness >= 0.0f ? fineness
                           : render::detail::stored_scalar (deposition[pixel]);
        render::detail::write_channel (
          at, format, render::detail::stored_scalar (moisture[pixel]));
        render::detail::write_channel (
          at + 1, format, render::detail::stored_scalar (erosion[pixel]));
        render::detail::write_channel (at + 2, format, laid);
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
      const auto& ground =
        spatial::get<terrain::surface_elevation> (source.geometry);
      const auto& level =
        spatial::get<terrain::surface_elevation> (source.water);
      const auto& trail = spatial::get<map::trail_influence> (source.readings);
      const auto& home =
        spatial::get<map::home_base_influence> (source.readings);
      const std::size_t stride = render::bytes_per_pixel (format);
      for (std::size_t pixel = 0; pixel < shore.size (); ++pixel) {
        std::byte* at = destination + pixel * stride;
        // A strand a river's spate covers reads as its shore, so the ground
        // draws it bare by the water and lets grass take the higher parts.
        float metres = render::detail::stored_scalar (shore[pixel]);
        if (const float strand = source.river_bed.strand[pixel]; strand >= 0.0f)
          metres = std::min (metres, 0.8f + 2.0f * strand);
        render::detail::write_channel (
          at,
          format,
          render::terrain_shore_lane (metres, level[pixel] > ground[pixel]));
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
                                const terrain::WaterSheets& water,
                                bool include_forest) {
    MOPPE_PROFILE_ZONE ("surface.upload_readings");
    const TerrainMaterialSource source {
      geometry,
      readings,
      water,
      include_forest,
      read_river_bed (geometry, water),
    };
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
