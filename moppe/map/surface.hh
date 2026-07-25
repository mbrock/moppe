#ifndef MOPPE_MAP_SURFACE_HH
#define MOPPE_MAP_SURFACE_HH

#include <moppe/map/surface_atlas.hh>
#include <moppe/terrain/fractional_drainage.hh>
#include <moppe/terrain/raster.hh>

#include <cstdint>
#include <span>
#include <string>

namespace moppe::map {
  // The world's one finite surface store. Its mandatory geometry bundle owns
  // elevation, normals, and material history; optional atlas groups add later
  // analyses over the same domain.
  class Surface {
  public:
    Surface (int width, int height, const Vec3& size);
    Surface (const Surface&) = delete;
    Surface& operator= (const Surface&) = delete;
    Surface (Surface&&) = delete;
    Surface& operator= (Surface&&) = delete;

    int width () const noexcept;
    int height () const noexcept;
    Vec3 scale () const noexcept;
    Vec3 size () const noexcept;

    float relative_elevation_at (int column, int row) const;
    void set_relative_elevation (int column, int row, float value);
    void fill_relative_elevation (float value);
    SurfaceElevation elevation_at (int column, int row) const;
    void set_elevation (int column, int row, SurfaceElevation value);
    void fill_elevation (SurfaceElevation value);
    Vec3 normal_at (int column, int row) const;

    const std::vector<SurfaceElevation>& elevations () const noexcept;
    std::vector<SurfaceElevation>& elevations () noexcept;
    const std::vector<SurfaceNormal>& normals () const noexcept;
    const std::vector<ErodedSurfaceMaterial>& eroded_material () const noexcept;
    std::vector<ErodedSurfaceMaterial>& eroded_material () noexcept;
    const std::vector<DepositedSurfaceMaterial>&
    deposited_material () const noexcept;
    std::vector<DepositedSurfaceMaterial>& deposited_material () noexcept;

    Vec3 vertex (int column, int row) const;
    Vec3 triangle_normal (int x1, int y1, int x2, int y2, int x3, int y3) const;
    Vec3 center () const;
    bool in_bounds (float x, float z) const;
    float interpolated_height (float x, float z) const;
    Vec3 interpolated_normal (float x, float z) const;
    SurfaceElevation min_elevation () const;
    SurfaceElevation max_elevation () const;
    float min_relative_elevation () const;
    float max_relative_elevation () const;

    void rebuild_geometry_readings ();
    void recompute_normals ();
    void reset_material_history ();
    void record_material_change (int column, int row, float delta);

    bool try_load_cache (const std::string& path);
    void save_cache (const std::string& path) const;

    SurfaceElevation elevation_at (const position_t& position) const;
    SurfaceNormal normal_at (const position_t& position) const;
    SnowSupport snow_support_at (const position_t& position) const;
    ChannelFlux channel_flux_at (const position_t& position) const;
    SurfaceMoisture moisture_at (const position_t& position) const;
    WaterlineDistance waterline_distance_at (const position_t& position) const;
    ErosionExposure erosion_exposure_at (const position_t& position) const;
    DepositionCover deposition_cover_at (const position_t& position) const;
    TreeHabitat tree_habitat_at (const position_t& position) const;
    ForestCover forest_cover_at (const position_t& position) const;
    TrailInfluence trail_influence_at (const position_t& position) const;
    HomeBaseInfluence home_base_influence_at (const position_t& position) const;

    void materialize_moisture (const terrain::ScalarRaster& moisture);
    void materialize_waterline_distance (const terrain::ScalarRaster& distance);
    void materialize_channel_flux (const terrain::FractionalDrainage& channels);
    void materialize_moisture (std::span<const float> moisture);
    void materialize_waterline_distance (std::span<const float> distance);
    void derive_geology_materials ();
    void derive_tree_habitat (meters_t water_level, meters_t tree_line);
    void derive_forest_cover (std::uint32_t seed);
    void materialize_trail_influence (std::span<const float> influence);
    void materialize_home_base_influence (std::span<const float> influence);
    void materialize_channel_flux (std::span<const float> flux);

    const SurfaceAtlas& atlas () const noexcept {
      return m_atlas;
    }
    SurfaceAtlas& atlas () noexcept {
      return m_atlas;
    }

  private:
    std::size_t offset (int column, int row) const;
    SurfaceAtlas& mutable_atlas () noexcept {
      return m_atlas;
    }

    meters_t m_height_scale;
    SurfaceAtlas m_atlas;
  };
}

#endif
