#include <moppe/map/surface.hh>

#include <moppe/profile.hh>

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstring>
#include <fstream>
#include <stdexcept>
#include <vector>

namespace moppe::map {
  namespace {
    std::size_t offset (const SurfaceGeometry& geometry, int column, int row) {
      return geometry.domain ().offset (
        { static_cast<std::size_t> (column), static_cast<std::size_t> (row) });
    }
  }

  int width (const SurfaceGeometry& geometry) noexcept {
    return static_cast<int> (geometry.domain ().width ());
  }

  int height (const SurfaceGeometry& geometry) noexcept {
    return static_cast<int> (geometry.domain ().height ());
  }

  Vec3 sample_spacing (const SurfaceGeometry& geometry) noexcept {
    return Vec3 (meters_value (geometry.domain ().spacing_x ()),
                 1.0f,
                 meters_value (geometry.domain ().spacing_z ()));
  }

  Vec3 world_period (const SurfaceGeometry& geometry) noexcept {
    return Vec3 (meters_value (geometry.domain ().period_x ()),
                 0.0f,
                 meters_value (geometry.domain ().period_z ()));
  }

  SurfaceElevation
  elevation_at (const SurfaceGeometry& geometry, int column, int row) {
    return spatial::get<terrain::surface_elevation> (
      geometry)[offset (geometry, column, row)];
  }

  void set_elevation (SurfaceGeometry& geometry,
                      int column,
                      int row,
                      SurfaceElevation value) {
    spatial::get<terrain::surface_elevation> (
      geometry)[offset (geometry, column, row)] = value;
  }

  void fill_elevation (SurfaceGeometry& geometry, SurfaceElevation value) {
    std::ranges::fill (spatial::get<terrain::surface_elevation> (geometry),
                       value);
  }

  Vec3 normal_at (const SurfaceGeometry& geometry, int column, int row) {
    return spatial::get<terrain::terrain_normal> (
             geometry)[offset (geometry, column, row)]
      .numerical_value_in (mp_units::one);
  }

  Vec3 vertex (const SurfaceGeometry& geometry, int column, int row) {
    const Vec3 spacing = sample_spacing (geometry);
    return Vec3 (
      spacing[0] * column,
      terrain::surface_elevation_value (elevation_at (geometry, column, row)),
      spacing[2] * row);
  }

  bool in_bounds (float x, float z) {
    return std::isfinite (x) && std::isfinite (z);
  }

  // Both reconstructions are the bundle's own interpolation over the terrain
  // domain, which wraps; these only unwrap the quantity for callers that
  // work in plain metres.
  float
  interpolated_height (const SurfaceGeometry& geometry, float x, float z) {
    return terrain::surface_elevation_value (
      elevation_at (geometry, position (Vec3 (x, 0.0f, z))));
  }

  Vec3 interpolated_normal (const SurfaceGeometry& geometry, float x, float z) {
    return normal_at (geometry, position (Vec3 (x, 0.0f, z)))
      .numerical_value_in (mp_units::one);
  }

  SurfaceElevation min_elevation (const SurfaceGeometry& geometry) {
    return *std::ranges::min_element (
      spatial::get<terrain::surface_elevation> (geometry),
      {},
      terrain::surface_elevation_value);
  }

  SurfaceElevation max_elevation (const SurfaceGeometry& geometry) {
    return *std::ranges::max_element (
      spatial::get<terrain::surface_elevation> (geometry),
      {},
      terrain::surface_elevation_value);
  }

  void recompute_normals (SurfaceGeometry& geometry) {
    MOPPE_PROFILE_ZONE ("map::recompute_normals");
    auto& normal_column = spatial::get<terrain::terrain_normal> (geometry);
    std::ranges::fill (normal_column,
                       Vec3 (0, 0, 0) * terrain::terrain_normal[mp_units::one]);
    const Vec3 spacing = sample_spacing (geometry);
    const auto point = [&] (int column, int row) {
      return Vec3 (spacing[0] * column,
                   terrain::surface_elevation_value (elevation_at (
                     geometry,
                     terrain::wrap_index (column, width (geometry)),
                     terrain::wrap_index (row, height (geometry)))),
                   spacing[2] * row);
    };
    const auto face = [&] (int x1, int y1, int x2, int y2, int x3, int y3) {
      return normalized (cross (point (x2, y2) - point (x1, y1),
                                point (x3, y3) - point (x1, y1)));
    };
    const auto add = [&] (int column, int row, const Vec3& value) {
      const std::size_t cell =
        offset (geometry,
                terrain::wrap_index (column, width (geometry)),
                terrain::wrap_index (row, height (geometry)));
      normal_column[cell] =
        (normal_column[cell].numerical_value_in (mp_units::one) + value) *
        terrain::terrain_normal[mp_units::one];
    };
    for (int row = 0; row < height (geometry); ++row)
      for (int column = 0; column < width (geometry); ++column) {
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
    for (SurfaceNormal& value : normal_column) {
      Vec3 normal = value.numerical_value_in (mp_units::one);
      normalize (normal);
      value = normal * terrain::terrain_normal[mp_units::one];
    }
  }

  void reset_material_history (SurfaceGeometry& geometry) {
    std::ranges::fill (spatial::get<eroded_surface_material> (geometry),
                       0.0f * eroded_surface_material[mp_units::one]);
    std::ranges::fill (spatial::get<deposited_surface_material> (geometry),
                       0.0f * deposited_surface_material[mp_units::one]);
  }

  void record_material_change (SurfaceGeometry& geometry,
                               int column,
                               int row,
                               float delta) {
    const std::size_t cell = offset (geometry, column, row);
    auto& eroded = spatial::get<eroded_surface_material> (geometry);
    auto& deposited = spatial::get<deposited_surface_material> (geometry);
    if (delta < 0.0f)
      eroded[cell] = (eroded[cell].numerical_value_in (mp_units::one) - delta) *
                     eroded_surface_material[mp_units::one];
    else
      deposited[cell] =
        (deposited[cell].numerical_value_in (mp_units::one) + delta) *
        deposited_surface_material[mp_units::one];
  }

}
