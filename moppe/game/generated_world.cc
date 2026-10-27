#include <moppe/game/generated_world.hh>

#include <moppe/game/disturbance.hh>
#include <moppe/profile.hh>
#include <moppe/terrain/moisture.hh>
#include <moppe/terrain/readings.hh>
#include <moppe/terrain/river.hh>
#include <moppe/terrain/waterline.hh>

#include <algorithm>
#include <array>
#include <cmath>
#include <iostream>
#include <span>
#include <tuple>
#include <type_traits>
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
    terrain::RiverNetwork rivers =
      terrain::extract_river_network (standing_water,
                                      lakes,
                                      drainage,
                                      channels,
                                      terrain::visible_river_minimum_area ());

    return Hydrology (std::move (standing_water),
                      std::move (lakes),
                      std::move (drainage),
                      std::move (rivers));
  }

  namespace {
    using spatial::get;

    std::uint32_t vegetation_seed (const terrain::WorldRecipe& recipe) {
      return recipe.seed ().value ^ 0x3b10e5a1U;
    }

    std::uint32_t cover_seed (const terrain::WorldRecipe& recipe) {
      return recipe.seed ().value ^ 0x6f12ad37U;
    }

    // A narrow bundle's columns copied out of a wide one, and back.
    template <typename Narrow, typename Wide>
    Narrow project (const Wide& wide) {
      Narrow narrow (wide.domain ());
      [&]<typename Domain, typename... Quantities> (
        std::type_identity<spatial::Bundle<Domain, Quantities...>>) {
        ((get<Quantities::quantity_spec> (narrow) =
            get<Quantities::quantity_spec> (wide)),
         ...);
      }(std::type_identity<Narrow> {});
      return narrow;
    }

    template <typename Wide, typename Narrow>
    void overwrite (Wide& wide, const Narrow& narrow) {
      [&]<typename Domain, typename... Quantities> (
        std::type_identity<spatial::Bundle<Domain, Quantities...>>) {
        ((get<Quantities::quantity_spec> (wide) =
            get<Quantities::quantity_spec> (narrow)),
         ...);
      }(std::type_identity<Narrow> {});
    }

    // How much of the dry ground each species holds the most of, as a
    // reading of the world's handwriting.
    void log_vegetation (const map::VegetationMap& vegetation,
                         const terrain::WaterSheets& water,
                         const map::SurfaceGeometry& geometry) {
      const auto& ground = spatial::get<terrain::surface_elevation> (geometry);
      const auto& level = spatial::get<terrain::surface_elevation> (water);
      const auto column = [&] (auto quantity) {
        return std::span (spatial::get<quantity> (vegetation));
      };
      const auto trees = std::tuple (column (map::spruce_abundance),
                                     column (map::pine_abundance),
                                     column (map::birch_abundance),
                                     column (map::mountain_birch_abundance));
      const auto field = std::tuple (column (map::lichen_abundance),
                                     column (map::heather_abundance),
                                     column (map::bilberry_abundance),
                                     column (map::sphagnum_abundance),
                                     column (map::dwarf_birch_abundance),
                                     column (map::sedge_abundance),
                                     column (map::alvar_abundance),
                                     column (map::fireweed_abundance));
      const auto tally = [&] (const auto& columns, const char* const* names) {
        constexpr std::size_t n =
          std::tuple_size_v<std::remove_cvref_t<decltype (columns)>>;
        std::array<std::size_t, n + 1> leads {};
        std::size_t dry = 0;
        for (std::size_t i = 0; i < ground.size (); ++i) {
          if (level[i] > ground[i])
            continue;
          ++dry;
          std::size_t best = n;
          float most = 0.15f;
          std::size_t s = 0;
          std::apply (
            [&] (const auto&... each) {
              ((each[i].numerical_value_in (mp_units::one) > most
                  ? (most = each[i].numerical_value_in (mp_units::one),
                     best = s)
                  : 0,
                ++s),
               ...);
            },
            columns);
          ++leads[best];
        }
        for (std::size_t s = 0; s <= n; ++s)
          std::cerr << ' ' << names[s] << ' '
                    << std::lround (100.0 * leads[s] /
                                    std::max<std::size_t> (dry, 1))
                    << '%';
      };
      static const char* const tree_names[] = {
        "spruce", "pine", "birch", "fell-birch", "treeless"
      };
      static const char* const field_names[] = {
        "lichen", "heather", "bilberry", "sphagnum", "dwarf-birch",
        "sedge",  "alvar",   "fireweed", "grass"
      };
      std::cerr << "moppe: vegetation: trees";
      tally (trees, tree_names);
      std::cerr << "; field layer";
      tally (field, field_names);
      std::cerr << std::endl;
    }
  }

  std::tuple<terrain::WaterSheets, map::SurfaceReadings>
  analyze_surface (const map::SurfaceGeometry& geometry,
                   const terrain::WorldRecipe& recipe,
                   const Hydrology& hydrology,
                   const terrain::TrailUseMap& use) {
    MOPPE_PROFILE_ZONE ("game::analyze_surface");
    const auto& [standing_water, lakes, drainage, rivers] = hydrology;
    const meters_t water_level = recipe.water_datum ();

    terrain::WaterSheets sheets = terrain::paint_watercourses (
      geometry, standing_water, lakes, drainage, rivers);
    const terrain::Waterline waterline =
      terrain::extract_waterline (geometry, sheets, lakes.membership ());

    terrain::MoistureMap moisture =
      terrain::analyze_moisture (standing_water, lakes.membership (), drainage);
    // Where the forest stops is a fraction of this world's own relief, not a
    // fixed climb from the sea. A flat island and an alpine world both want a
    // tree line, and they do not want the same one in metres. This fraction
    // sits just under where the terrain shader begins to hold snow, so the
    // last trees thin out into the snowfields instead of ending under them.
    constexpr float tree_line_share_of_relief = 0.62f;
    const meters_t land_relief =
      terrain::measure_height_range (geometry).maximum * u::m - water_level;
    // A near-flat world (the grass laboratory's rolling plain, or any
    // low-forcing recipe) still deserves a terrestrial habitat band: clamp
    // the tree line above the analysis floor instead of refusing to finish
    // the world. Everything on such a plain simply lies below the trees'
    // altitude limit, which is the truthful reading.
    const meters_t tree_line =
      std::max (water_level + tree_line_share_of_relief * land_relief,
                water_level + 21.0f * u::m);
    map::TreeHabitatMap habitat =
      map::analyze_tree_habitat (geometry, moisture, water_level, tree_line);
    map::DisturbanceMap disturbance = map::undisturbed (geometry.domain ());
    map::VegetationMap vegetation =
      map::analyze_vegetation (geometry,
                               get<terrain::soil_wetness> (moisture),
                               water_level,
                               water_level + land_relief,
                               vegetation_seed (recipe),
                               disturbance);
    map::ForestCoverMap cover =
      map::analyze_forest_cover (habitat, use, vegetation, cover_seed (recipe));

    // The join names the readings in the order the bundle declares them; a
    // world is finished when every one of them is present.
    return { std::move (sheets),
             spatial::join (std::move (moisture),
                            terrain::waterline_proximity (waterline),
                            map::analyze_geology_materials (geometry),
                            std::move (habitat),
                            std::move (cover),
                            use,
                            std::move (vegetation),
                            std::move (disturbance)) };
  }

  terrain::RoadNetwork plan_world_roads (const map::SurfaceGeometry& geometry,
                                         const terrain::WorldRecipe& recipe,
                                         const Hydrology& hydrology,
                                         const terrain::TrailNetwork& trails,
                                         const map::SurfaceReadings& readings) {
    const auto& [standing_water, lakes, drainage, rivers] = hydrology;
    const auto numbers = [] (const auto& column) {
      using Quantity = std::ranges::range_value_t<decltype (column)>;
      static_assert (sizeof (Quantity) == sizeof (float));
      return std::span<const float> (
        reinterpret_cast<const float*> (column.data ()), column.size ());
    };
    return terrain::plan_roads (
      geometry.domain (),
      spatial::get<terrain::surface_elevation> (geometry),
      standing_water,
      drainage,
      { .forest_cover = numbers (spatial::get<map::forest_cover> (readings)),
        .mire = numbers (spatial::get<map::sphagnum_abundance> (readings)),
        .wetness = numbers (spatial::get<terrain::soil_wetness> (readings)) },
      rivers.minimum_area,
      trails.plan.home_base,
      recipe.water_datum ().numerical_value_in (u::m),
      recipe.seed ().value ^ 0x5d1c0a7bU);
  }

  terrain::RoadNetwork lay_world_roads (map::SurfaceGeometry& geometry,
                                        const terrain::WorldRecipe& recipe,
                                        Hydrology& hydrology,
                                        const terrain::TrailNetwork& trails,
                                        terrain::WaterSheets& water,
                                        map::SurfaceReadings& readings) {
    MOPPE_PROFILE_ZONE ("game::lay_world_roads");
    terrain::RoadNetwork roads =
      plan_world_roads (geometry, recipe, hydrology, trails, readings);
    const terrain::RoadFormationResult built =
      map::form_terrain_roads (geometry,
                               roads,
                               std::get<terrain::FloodField> (hydrology),
                               std::get<terrain::DrainageGraph> (hydrology));
    std::cerr << "moppe: roads: " << roads.places.size () << " places, "
              << roads.roads.size () << " ways, " << built.cut_m3 << " m3 cut, "
              << built.fill_m3 << " m3 filled, " << built.steep_points << " of "
              << built.points << " points steeper than the road's grade"
              << std::endl;
    map::rebuild_geometry (geometry);
    hydrology = analyze_hydrology (geometry, recipe);
    terrain::TrailUseMap use = trails.use;
    auto& tread = spatial::get<map::trail_influence> (use);
    for (std::size_t cell = 0; cell < tread.size (); ++cell)
      tread[cell] = std::max (
        tread[cell], built.influence[cell] * map::trail_influence[one]);
    std::tie (water, readings) =
      analyze_surface (geometry, recipe, hydrology, use);
    return roads;
  }

  void disturb_surface (const map::SurfaceGeometry& geometry,
                        const terrain::WorldRecipe& recipe,
                        const terrain::WaterSheets& water,
                        const terrain::RoadNetwork& roads,
                        map::SurfaceReadings& readings) {
    MOPPE_PROFILE_ZONE ("game::disturb_surface");
    map::DisturbanceMap disturbance = plan_disturbance (
      geometry, readings, water, roads, recipe.seed ().value ^ 0x7d15ba7eU);
    const meters_t land_top =
      terrain::measure_height_range (geometry).maximum * u::m;
    map::VegetationMap vegetation =
      map::analyze_vegetation (geometry,
                               get<terrain::soil_wetness> (readings),
                               recipe.water_datum (),
                               land_top,
                               vegetation_seed (recipe),
                               disturbance);
    log_vegetation (vegetation, water, geometry);
    map::ForestCoverMap cover =
      map::analyze_forest_cover (project<map::TreeHabitatMap> (readings),
                                 project<terrain::TrailUseMap> (readings),
                                 vegetation,
                                 cover_seed (recipe));
    overwrite (readings, vegetation);
    overwrite (readings, cover);
    overwrite (readings, disturbance);
  }

  GeneratedWorld::GeneratedWorld (WorldParams params,
                                  terrain::WorldRecipe recipe,
                                  map::SurfaceGeometry surface,
                                  Hydrology hydrology,
                                  terrain::WaterSheets water,
                                  terrain::TrailNetwork trails,
                                  map::SurfaceReadings readings,
                                  terrain::RoadNetwork roads,
                                  ForestPlan forest)
      : m_params (bind_world_params (params, recipe)),
        m_recipe (std::move (recipe)), m_surface (std::move (surface)),
        m_hydrology (std::move (hydrology)),
        m_water_surface (std::move (water)), m_trails (std::move (trails)),
        m_readings (std::move (readings)), m_roads (std::move (roads)),
        m_forest (std::move (forest)) {}
}
