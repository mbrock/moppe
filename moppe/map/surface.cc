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

    SnowSupportStencil snow_support_stencil (const Surface& surface) {
      constexpr meters_t support_radius = 24.0f * u::m;
      const Vec3 spacing = surface.sample_spacing ();
      return {
        .width = surface.width (),
        .height = surface.height (),
        .dx = std::max (1,
                        static_cast<int> (std::lround (
                          meters_value (support_radius) / spacing[0]))),
        .dz = std::max (1,
                        static_cast<int> (std::lround (
                          meters_value (support_radius) / spacing[2]))),
      };
    }

    Vec3 snow_support_normal (const Surface& surface,
                              const SnowSupportStencil& stencil,
                              int column,
                              int row) {
      column = terrain::wrap_index (column, stencil.width);
      row = terrain::wrap_index (row, stencil.height);
      const auto sample = [&] (int x, int z) {
        return surface.normal_at (
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

    void populate_snow_support (SurfaceGeometry& sections,
                                const Surface& surface) {
      MOPPE_PROFILE_ZONE ("surface.populate_snow_support");
      const SnowSupportStencil support_stencil = snow_support_stencil (surface);
      for (int row = 0; row < surface.height (); ++row)
        for (int column = 0; column < surface.width (); ++column) {
          const terrain::TerrainIndex index { static_cast<std::size_t> (column),
                                              static_cast<std::size_t> (row) };
          auto site = sections[index];
          spatial::get<snow_support> (site) =
            std::clamp (
              snow_support_normal (surface, support_stencil, column, row)[1],
              0.0f,
              1.0f) *
            snow_support[one];
        }
    }

  }

  Surface::Surface (int width, int height, const Vec3& size)
      : m_vertical_extent (size[1] * u::m),
        m_geometry (terrain::TerrainDomain (
          static_cast<std::size_t> (width),
          static_cast<std::size_t> (height),
          size[0] / static_cast<float> (width) * u::m,
          size[2] / static_cast<float> (height) * u::m)) {
    if (width < 2 || height < 2 || size[0] <= 0.0f || size[1] <= 0.0f ||
        size[2] <= 0.0f)
      throw std::invalid_argument ("Surface dimensions must be positive");
    reset_material_history ();
  }

  void Surface::rebuild_geometry () {
    MOPPE_PROFILE_ZONE ("Surface::rebuild_geometry");
    recompute_normals ();
    populate_snow_support (m_geometry, *this);
  }

  SurfaceElevation Surface::elevation_at (const position_t& position) const {
    const Vec3 point = position_value (position);
    return SurfaceElevation (interpolated_height (point[0], point[2]) *
                             surface_elevation[u::m]);
  }

  SurfaceNormal Surface::normal_at (const position_t& position) const {
    return spatial::sample<terrain::terrain_normal> (geometry (), position);
  }

  SnowSupport Surface::snow_support_at (const position_t& position) const {
    return spatial::sample<snow_support> (geometry (), position);
  }

}
