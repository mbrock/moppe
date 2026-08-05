#ifndef MOPPE_TERRAIN_WORLD_RECIPE_HH
#define MOPPE_TERRAIN_WORLD_RECIPE_HH

#include <moppe/gfx/math.hh>
#include <moppe/quantities.hh>
#include <moppe/terrain/stream_power_evolution.hh>
#include <moppe/terrain/trail.hh>

#include <optional>
#include <string_view>

namespace moppe::terrain {
  enum class TerrainGenerationProfile { Smoke, Fast, Play, Research };

  std::string_view profile_id (TerrainGenerationProfile profile) noexcept;

  // The complete immutable input to construction of one physical world.
  class WorldRecipe {
  public:
    const spatial_extent_t& extent () const noexcept {
      return m_extent;
    }

    int resolution () const noexcept {
      return m_resolution;
    }

    Seed seed () const noexcept {
      return m_seed;
    }

    meters_t water_datum () const noexcept {
      return m_water_datum;
    }

    TerrainGenerationProfile generation_profile () const noexcept {
      return m_generation_profile;
    }

    const StreamPowerEvolution& evolution () const noexcept {
      return m_evolution;
    }

    const TrailFormation& trail_formation () const noexcept {
      return m_trail_formation;
    }

  private:
    friend WorldRecipe make_world_recipe (
      spatial_extent_t extent,
      int resolution,
      Seed seed,
      meters_t water_datum,
      TerrainGenerationProfile generation_profile,
      std::optional<julian_years_t> uplift_duration,
      std::optional<square_meters_t> channel_initiation_area,
      std::optional<SedimentConcentration> sediment_concentration);
    WorldRecipe (spatial_extent_t extent,
                 int resolution,
                 Seed seed,
                 meters_t water_datum,
                 TerrainGenerationProfile generation_profile,
                 std::optional<julian_years_t> uplift_duration,
                 std::optional<square_meters_t> channel_initiation_area,
                 std::optional<SedimentConcentration> sediment_concentration);

    spatial_extent_t m_extent;
    int m_resolution;
    Seed m_seed;
    meters_t m_water_datum;
    TerrainGenerationProfile m_generation_profile;
    StreamPowerEvolution m_evolution;
    TrailFormation m_trail_formation;
  };

  WorldRecipe make_world_recipe (
    spatial_extent_t extent,
    int resolution,
    Seed seed,
    meters_t water_datum,
    TerrainGenerationProfile generation_profile,
    std::optional<julian_years_t> uplift_duration = std::nullopt,
    std::optional<square_meters_t> channel_initiation_area = std::nullopt,
    std::optional<SedimentConcentration> sediment_concentration = std::nullopt);
}

#endif
