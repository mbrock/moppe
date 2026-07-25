#include <moppe/game/surface_presentation.hh>

#include <moppe/profile.hh>
#include <moppe/render/texture_pixels.hh>

namespace moppe::game {
  void upload_surface_readings (render::Renderer& renderer,
                                const map::SurfaceGeometry& geometry,
                                const map::SurfaceReadings& readings,
                                bool include_forest) {
    MOPPE_PROFILE_ZONE ("surface.upload_readings");
    using render::PixelFormat;
    using render::planar_texture_pixels;
    using render::texture_pixels;

    renderer.set_terrain_moisture (
      texture_pixels<map::surface_moisture> (readings, PixelFormat::r32f));
    renderer.set_terrain_geology (
      texture_pixels<map::erosion_exposure, map::deposition_cover> (
        readings, PixelFormat::rg16f));
    renderer.set_terrain_shore (
      texture_pixels<map::waterline_distance> (readings, PixelFormat::r16f));
    renderer.set_terrain_snow_support (
      texture_pixels<map::snow_support> (geometry, PixelFormat::r16f));
    renderer.set_terrain_paths (
      texture_pixels<map::trail_influence, map::home_base_influence> (
        readings, PixelFormat::rg16f));

    // The flux reading is a planar vector; the shader wants its two
    // horizontal lanes and reconstructs nothing else.
    renderer.set_terrain_channel_flux (
      planar_texture_pixels<map::channel_flux> (readings, PixelFormat::rg16f));

    // An empty source is how a caller says "leave this overlay off".
    renderer.set_terrain_forest (
      include_forest
        ? texture_pixels<map::forest_cover> (readings, PixelFormat::r32f)
        : render::TexturePixels ());
  }
}
