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
#include <tuple>

namespace moppe::game {
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

  // The recipe is authoritative for a world's extent, resolution, and datum,
  // both before it is generated and after.
  WorldParams bind_world_params (WorldParams params,
                                 const terrain::WorldRecipe& recipe);

  Hydrology analyze_hydrology (const map::SurfaceGeometry& geometry,
                               const terrain::WorldRecipe& recipe,
                               const HydrologyProgress& progress = {});

  // Painted water and the readings over the ground arrive together: the
  // waterline the readings measure against comes from the sheets.
  std::tuple<terrain::WaterSheets, map::SurfaceReadings>
  analyze_surface (const map::SurfaceGeometry& geometry,
                   const terrain::WorldRecipe& recipe,
                   const Hydrology& hydrology,
                   const terrain::TrailUseMap& use);

  // A generated world's durable, renderer-free artifacts. It is assembled
  // from finished parts, so holding one means the world is complete; the
  // build itself lives in world_loading.
  class GeneratedWorld {
  public:
    GeneratedWorld (WorldParams params,
                    terrain::WorldRecipe recipe,
                    map::Surface surface,
                    Hydrology hydrology,
                    terrain::WaterSheets water,
                    terrain::TrailNetwork trails,
                    map::SurfaceReadings readings);
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

    const Hydrology& hydrology () const noexcept {
      return m_hydrology;
    }

    const map::SurfaceReadings& readings () const noexcept {
      return m_readings;
    }

    const terrain::WaterSheets& water_surface () const noexcept {
      return m_water_surface;
    }

    const terrain::TrailNetwork& trails () const noexcept {
      return m_trails;
    }

  private:
    WorldParams m_params;
    terrain::WorldRecipe m_recipe;
    map::Surface m_surface;
    Hydrology m_hydrology;
    terrain::WaterSheets m_water_surface;
    terrain::TrailNetwork m_trails;
    map::SurfaceReadings m_readings;
  };
}

#endif
