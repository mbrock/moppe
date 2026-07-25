#ifndef MOPPE_MAP_SURFACE_HH
#define MOPPE_MAP_SURFACE_HH

#include <moppe/map/surface_sections.hh>

#include <string>

namespace moppe::map {
  // The world's finite ground geometry: elevation, normals, and the material
  // history the world's erosion left behind. Readings analysed over the same
  // domain are a separate bundle the completed world owns.
  class Surface {
  public:
    Surface (int width, int height, const Vec3& size);
    // A lattice this large is never copied by accident, but a finished one
    // does move into the world that owns it.
    Surface (const Surface&) = delete;
    Surface& operator= (const Surface&) = delete;
    Surface (Surface&&) = default;
    Surface& operator= (Surface&&) = default;

    int width () const noexcept;
    int height () const noexcept;
    Vec3 sample_spacing () const noexcept;
    // The lattice repeats horizontally and has no vertical bound of its own;
    // a world's full extent belongs to its parameters.
    Vec3 world_period () const noexcept;
    const terrain::TerrainDomain& domain () const noexcept {
      return m_geometry.domain ();
    }

    SurfaceElevation elevation_at (int column, int row) const;
    void set_elevation (int column, int row, SurfaceElevation value);
    void fill_elevation (SurfaceElevation value);
    Vec3 normal_at (int column, int row) const;

    const SurfaceGeometry& geometry () const noexcept {
      return m_geometry;
    }
    SurfaceGeometry& geometry () noexcept {
      return m_geometry;
    }

    Vec3 vertex (int column, int row) const;
    bool in_bounds (float x, float z) const;
    float interpolated_height (float x, float z) const;
    Vec3 interpolated_normal (float x, float z) const;
    SurfaceElevation min_elevation () const;
    SurfaceElevation max_elevation () const;

    void rebuild_geometry ();
    void recompute_normals ();
    void reset_material_history ();
    void record_material_change (int column, int row, float delta);

    bool try_load_cache (const std::string& path);
    void save_cache (const std::string& path) const;

    SurfaceElevation elevation_at (const position_t& position) const;
    SurfaceNormal normal_at (const position_t& position) const;
    SnowSupport snow_support_at (const position_t& position) const;

  private:
    std::size_t offset (int column, int row) const;

    SurfaceGeometry m_geometry;
  };
}

#endif
