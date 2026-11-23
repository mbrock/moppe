#ifndef MOPPE_GAME_GENERATED_WORLD_HH
#define MOPPE_GAME_GENERATED_WORLD_HH

#include <moppe/game/cover_grid.hh>
#include <moppe/game/forest_plan.hh>
#include <moppe/game/world.hh>
#include <moppe/map/surface.hh>
#include <moppe/terrain/flood.hh>
#include <moppe/terrain/roads.hh>
#include <moppe/terrain/trail.hh>
#include <moppe/terrain/water_view.hh>
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

  // Form the road bed and return its combined road and trail influence.
  // Run the full hydrology and surface analysis on the resulting ground.
  std::pair<terrain::RoadNetwork, terrain::TrailUseMap>
  form_world_roads (map::SurfaceGeometry& geometry,
                    const terrain::WorldRecipe& recipe,
                    const terrain::TrailNetwork& trails);

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

    const Hydrology& hydrology () const {
      return m_build.value ().hydrology;
    }

    const terrain::FloodField& standing_water () const {
      return std::get<terrain::FloodField> (hydrology ());
    }

    const terrain::LakeCensus& lake_census () const {
      return std::get<terrain::LakeCensus> (hydrology ());
    }

    std::span<const terrain::WaterBody> water_bodies () const {
      return m_build ? lake_census ().water_bodies ()
                     : std::span<const terrain::WaterBody> (m_runtime_bodies);
    }

    const terrain::DrainageGraph& drainage () const {
      return std::get<terrain::DrainageGraph> (hydrology ());
    }

    const terrain::RiverNetwork& rivers () const noexcept {
      return m_build ? std::get<terrain::RiverNetwork> (m_build->hydrology)
                     : m_runtime_rivers;
    }

    const map::SurfaceReadings& readings () const {
      return m_build.value ().readings;
    }

    const terrain::WaterSheets& water_surface () const {
      return m_build.value ().water;
    }

    terrain::WaterView water_view () const {
      return m_build ? terrain::WaterView (&m_build->water)
                     : terrain::WaterView (m_runtime_water.value ());
    }

    // Release the analysis after painting the map, uploading textures and
    // placing inhabitants. Sessions retain the elevation buffer in place.
    void enter_play ();

    template <auto Field>
    auto reading (const position_t& at) const {
      if (m_build) {
        if constexpr (Field == map::snow_support)
          return spatial::sample<Field> (m_surface, at);
        else
          return spatial::sample<Field> (m_build->readings, at);
      }
      return spatial::sample_field (m_surface.domain (), at, [&] (auto site) {
        const auto value = m_cover[PlayFields::spec_index<Field>].at (site);
        return (value / 255.f) * Field[one];
      });
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
    struct BuildFields {
      Hydrology hydrology;
      terrain::WaterSheets water;
      map::SurfaceReadings readings;
    };
    std::optional<BuildFields> m_build;
    terrain::RiverNetwork m_runtime_rivers;
    std::vector<terrain::WaterBody> m_runtime_bodies;
    std::optional<terrain::RuntimeWater> m_runtime_water;
    // The first four lanes feed wheel and snow physics. The other six
    // supply animals as they choose new pasture during play.
    using PlayFields = spatial::Bundle<terrain::TerrainDomain,
                                       map::TrailInfluence,
                                       map::ErosionExposure,
                                       map::SurfaceMoisture,
                                       map::SnowSupport,
                                       map::GrazingPressure,
                                       map::ForestCover,
                                       map::DwarfBirchAbundance,
                                       map::MountainBirchAbundance,
                                       map::LichenAbundance,
                                       map::SphagnumAbundance>;
    std::array<CoverGrid, PlayFields::column_count> m_cover;
    terrain::TrailNetwork m_trails;
    terrain::RoadNetwork m_roads;
    terrain::RoadSurface m_road_surface;
    ForestPlan m_forest;
  };
}

#endif
