#include <moppe/game/generated_world.hh>

#include <moppe/profile.hh>
#include <moppe/terrain/moisture.hh>
#include <moppe/terrain/river.hh>
#include <moppe/terrain/waterline.hh>

#include <utility>

namespace moppe::game {
  WorldParams bind_world_params (WorldParams params,
                                 const terrain::WorldRecipe& recipe) {
    params.map_size = recipe.extent ();
    params.resolution = recipe.resolution ();
    params.water_level = recipe.water_datum ();
    return params;
  }

  Hydrology analyze_hydrology (const map::SurfaceGeometry& geometry,
                               const terrain::WorldRecipe& recipe,
                               const HydrologyProgress& progress) {
    MOPPE_PROFILE_ZONE ("game::analyze_hydrology");
    const auto report = [&progress] (HydrologyStage stage) {
      if (progress)
        progress (stage);
    };

    report (HydrologyStage::StandingWater);
    terrain::FloodField standing_water = terrain::analyze_standing_water (
      geometry, (recipe.water_datum ()).numerical_value_in (moppe::u::m));

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

    return Hydrology (std::move (standing_water),
                      std::move (lakes),
                      std::move (drainage),
                      std::move (channels),
                      std::move (rivers));
  }

  std::tuple<terrain::WaterSheets, map::SurfaceReadings>
  analyze_surface (const map::SurfaceGeometry& geometry,
                   const terrain::WorldRecipe& recipe,
                   const Hydrology& hydrology,
                   const terrain::TrailUseMap& use) {
    MOPPE_PROFILE_ZONE ("game::analyze_surface");
    const auto& [standing_water, lakes, drainage, channels, rivers] = hydrology;
    const meters_t water_level = recipe.water_datum ();

    map::ChannelFluxMap channel_flux =
      map::analyze_channel_flux (geometry.domain (), channels);

    terrain::WaterSheets sheets = terrain::paint_watercourses (
      geometry, standing_water, lakes, drainage, rivers);
    const terrain::Waterline waterline =
      terrain::extract_waterline (geometry, sheets, lakes.membership ());

    terrain::MoistureMap moisture =
      terrain::analyze_moisture (standing_water, lakes.membership (), drainage);
    map::TreeHabitatMap habitat = map::analyze_tree_habitat (
      geometry, moisture, water_level, water_level + 145.0f * u::m);
    map::ForestCoverMap cover = map::analyze_forest_cover (
      habitat, use, recipe.seed ().value ^ 0x6f12ad37U);

    // The join names the readings in the order the bundle declares them; a
    // world is finished when every one of them is present.
    return { std::move (sheets),
             spatial::join (std::move (channel_flux),
                            std::move (moisture),
                            terrain::waterline_proximity (waterline),
                            map::analyze_geology_materials (geometry),
                            std::move (habitat),
                            std::move (cover),
                            use) };
  }

  GeneratedWorld::GeneratedWorld (WorldParams params,
                                  terrain::WorldRecipe recipe,
                                  map::SurfaceGeometry surface,
                                  Hydrology hydrology,
                                  terrain::WaterSheets water,
                                  terrain::TrailNetwork trails,
                                  map::SurfaceReadings readings)
      : m_params (bind_world_params (params, recipe)),
        m_recipe (std::move (recipe)), m_surface (std::move (surface)),
        m_hydrology (std::move (hydrology)),
        m_water_surface (std::move (water)), m_trails (std::move (trails)),
        m_readings (std::move (readings)) {}
}
