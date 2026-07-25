#include <moppe/map/surface.hh>

#include <moppe/profile.hh>

#include <algorithm>
#include <cmath>
#include <stdexcept>

namespace moppe::map {
  namespace {
    struct SnowSupportStencil {
      int width;
      int height;
      int dx;
      int dz;
    };

    SnowSupportStencil snow_support_stencil (const SurfaceGeometry& geometry) {
      constexpr meters_t support_radius = 24.0f * u::m;
      const Vec3 spacing = sample_spacing (geometry);
      return {
        .width = width (geometry),
        .height = height (geometry),
        .dx = std::max (1,
                        static_cast<int> (std::lround (
                          meters_value (support_radius) / spacing[0]))),
        .dz = std::max (1,
                        static_cast<int> (std::lround (
                          meters_value (support_radius) / spacing[2]))),
      };
    }

    Vec3 snow_support_normal (const SurfaceGeometry& geometry,
                              const SnowSupportStencil& stencil,
                              int column,
                              int row) {
      column = terrain::wrap_index (column, stencil.width);
      row = terrain::wrap_index (row, stencil.height);
      const auto sample = [&] (int x, int z) {
        return normal_at (geometry,
                          terrain::wrap_index (column + x, stencil.width),
                          terrain::wrap_index (row + z, stencil.height));
      };
      Vec3 support = sample (0, 0) * 4.0f;
      support += (sample (-stencil.dx, 0) + sample (stencil.dx, 0) +
                  sample (0, -stencil.dz) + sample (0, stencil.dz)) *
                 2.0f;
      support +=
        sample (-stencil.dx, -stencil.dz) + sample (stencil.dx, -stencil.dz) +
        sample (-stencil.dx, stencil.dz) + sample (stencil.dx, stencil.dz);
      return normalized (support);
    }

    void populate_snow_support (SurfaceGeometry& geometry) {
      MOPPE_PROFILE_ZONE ("surface.populate_snow_support");
      const SnowSupportStencil stencil = snow_support_stencil (geometry);
      for (int row = 0; row < height (geometry); ++row)
        for (int column = 0; column < width (geometry); ++column) {
          const terrain::TerrainIndex index { static_cast<std::size_t> (column),
                                              static_cast<std::size_t> (row) };
          auto site = geometry[index];
          spatial::get<snow_support> (site) =
            std::clamp (snow_support_normal (geometry, stencil, column, row)[1],
                        0.0f,
                        1.0f) *
            snow_support[one];
        }
    }

  }

  SurfaceGeometry make_surface (int width, int height, const Vec3& size) {
    if (width < 2 || height < 2 || size[0] <= 0.0f || size[1] <= 0.0f ||
        size[2] <= 0.0f)
      throw std::invalid_argument ("Surface dimensions must be positive");
    SurfaceGeometry geometry (
      terrain::TerrainDomain (static_cast<std::size_t> (width),
                              static_cast<std::size_t> (height),
                              size[0] / static_cast<float> (width) * u::m,
                              size[2] / static_cast<float> (height) * u::m));
    reset_material_history (geometry);
    return geometry;
  }

  void rebuild_geometry (SurfaceGeometry& geometry) {
    MOPPE_PROFILE_ZONE ("map::rebuild_geometry");
    recompute_normals (geometry);
    populate_snow_support (geometry);
  }

  SurfaceElevation elevation_at (const SurfaceGeometry& geometry,
                                 const position_t& position) {
    return spatial::sample<terrain::surface_elevation> (geometry, position);
  }

  SurfaceNormal normal_at (const SurfaceGeometry& geometry,
                           const position_t& position) {
    return spatial::sample<terrain::terrain_normal> (geometry, position);
  }

  SnowSupport snow_support_at (const SurfaceGeometry& geometry,
                               const position_t& position) {
    return spatial::sample<snow_support> (geometry, position);
  }

}
