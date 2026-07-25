#ifndef MOPPE_MAP_SURFACE_HH
#define MOPPE_MAP_SURFACE_HH

#include <moppe/map/surface_sections.hh>

// The world's finite ground geometry is the SurfaceGeometry bundle itself:
// elevation, normals, and the material history the world's erosion left
// behind, over one terrain domain. These are the operations the game and the
// generator perform on it. Readings analysed over the same domain live in a
// separate bundle the completed world owns.

namespace moppe::map {
  SurfaceGeometry make_surface (int width, int height, const Vec3& size);

  int width (const SurfaceGeometry& geometry) noexcept;
  int height (const SurfaceGeometry& geometry) noexcept;
  Vec3 sample_spacing (const SurfaceGeometry& geometry) noexcept;
  // The lattice repeats horizontally and has no vertical bound of its own;
  // a world's full extent belongs to its parameters.
  Vec3 world_period (const SurfaceGeometry& geometry) noexcept;

  SurfaceElevation
  elevation_at (const SurfaceGeometry& geometry, int column, int row);
  void set_elevation (SurfaceGeometry& geometry,
                      int column,
                      int row,
                      SurfaceElevation value);
  void fill_elevation (SurfaceGeometry& geometry, SurfaceElevation value);
  Vec3 normal_at (const SurfaceGeometry& geometry, int column, int row);
  Vec3 vertex (const SurfaceGeometry& geometry, int column, int row);
  SurfaceElevation min_elevation (const SurfaceGeometry& geometry);
  SurfaceElevation max_elevation (const SurfaceGeometry& geometry);

  // The world wraps, so every finite point names a place on it.
  bool in_bounds (float x, float z);

  SurfaceElevation elevation_at (const SurfaceGeometry& geometry,
                                 const position_t& position);
  SurfaceNormal normal_at (const SurfaceGeometry& geometry,
                           const position_t& position);
  SnowSupport snow_support_at (const SurfaceGeometry& geometry,
                               const position_t& position);
  float interpolated_height (const SurfaceGeometry& geometry, float x, float z);
  Vec3 interpolated_normal (const SurfaceGeometry& geometry, float x, float z);

  // Normals and the broad snow support plane follow from elevation; rebuild
  // them whenever the heightfield changes.
  void rebuild_geometry (SurfaceGeometry& geometry);
  void recompute_normals (SurfaceGeometry& geometry);
  void reset_material_history (SurfaceGeometry& geometry);
  void record_material_change (SurfaceGeometry& geometry,
                               int column,
                               int row,
                               float delta);
}

#endif
