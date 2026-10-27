#ifndef MOPPE_GAME_GENERATED_WORLD_HH
#define MOPPE_GAME_GENERATED_WORLD_HH

#include <moppe/game/forest_plan.hh>
#include <moppe/game/world.hh>
#include <moppe/map/surface.hh>
#include <moppe/terrain/flood.hh>
#include <moppe/terrain/roads.hh>
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
  // derive it.
  using Hydrology = std::tuple<terrain::FloodField,
                               terrain::LakeCensus,
                               terrain::DrainageGraph,
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

  // The roads, footpaths, and places of a world (terrain/roads.hh), planned
  // over its finished ground: they go round the forest and the mires the
  // readings find, and begin at the trail's home base.
  terrain::RoadNetwork plan_world_roads (const map::SurfaceGeometry& geometry,
                                         const terrain::WorldRecipe& recipe,
                                         const Hydrology& hydrology,
                                         const terrain::TrailNetwork& trails,
                                         const map::SurfaceReadings& readings);

  // Plans the roads and builds them into the ground, then reads the water
  // and the surface again over the built ground, the roads' treads joining
  // the trail's in the readings (so the forest stands back from them and
  // the ground is packed dirt on them). Everything a world is assembled
  // from passes through it.
  terrain::RoadNetwork lay_world_roads (map::SurfaceGeometry& geometry,
                                        const terrain::WorldRecipe& recipe,
                                        Hydrology& hydrology,
                                        const terrain::TrailNetwork& trails,
                                        terrain::WaterSheets& water,
                                        map::SurfaceReadings& readings);

  // What has happened to the ground since (game/disturbance.hh): the fires,
  // the pastures of the places the roads join, and the roads' verges,
  // written into READINGS -- which analyze_surface leaves undisturbed --
  // with the vegetation and the forest cover read again over them.
  void disturb_surface (const map::SurfaceGeometry& geometry,
                        const terrain::WorldRecipe& recipe,
                        const terrain::WaterSheets& water,
                        const terrain::RoadNetwork& roads,
                        map::SurfaceReadings& readings);

  // A generated world's durable, renderer-free artifacts. It is assembled
  // from finished parts, so holding one means the world is complete; the
  // build itself lives in world_loading.
  class GeneratedWorld {
  public:
    GeneratedWorld (WorldParams params,
                    terrain::WorldRecipe recipe,
                    map::SurfaceGeometry surface,
                    Hydrology hydrology,
                    terrain::WaterSheets water,
                    terrain::TrailNetwork trails,
                    map::SurfaceReadings readings,
                    terrain::RoadNetwork roads,
                    ForestPlan forest);
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

    const map::SurfaceGeometry& surface () const noexcept {
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

    const terrain::RoadNetwork& roads () const noexcept {
      return m_roads;
    }

    // The built roads exactly, for the renderer and for footing.
    const terrain::RoadSurface& road_surface () const noexcept {
      return m_road_surface;
    }

    const ForestPlan& forest () const noexcept {
      return m_forest;
    }

  private:
    WorldParams m_params;
    terrain::WorldRecipe m_recipe;
    map::SurfaceGeometry m_surface;
    Hydrology m_hydrology;
    terrain::WaterSheets m_water_surface;
    terrain::TrailNetwork m_trails;
    map::SurfaceReadings m_readings;
    terrain::RoadNetwork m_roads;
    terrain::RoadSurface m_road_surface;
    ForestPlan m_forest;
  };
}

#endif
