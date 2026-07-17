#ifndef MOPPE_MAP_SURFACE_HH
#define MOPPE_MAP_SURFACE_HH

#include <moppe/map/surface_sections.hh>

#include <cstdint>
#include <optional>
#include <span>

namespace moppe::map {
  class HeightMap;

  // A materialized reading of the world's surface. Surface owns the shared
  // domain and its typed sections; generation remains authoritative until
  // refresh crosses the explicit materialization barrier.
  class Surface {
  public:
    Surface () = default;
    explicit Surface (const HeightMap& map);

    void refresh (const HeightMap& map);

    SurfaceElevation elevation_at (const position_t& position) const {
      return spatial::sample<surface_elevation> (sections (), position);
    }

    SurfaceNormal normal_at (const position_t& position) const {
      return spatial::sample<surface_normal> (sections (), position);
    }

    SnowSupport snow_support_at (const position_t& position) const {
      return spatial::sample<snow_support> (sections (), position);
    }

    ChannelFlux channel_flux_at (const position_t& position) const {
      return spatial::sample<channel_flux> (sections (), position);
    }

    TreeHabitat tree_habitat_at (const position_t& position) const {
      return spatial::sample<tree_habitat> (sections (), position);
    }

    ForestCover forest_cover_at (const position_t& position) const {
      return spatial::sample<forest_cover> (sections (), position);
    }

    TrailInfluence trail_influence_at (const position_t& position) const {
      return spatial::sample<trail_influence> (sections (), position);
    }

    HomeBaseInfluence
    home_base_influence_at (const position_t& position) const {
      return spatial::sample<home_base_influence> (sections (), position);
    }

    void derive_tree_habitat (std::span<const float> moisture,
                              meters_t water_level,
                              meters_t tree_line);
    void derive_forest_cover (std::uint32_t seed);

    void materialize_trail_influence (std::span<const float> influence);
    void materialize_home_base_influence (std::span<const float> influence);
    void materialize_channel_flux (std::span<const float> flux);

    const SurfaceSections& sections () const;

    template <mp_units::QuantitySpec auto QS>
      requires spatial::BundleContains<QS, SurfaceSections>
    const auto& section () const {
      return spatial::get<QS> (sections ());
    }

  private:
    SurfaceSections& mutable_sections ();

    std::optional<SurfaceSections> m_sections;
  };
}

#endif
