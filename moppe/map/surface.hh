#ifndef MOPPE_MAP_SURFACE_HH
#define MOPPE_MAP_SURFACE_HH

#include <moppe/map/surface_sections.hh>
#include <moppe/terrain/fractional_drainage.hh>
#include <moppe/terrain/moisture.hh>
#include <moppe/terrain/trail.hh>
#include <moppe/terrain/waterline.hh>

#include <cstdint>
#include <optional>
#include <string>

namespace moppe::map {
  // The world's one finite surface store. Its mandatory geometry bundle owns
  // elevation, normals, and material history; an optional readings bundle adds
  // later analyses over the same domain.
  class Surface {
  public:
    Surface (int width, int height, const Vec3& size);
    Surface (const Surface&) = delete;
    Surface& operator= (const Surface&) = delete;
    Surface (Surface&&) = delete;
    Surface& operator= (Surface&&) = delete;

    int width () const noexcept;
    int height () const noexcept;
    Vec3 sample_spacing () const noexcept;
    Vec3 world_extent () const noexcept;
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
    Vec3 triangle_normal (int x1, int y1, int x2, int y2, int x3, int y3) const;
    Vec3 center () const;
    bool in_bounds (float x, float z) const;
    float interpolated_height (float x, float z) const;
    Vec3 interpolated_normal (float x, float z) const;
    SurfaceElevation min_elevation () const;
    SurfaceElevation max_elevation () const;

    void rebuild_geometry_readings ();
    void recompute_normals ();
    void reset_material_history ();
    void record_material_change (int column, int row, float delta);

    bool try_load_cache (const std::string& path);
    void save_cache (const std::string& path) const;

    SurfaceElevation elevation_at (const position_t& position) const;
    SurfaceNormal normal_at (const position_t& position) const;
    SnowSupport snow_support_at (const position_t& position) const;
    TreeHabitat tree_habitat_at (const position_t& position) const;
    ForestCover forest_cover_at (const position_t& position) const;

    void set_moisture (terrain::MoistureMap moisture);
    void set_waterline_distance (terrain::WaterlineProximity distance);
    void derive_channel_flux (const terrain::FractionalDrainage& channels);
    void set_use (terrain::TrailUseMap use);
    void derive_geology_materials ();
    void derive_tree_habitat (meters_t water_level, meters_t tree_line);
    void derive_forest_cover (std::uint32_t seed);

    const SurfaceReadings* readings () const noexcept {
      return m_readings ? &*m_readings : nullptr;
    }

  private:
    std::size_t offset (int column, int row) const;
    SurfaceReadings& ensure_readings ();

    meters_t m_vertical_extent;
    SurfaceGeometry m_geometry;
    std::optional<SurfaceReadings> m_readings;
  };
}

#endif
