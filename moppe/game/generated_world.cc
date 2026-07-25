#include <moppe/game/generated_world.hh>

#include <moppe/profile.hh>
#include <moppe/terrain/moisture.hh>
#include <moppe/terrain/river.hh>
#include <moppe/terrain/waterline.hh>

#include <algorithm>
#include <cmath>
#include <span>
#include <stdexcept>
#include <utility>
#include <variant>
#include <vector>

namespace moppe::game {
  namespace {
    void bind_world_params (WorldParams& params,
                            const terrain::WorldRecipe& recipe) {
      params.map_size = recipe.extent ();
      params.resolution = recipe.resolution ();
      params.water_level = recipe.water_datum ();
    }

  }

  GeneratedWorld::Hydrology::Hydrology (terrain::FloodField standing_water,
                                        terrain::LakeCensus lakes,
                                        terrain::DrainageGraph drainage,
                                        terrain::FractionalDrainage channels,
                                        terrain::WaterNetwork waterways,
                                        terrain::RiverNetwork rivers)
      : m_standing_water (std::move (standing_water)),
        m_lakes (std::move (lakes)), m_drainage (std::move (drainage)),
        m_channels (std::move (channels)), m_waterways (std::move (waterways)),
        m_rivers (std::move (rivers)) {}

  GeneratedWorld::GeneratedWorld (WorldParams params,
                                  terrain::WorldRecipe recipe)
      : m_params (params), m_recipe (std::move (recipe)),
        m_surface (m_recipe.resolution (),
                   m_recipe.resolution (),
                   extent_value (m_recipe.extent ())) {
    bind_world_params (m_params, m_recipe);
  }

  void GeneratedWorld::rebuild_surface () {
    MOPPE_PROFILE_ZONE ("GeneratedWorld::rebuild_surface");
    m_surface.rebuild_geometry_readings ();
  }

  void GeneratedWorld::analyze_hydrology (const HydrologyProgress& progress) {
    MOPPE_PROFILE_ZONE ("GeneratedWorld::analyze_hydrology");
    const auto report = [&progress] (HydrologyStage stage) {
      if (progress)
        progress (stage);
    };

    report (HydrologyStage::StandingWater);
    terrain::FloodField standing_water = terrain::analyze_standing_water (
      m_surface.terrain_view (), meters_value (m_recipe.water_datum ()));

    report (HydrologyStage::Lakes);
    terrain::LakeCensus lakes = terrain::census_lakes (standing_water);

    report (HydrologyStage::Drainage);
    terrain::DrainageGraph drainage = terrain::analyze_wet_drainage (
      m_surface.terrain_view (), standing_water, lakes);

    report (HydrologyStage::Waterways);
    terrain::WaterNetwork waterways =
      terrain::analyze_water_network (standing_water, lakes, drainage);

    report (HydrologyStage::Channels);
    terrain::FractionalDrainage channels =
      terrain::analyze_fractional_drainage (
        m_surface.terrain_view (), standing_water, lakes);

    report (HydrologyStage::Rivers);
    terrain::RiverNetwork rivers = terrain::extract_river_network (
      standing_water,
      lakes,
      drainage,
      channels,
      terrain::visible_river_minimum_area (drainage.domain));

    m_hydrology.emplace (Hydrology (std::move (standing_water),
                                    std::move (lakes),
                                    std::move (drainage),
                                    std::move (channels),
                                    std::move (waterways),
                                    std::move (rivers)));
  }

  void GeneratedWorld::materialize_analyses (
    std::optional<terrain::TrailNetwork> generated_trails) {
    MOPPE_PROFILE_ZONE ("GeneratedWorld::materialize_analyses");
    m_water_surface.reset ();
    m_trails.reset ();

    if (m_hydrology) {
      const Hydrology& hydrology = *m_hydrology;
      m_surface.materialize_channel_flux (hydrology.channels ());

      const terrain::WaterSheets sheets =
        terrain::paint_watercourses (m_surface.terrain_view (),
                                     hydrology.standing_water (),
                                     hydrology.lakes (),
                                     hydrology.drainage (),
                                     hydrology.rivers ());
      m_water_surface.emplace (m_surface.atlas ().domain (), sheets);

      const terrain::Waterline waterline = terrain::extract_waterline (
        m_surface.terrain_view (), sheets.surface, hydrology.lakes ());
      m_surface.materialize_waterline_distance (
        terrain::waterline_proximity (waterline));

      m_surface.materialize_moisture (
        terrain::analyze_moisture (hydrology.standing_water (),
                                   hydrology.lakes (),
                                   hydrology.drainage ()));
      m_surface.derive_tree_habitat (m_params.water_level,
                                     m_params.water_level + 145.0f * u::m);

      const terrain::TerrainProgram& program = m_recipe.terrain_program ();
      const auto stage = std::find_if (
        program.transforms.begin (),
        program.transforms.end (),
        [] (const terrain::TerrainTransform& transform) {
          return std::holds_alternative<terrain::TrailFormation> (transform);
        });
      if (stage != program.transforms.end ()) {
        MOPPE_PROFILE_ZONE ("world.materialize_trails");
        if (generated_trails)
          m_trails = std::move (generated_trails);
        else
          m_trails = terrain::analyze_trail_network (
            m_surface.terrain_view (),
            std::get<terrain::TrailFormation> (*stage));
        m_surface.materialize_trail_influence (m_trails->influence.values ());
        m_surface.materialize_home_base_influence (
          m_trails->home_base_influence.values ());
      }
    }

    if (m_surface.atlas ().ecology ().tree_habitat ())
      m_surface.derive_forest_cover (m_recipe.seed ().value ^ 0x6f12ad37U);

    m_surface.derive_geology_materials ();
  }
}
