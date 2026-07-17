#ifndef MOPPE_GAME_GENERATED_WORLD_HH
#define MOPPE_GAME_GENERATED_WORLD_HH

#include <moppe/game/world.hh>
#include <moppe/map/generate.hh>
#include <moppe/map/surface.hh>
#include <moppe/map/water_surface.hh>
#include <moppe/terrain/flood.hh>
#include <moppe/terrain/fractional_drainage.hh>
#include <moppe/terrain/trail.hh>
#include <moppe/terrain/watercourse.hh>
#include <moppe/terrain/world_recipe.hh>

#include <functional>
#include <optional>
#include <vector>

namespace moppe::game {
  // A stable home for a generated world's durable, renderer-free artifacts.
  // Gameplay receives only const readings.  Construction has an explicit
  // Builder capability, and Terrain Lab is the one named mutable borrower:
  // it restores the map before returning control to ordinary gameplay.
  class GeneratedWorld {
  public:
    enum class HydrologyStage {
      StandingWater,
      Lakes,
      Drainage,
      Waterways,
      Channels,
      Rivers,
    };

    using HydrologyProgress = std::function<void (HydrologyStage)>;

    class Hydrology {
    public:
      const terrain::FloodField& standing_water () const noexcept {
        return m_standing_water;
      }

      const terrain::LakeCensus& lakes () const noexcept {
        return m_lakes;
      }

      const terrain::DrainageGraph& drainage () const noexcept {
        return m_drainage;
      }

      const terrain::FractionalDrainage& channels () const noexcept {
        return m_channels;
      }

      const terrain::WaterNetwork& waterways () const noexcept {
        return m_waterways;
      }

      const terrain::RiverNetwork& rivers () const noexcept {
        return m_rivers;
      }

    private:
      friend class GeneratedWorld;

      Hydrology (terrain::FloodField standing_water,
                 terrain::LakeCensus lakes,
                 terrain::DrainageGraph drainage,
                 terrain::FractionalDrainage channels,
                 terrain::WaterNetwork waterways,
                 terrain::RiverNetwork rivers);

      terrain::FloodField m_standing_water;
      terrain::LakeCensus m_lakes;
      terrain::DrainageGraph m_drainage;
      terrain::FractionalDrainage m_channels;
      terrain::WaterNetwork m_waterways;
      terrain::RiverNetwork m_rivers;
    };

    class Builder;

    GeneratedWorld (WorldParams params, terrain::WorldRecipe recipe);
    GeneratedWorld (const GeneratedWorld&) = delete;
    GeneratedWorld& operator= (const GeneratedWorld&) = delete;
    GeneratedWorld (GeneratedWorld&&) = delete;
    GeneratedWorld& operator= (GeneratedWorld&&) = delete;

    const WorldParams& params () const noexcept {
      return m_params;
    }

    const terrain::WorldRecipe& recipe () const noexcept {
      return m_recipe;
    }

    const map::RandomHeightMap& terrain () const noexcept {
      return m_terrain;
    }

    const map::Surface& surface () const noexcept {
      return m_surface;
    }

    const std::vector<std::vector<float>>& terrain_history () const noexcept {
      return m_terrain_history;
    }

    const std::optional<Hydrology>& hydrology () const noexcept {
      return m_hydrology;
    }

    const std::optional<map::WaterSurface>& water_surface () const noexcept {
      return m_water_surface;
    }

    const std::optional<terrain::TrailNetwork>& trails () const noexcept {
      return m_trails;
    }

    // Terrain Lab's begin/leave transaction is the only ordinary runtime
    // mutation of a completed world's height field.
    map::RandomHeightMap& terrain_for_terrain_lab () noexcept {
      return m_terrain;
    }

    Builder build () noexcept;

  private:
    friend class Builder;

    void reset (terrain::WorldRecipe recipe);
    void rebuild_surface ();
    void analyze_hydrology (const HydrologyProgress& progress);
    void materialize_analyses (
      std::optional<terrain::TrailNetwork> generated_trails);

    WorldParams m_params;
    terrain::WorldRecipe m_recipe;
    map::RandomHeightMap m_terrain;
    map::Surface m_surface;
    std::vector<std::vector<float>> m_terrain_history;
    std::optional<Hydrology> m_hydrology;
    std::optional<map::WaterSurface> m_water_surface;
    std::optional<terrain::TrailNetwork> m_trails;
  };

  // Builder is a short-lived construction capability.  It is intentionally
  // the sole mutable route for generation and completion; the public world
  // surface remains a read-only value to gameplay consumers.
  class GeneratedWorld::Builder {
  public:
    map::RandomHeightMap& terrain () noexcept;
    std::vector<std::vector<float>>& terrain_history () noexcept;

    void reset (terrain::WorldRecipe recipe);
    void rebuild_surface ();
    void analyze_hydrology (const HydrologyProgress& progress = {});
    void materialize_analyses (
      std::optional<terrain::TrailNetwork> generated_trails = {});

  private:
    friend class GeneratedWorld;

    explicit Builder (GeneratedWorld& world) : m_world (world) {}

    GeneratedWorld& m_world;
  };
}

#endif
