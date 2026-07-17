#ifndef MOPPE_MAP_SURFACE_HH
#define MOPPE_MAP_SURFACE_HH

#include <moppe/map/surface_sections.hh>

#include <cstdint>
#include <optional>
#include <span>
#include <type_traits>

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

    SurfaceMoisture moisture_at (const position_t& position) const {
      return spatial::sample<surface_moisture> (sections (), position);
    }

    WaterlineDistance waterline_distance_at (const position_t& position) const {
      return spatial::sample<waterline_distance> (sections (), position);
    }

    ErosionExposure erosion_exposure_at (const position_t& position) const {
      return spatial::sample<erosion_exposure> (sections (), position);
    }

    DepositionCover deposition_cover_at (const position_t& position) const {
      return spatial::sample<deposition_cover> (sections (), position);
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

    void materialize_moisture (std::span<const float> moisture);
    void materialize_waterline_distance (std::span<const float> distance);
    void derive_geology_materials (std::span<const float> eroded,
                                   std::span<const float> deposited);
    void derive_tree_habitat (meters_t water_level, meters_t tree_line);
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

    template <mp_units::QuantitySpec auto QS>
      requires spatial::BundleContains<QS, SurfaceSections>
    bool has_section () const noexcept {
      using Spec = std::remove_cvref_t<decltype (QS)>;
      if constexpr (
        std::same_as<Spec, std::remove_cvref_t<decltype (surface_elevation)>> ||
        std::same_as<Spec, std::remove_cvref_t<decltype (surface_normal)>> ||
        std::same_as<Spec, std::remove_cvref_t<decltype (snow_support)>>)
        return m_sections.has_value ();
      else if constexpr (std::same_as<
                           Spec,
                           std::remove_cvref_t<decltype (channel_flux)>>)
        return m_materialized.channel_flux;
      else if constexpr (std::same_as<
                           Spec,
                           std::remove_cvref_t<decltype (surface_moisture)>>)
        return m_materialized.moisture;
      else if constexpr (std::same_as<
                           Spec,
                           std::remove_cvref_t<decltype (waterline_distance)>>)
        return m_materialized.waterline;
      else if constexpr (
        std::same_as<Spec, std::remove_cvref_t<decltype (erosion_exposure)>> ||
        std::same_as<Spec, std::remove_cvref_t<decltype (deposition_cover)>>)
        return m_materialized.geology;
      else if constexpr (std::same_as<
                           Spec,
                           std::remove_cvref_t<decltype (tree_habitat)>>)
        return m_materialized.tree_habitat;
      else if constexpr (std::same_as<
                           Spec,
                           std::remove_cvref_t<decltype (forest_cover)>>)
        return m_materialized.forest_cover;
      else if constexpr (std::same_as<
                           Spec,
                           std::remove_cvref_t<decltype (trail_influence)>>)
        return m_materialized.trails;
      else if constexpr (std::same_as<
                           Spec,
                           std::remove_cvref_t<decltype (home_base_influence)>>)
        return m_materialized.home_base;
      else
        return false;
    }

  private:
    SurfaceSections& mutable_sections ();

    struct MaterializedSections {
      bool channel_flux = false;
      bool moisture = false;
      bool waterline = false;
      bool geology = false;
      bool tree_habitat = false;
      bool forest_cover = false;
      bool trails = false;
      bool home_base = false;
    } m_materialized;

    std::optional<SurfaceSections> m_sections;
  };
}

#endif
