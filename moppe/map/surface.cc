#include <moppe/map/surface.hh>

#include <moppe/profile.hh>

#include <algorithm>
#include <cmath>

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
      const terrain::TerrainDomain& domain = geometry.domain ();
      return {
        .width = static_cast<int> (domain.width ()),
        .height = static_cast<int> (domain.height ()),
        .dx =
          std::max (1,
                    static_cast<int> (std::lround (
                      meters_value (support_radius) / domain.spacing_x_m ()))),
        .dz =
          std::max (1,
                    static_cast<int> (std::lround (
                      meters_value (support_radius) / domain.spacing_z_m ()))),
      };
    }

    Vec3 snow_support_normal (const SurfaceGeometry& geometry,
                              const SnowSupportStencil& stencil,
                              int column,
                              int row) {
      column = terrain::wrap_index (column, stencil.width);
      row = terrain::wrap_index (row, stencil.height);
      const auto sample = [&] (int x, int z) {
        const terrain::TerrainIndex index {
          static_cast<std::size_t> (
            terrain::wrap_index (column + x, stencil.width)),
          static_cast<std::size_t> (
            terrain::wrap_index (row + z, stencil.height))
        };
        return spatial::get<terrain::terrain_normal> (geometry[index])
          .numerical_value_in (mp_units::one);
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
      for (int row = 0; row < stencil.height; ++row)
        for (int column = 0; column < stencil.width; ++column) {
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

    void recompute_surface_normals (SurfaceGeometry& geometry) {
      MOPPE_PROFILE_ZONE ("map::recompute_normals");
      const terrain::TerrainDomain& domain = geometry.domain ();
      const int width = static_cast<int> (domain.width ());
      const int height = static_cast<int> (domain.height ());
      const auto& elevations =
        spatial::get<terrain::surface_elevation> (geometry);
      auto& normals = spatial::get<terrain::terrain_normal> (geometry);
      std::ranges::fill (
        normals, Vec3 (0, 0, 0) * terrain::terrain_normal[mp_units::one]);

      const auto point = [&] (int column, int row) {
        const terrain::TerrainIndex index {
          static_cast<std::size_t> (terrain::wrap_index (column, width)),
          static_cast<std::size_t> (terrain::wrap_index (row, height))
        };
        return Vec3 (
          domain.spacing_x_m () * column,
          terrain::surface_elevation_value (elevations[domain.offset (index)]),
          domain.spacing_z_m () * row);
      };
      const auto face = [&] (int x1, int y1, int x2, int y2, int x3, int y3) {
        return normalized (cross (point (x2, y2) - point (x1, y1),
                                  point (x3, y3) - point (x1, y1)));
      };
      const auto add = [&] (int column, int row, const Vec3& value) {
        const terrain::TerrainIndex index {
          static_cast<std::size_t> (terrain::wrap_index (column, width)),
          static_cast<std::size_t> (terrain::wrap_index (row, height))
        };
        SurfaceNormal& normal = normals[domain.offset (index)];
        normal = (normal.numerical_value_in (mp_units::one) + value) *
                 terrain::terrain_normal[mp_units::one];
      };
      for (int row = 0; row < height; ++row)
        for (int column = 0; column < width; ++column) {
          const Vec3 left =
            face (column, row, column, row + 1, column + 1, row + 1);
          const Vec3 right =
            face (column, row, column + 1, row + 1, column + 1, row);
          add (column, row, left);
          add (column, row + 1, left);
          add (column + 1, row + 1, left);
          add (column, row, right);
          add (column + 1, row, right);
          add (column + 1, row + 1, right);
        }
      for (SurfaceNormal& value : normals) {
        Vec3 normal = value.numerical_value_in (mp_units::one);
        normalize (normal);
        value = normal * terrain::terrain_normal[mp_units::one];
      }
    }
  }

  void rebuild_geometry (SurfaceGeometry& geometry) {
    MOPPE_PROFILE_ZONE ("map::rebuild_geometry");
    recompute_surface_normals (geometry);
    populate_snow_support (geometry);
  }

}
