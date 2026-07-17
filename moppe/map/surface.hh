#ifndef MOPPE_MAP_SURFACE_HH
#define MOPPE_MAP_SURFACE_HH

#include <moppe/map/surface_atlas.hh>

#include <cstdint>
#include <optional>
#include <span>

namespace moppe::map {
  class HeightMap;

  // A materialized reading of the world's surface. Surface owns the atlas;
  // generation remains authoritative until refresh crosses the geometry
  // barrier, and later analyses materialize their named atlas views.
  class Surface {
  public:
    Surface () = default;
    explicit Surface (const HeightMap& map);

    void refresh (const HeightMap& map);

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

    void materialize_moisture (std::span<const float> moisture);
    void materialize_waterline_distance (std::span<const float> distance);
    void derive_geology_materials (std::span<const float> eroded,
                                   std::span<const float> deposited);
    void derive_tree_habitat (meters_t water_level, meters_t tree_line);
    void derive_forest_cover (std::uint32_t seed);

    void materialize_trail_influence (std::span<const float> influence);
    void materialize_home_base_influence (std::span<const float> influence);
    void materialize_channel_flux (std::span<const float> flux);

    const SurfaceAtlas& atlas () const;

  private:
    SurfaceAtlas& mutable_atlas ();

    std::optional<SurfaceAtlas> m_atlas;
  };
}

#endif
