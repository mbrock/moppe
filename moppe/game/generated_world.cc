#include <moppe/game/generated_world.hh>

#include <moppe/profile.hh>
#include <moppe/terrain/moisture.hh>
#include <moppe/terrain/river.hh>
#include <moppe/terrain/waterline.hh>

#include <cmath>
#include <span>
#include <stdexcept>
#include <utility>
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
    m_readings.reset ();
    m_surface.rebuild_geometry ();
  }

  void GeneratedWorld::analyze_hydrology (const HydrologyProgress& progress) {
    MOPPE_PROFILE_ZONE ("GeneratedWorld::analyze_hydrology");
    const auto report = [&progress] (HydrologyStage stage) {
      if (progress)
        progress (stage);
    };

    report (HydrologyStage::StandingWater);
    terrain::FloodField standing_water = terrain::analyze_standing_water (
      m_surface.geometry (), meters_value (m_recipe.water_datum ()));

    report (HydrologyStage::Lakes);
    terrain::LakeCensus lakes = terrain::census_lakes (standing_water);

    report (HydrologyStage::Drainage);
    terrain::DrainageGraph drainage =
      terrain::analyze_wet_drainage (standing_water, lakes);

    report (HydrologyStage::Channels);
    terrain::FractionalDrainage channels =
      terrain::analyze_fractional_drainage (standing_water, lakes);

    report (HydrologyStage::Rivers);
    terrain::RiverNetwork rivers = terrain::extract_river_network (
      standing_water,
      lakes,
      drainage,
      channels,
      terrain::visible_river_minimum_area (drainage.domain ()));

    m_hydrology.emplace (std::move (standing_water),
                         std::move (lakes),
                         std::move (drainage),
                         std::move (channels),
                         std::move (rivers));
  }

  void GeneratedWorld::derive_surface_readings (
    std::optional<terrain::TrailNetwork> generated_trails) {
    MOPPE_PROFILE_ZONE ("GeneratedWorld::derive_surface_readings");
    m_readings.reset ();
    m_water_surface.reset ();
    m_trails.reset ();
    if (!m_hydrology)
      throw std::logic_error ("Surface readings need the world's hydrology");

    const auto& [standing_water, lakes, drainage, channels, rivers] =
      *m_hydrology;
    const map::SurfaceGeometry& geometry = m_surface.geometry ();

    map::ChannelFluxMap channel_flux =
      map::analyze_channel_flux (geometry.domain (), channels);

    terrain::WaterSheets sheets = terrain::paint_watercourses (
      geometry, standing_water, lakes, drainage, rivers);
    const terrain::Waterline waterline =
      terrain::extract_waterline (geometry, sheets, lakes);
    m_water_surface.emplace (std::move (sheets));

    terrain::MoistureMap moisture =
      terrain::analyze_moisture (standing_water, lakes, drainage);
    map::TreeHabitatMap habitat =
      map::analyze_tree_habitat (geometry,
                                 moisture,
                                 m_params.water_level,
                                 m_params.water_level + 145.0f * u::m);

    {
      MOPPE_PROFILE_ZONE ("world.derive_trails");
      if (generated_trails)
        m_trails = std::move (generated_trails);
      else
        m_trails = terrain::analyze_trail_network (geometry,
                                                   m_recipe.trail_formation ());
    }

    map::ForestCoverMap cover = map::analyze_forest_cover (
      habitat, m_trails->use, m_recipe.seed ().value ^ 0x6f12ad37U);

    // The join names the readings in the order the bundle declares them; a
    // world is finished when every one of them is present.
    m_readings.emplace (
      spatial::join (std::move (channel_flux),
                     std::move (moisture),
                     terrain::waterline_proximity (waterline),
                     map::analyze_geology_materials (geometry),
                     std::move (habitat),
                     std::move (cover),
                     m_trails->use));
  }
}
