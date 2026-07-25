#ifndef MOPPE_GAME_GENERATED_WORLD_HH
#define MOPPE_GAME_GENERATED_WORLD_HH

#include <moppe/game/world.hh>
#include <moppe/map/surface.hh>
#include <moppe/map/surface_readings.hh>
#include <moppe/terrain/flood.hh>
#include <moppe/terrain/fractional_drainage.hh>
#include <moppe/terrain/trail.hh>
#include <moppe/terrain/watercourse.hh>
#include <moppe/terrain/world_recipe.hh>

#include <functional>
#include <optional>
#include <tuple>

namespace moppe::game {
  // A stable home for a generated world's durable, renderer-free artifacts.
  // The loading worker constructs one by calling the three build steps in
  // order (rebuild_surface, analyze_hydrology, derive_surface_readings) after
  // evaluating the terrain. Each step's result stays absent until it runs;
  // every one of them shares the surface geometry's domain.
  class GeneratedWorld {
  public:
    enum class HydrologyStage {
      StandingWater,
      Lakes,
      Drainage,
      Channels,
      Rivers,
    };

    using HydrologyProgress = std::function<void (HydrologyStage)>;

    // What analyzing a world's water leaves behind, in the order the stages
    // derive it: each stage reads the results before it.
    using Hydrology = std::tuple<terrain::FloodField,
                                 terrain::LakeCensus,
                                 terrain::DrainageGraph,
                                 terrain::FractionalDrainage,
                                 terrain::RiverNetwork>;

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

    const map::Surface& surface () const noexcept {
      return m_surface;
    }

    map::Surface& surface () noexcept {
      return m_surface;
    }

    const std::optional<Hydrology>& hydrology () const noexcept {
      return m_hydrology;
    }

    const std::optional<map::SurfaceReadings>& readings () const noexcept {
      return m_readings;
    }

    const std::optional<terrain::WaterSheets>& water_surface () const noexcept {
      return m_water_surface;
    }

    const std::optional<terrain::TrailNetwork>& trails () const noexcept {
      return m_trails;
    }

    // The three build steps, in the order the loading worker runs them.
    void rebuild_surface ();
    void analyze_hydrology (const HydrologyProgress& progress = {});
    void derive_surface_readings (
      std::optional<terrain::TrailNetwork> generated_trails = {});

  private:
    WorldParams m_params;
    terrain::WorldRecipe m_recipe;
    map::Surface m_surface;
    std::optional<map::SurfaceReadings> m_readings;
    std::optional<Hydrology> m_hydrology;
    std::optional<terrain::WaterSheets> m_water_surface;
    std::optional<terrain::TrailNetwork> m_trails;
  };
}

#endif
