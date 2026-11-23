#include <moppe/environment.hh>
#include <moppe/game/generated_world.hh>
#include <moppe/parallel.hh>

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

  static auto drain_surface (const map::SurfaceGeometry& geometry,
                             const terrain::WorldRecipe& recipe,
                             const HydrologyProgress& progress) {
    MOPPE_PROFILE_ZONE ("game::drain_surface");
    const auto report = [&progress] (HydrologyStage stage) {
      if (progress)
        progress (stage);
    };

    report (HydrologyStage::StandingWater);
    terrain::FloodField standing_water = terrain::analyze_standing_water (
      geometry, terrain::surface_elevation_point (recipe.water_datum ()));

    report (HydrologyStage::Lakes);
    terrain::LakeCensus lakes = terrain::census_lakes (standing_water);

    report (HydrologyStage::Drainage);
    terrain::DrainageGraph drainage =
      terrain::analyze_wet_drainage (standing_water, lakes);

    return std::tuple (
      std::move (standing_water), std::move (lakes), std::move (drainage));
  }

  Hydrology analyze_hydrology (const map::SurfaceGeometry& geometry,
                               const terrain::WorldRecipe& recipe,
                               const HydrologyProgress& progress) {
    auto [standing_water, lakes, drainage] =
      drain_surface (geometry, recipe, progress);
    if (progress)
      progress (HydrologyStage::Channels);
    terrain::FractionalDrainage channels =
      terrain::analyze_fractional_drainage (standing_water, lakes);

    if (progress)
      progress (HydrologyStage::Rivers);
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
                                     column (map::fireweed_abundance),
                                     column (map::spring_fen_abundance));
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
          proportion_t most = 0.15f * proportion[one];
          std::size_t s = 0;
          std::apply (
            [&] (const auto&... each) {
              ((each[i] > most ? (most = each[i], best = s) : 0, ++s), ...);
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
        "lichen", "heather", "bilberry", "sphagnum",   "dwarf-birch",
        "sedge",  "alvar",   "fireweed", "spring-fen", "grass"
      };
      std::cerr << "moppe: vegetation: trees";
      tally (trees, tree_names);
      std::cerr << "; field layer";
      tally (field, field_names);
      const auto rock = column (map::bedrock_exposure);
      std::size_t dry = 0, bare = 0;
      for (std::size_t i = 0; i < ground.size (); ++i)
        if (level[i] <= ground[i]) {
          ++dry;
          bare += rock[i] > 0.5f * map::bedrock_exposure[one];
        }
      std::cerr << "; bare rock "
                << std::lround (100.0 * bare / std::max<std::size_t> (dry, 1))
                << '%' << std::endl;
    }
  }

  static auto read_vegetation (const map::SurfaceGeometry& geometry,
                               const terrain::WorldRecipe& recipe,
                               const terrain::TrailUseMap& use,
                               const terrain::FloodField& standing_water,
                               const terrain::LakeCensus& lakes,
                               const terrain::DrainageGraph& drainage) {
    const meters_t water_level = recipe.water_datum ();
    terrain::MoistureMap moisture =
      terrain::analyze_moisture (standing_water, lakes.membership (), drainage);
    // Where the forest stops is a fraction of this world's own relief, not a
    // fixed climb from the sea. A flat island and an alpine world both want a
    // tree line, and they do not want the same one in metres. This fraction
    // sits just under where the terrain shader begins to hold snow, so the
    // last trees thin out into the snowfields instead of ending under them.
    constexpr proportion_t tree_line_share_of_relief = 0.62f * proportion[one];
    const meters_t land_relief = quantity_cast<isq::length> (
      terrain::measure_elevation_range (geometry).maximum -
      terrain::surface_elevation_point (water_level));
    // A near-flat world (the grass laboratory's rolling plain, or any
    // low-forcing recipe) still deserves a terrestrial habitat band: clamp
    // the tree line above the analysis floor instead of refusing to finish
    // the world. Everything on such a plain simply lies below the trees'
    // altitude limit, which is the truthful reading.
    const meters_t tree_line = std::max<meters_t> (
      water_level +
        quantity_cast<isq::length> (tree_line_share_of_relief * land_relief),
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

    return std::tuple (std::move (moisture),
                       std::move (habitat),
                       std::move (disturbance),
                       std::move (vegetation),
                       std::move (cover));
  }

  std::tuple<terrain::WaterSheets, map::SurfaceReadings>
  analyze_surface (const map::SurfaceGeometry& geometry,
                   const terrain::WorldRecipe& recipe,
                   const Hydrology& hydrology,
                   const terrain::TrailUseMap& use) {
    MOPPE_PROFILE_ZONE ("game::analyze_surface");
    const auto& [standing_water, lakes, drainage, rivers] = hydrology;
    terrain::WaterSheets sheets = terrain::paint_watercourses (
      geometry, standing_water, lakes, drainage, rivers);
    const terrain::Waterline waterline =
      terrain::extract_waterline (geometry, sheets, lakes.membership ());

    auto [moisture, habitat, disturbance, vegetation, cover] =
      read_vegetation (geometry, recipe, use, standing_water, lakes, drainage);

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

  std::pair<terrain::RoadNetwork, terrain::TrailUseMap>
  form_world_roads (map::SurfaceGeometry& geometry,
                    const terrain::WorldRecipe& recipe,
                    const terrain::TrailNetwork& trails) {
    MOPPE_PROFILE_ZONE ("game::form_world_roads");
    auto [standing_water, lakes, drainage] =
      drain_surface (geometry, recipe, {});
    // Roads avoid mires and dense forest. Compute just their inputs on
    // the unformed terrain. Channels and water sheets belong to the final bed.
    auto roads = [&] {
      auto [moisture, habitat, disturbance, vegetation, cover] =
        read_vegetation (
          geometry, recipe, trails.use, standing_water, lakes, drainage);
      return terrain::plan_roads (
        geometry.domain (),
        get<terrain::surface_elevation> (geometry),
        standing_water,
        drainage,
        { .forest_cover = get<map::forest_cover> (cover),
          .mire = get<map::sphagnum_abundance> (vegetation),
          .wetness = get<terrain::soil_wetness> (moisture) },
        terrain::visible_river_minimum_area (),
        trails.plan.home_base,
        terrain::surface_elevation_point (recipe.water_datum ()),
        recipe.seed ().value ^ 0x5d1c0a7bU);
    }();
    const auto built =
      map::form_terrain_roads (geometry, roads, standing_water, drainage);
    std::cerr << "moppe: roads: " << roads.places.size () << " places, "
              << roads.roads.size () << " ways, "
              << built.cut.numerical_value_in (u::m * u::m * u::m)
              << " m3 cut, "
              << built.fill.numerical_value_in (u::m * u::m * u::m)
              << " m3 filled, " << built.steep_points << " of " << built.points
              << " points steeper than the road's grade";
    for (const terrain::Road& road : roads.roads)
      if (road.loop)
        std::cerr << ", a loop round the world";
    std::cerr << std::endl;
    map::rebuild_geometry (geometry);
    terrain::TrailUseMap use = trails.use;
    auto& tread = spatial::get<map::trail_influence> (use);
    for (std::size_t cell = 0; cell < tread.size (); ++cell)
      tread[cell] =
        std::max (tread[cell],
                  quantity_cast<map::trail_influence> (built.influence[cell]));
    return { std::move (roads), std::move (use) };
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

  void GeneratedWorld::enter_play () {
    if (!m_build)
      return;
    const auto& d = m_surface.domain ();
    [&]<typename Domain, typename... Field> (
      std::type_identity<spatial::Bundle<Domain, Field...>>) {
      // Each task owns its packed tile directory and byte stream.
      const auto pack = [&]<typename Q> () {
        auto& grid = m_cover[PlayFields::spec_index<Q::quantity_spec>];
        if constexpr (Q::quantity_spec == map::snow_support)
          grid.assign (d, spatial::get<map::snow_support> (m_surface));
        else
          grid.assign (d, spatial::get<Q::quantity_spec> (m_build->readings));
      };
      const std::array<std::function<void ()>, sizeof...(Field)> tasks { [&] {
        pack.template operator()<Field> ();
      }... };
      parallel_for (tasks.size (), 1, [&] (size_t first, size_t last) {
        for (size_t i = first; i < last; ++i)
          tasks[i]();
      });
    }(std::type_identity<PlayFields> {});
    if (environment ("MOPPE_MEMORY_REPORT")) {
      size_t bytes = 0;
      for (const auto& grid : m_cover)
        bytes += grid.storage_bytes ();
      std::cerr << "moppe: runtime ground cover " << bytes << " bytes\n";
    }
    m_runtime_water.emplace (
      m_build->water, spatial::get<terrain::surface_elevation> (m_surface));
    const auto bodies = lake_census ().water_bodies ();
    m_runtime_bodies.assign (bodies.begin (), bodies.end ());
    m_runtime_rivers =
      std::move (std::get<terrain::RiverNetwork> (m_build->hydrology));
    m_build.reset ();
    const auto release = [] (auto& column) {
      std::remove_reference_t<decltype (column)> ().swap (column);
    };
    release (spatial::get<terrain::sediment_thickness> (m_surface));
    release (spatial::get<terrain::terrain_normal> (m_surface));
    release (spatial::get<map::eroded_surface_material> (m_surface));
    release (spatial::get<map::deposited_surface_material> (m_surface));
    release (spatial::get<map::snow_support> (m_surface));
    release (m_trails.earthwork_delta);
    release (spatial::get<map::trail_influence> (m_trails.use));
    release (spatial::get<map::home_base_influence> (m_trails.use));
    release (m_forest.sites);
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
        m_build (BuildFields {
          std::move (hydrology), std::move (water), std::move (readings) }),
        m_trails (std::move (trails)), m_roads (std::move (roads)),
        m_road_surface (m_roads,
                        plan_extent (m_surface.domain ().period_x (),
                                     m_surface.domain ().period_z ())),
        m_forest (std::move (forest)) {
    const auto period = plan_extent (m_surface.domain ().period_x (),
                                     m_surface.domain ().period_z ());
    m_road_surface.place_guardrails (m_roads, [&] (plan_position_t at) {
      const position_t ground =
        at_height (terrain::wrap (at, period), 0.f * u::m);
      return spatial::sample<terrain::surface_elevation> (m_surface, ground);
    });
    // Where the longest stands, to aim a camera at.
    std::size_t posts = 0;
    const terrain::Guardrail* longest = nullptr;
    for (const terrain::Guardrail& run : m_road_surface.guardrails ()) {
      posts += run.posts.size ();
      if (!longest || run.posts.size () > longest->posts.size ())
        longest = &run;
    }
    std::cerr << "moppe: guardrails: " << m_road_surface.guardrails ().size ()
              << " runs, " << posts << " posts";
    if (longest) {
      const terrain::Guardrail::Post& end = longest->posts.front ();
      const Vec2 at = plan_value (terrain::wrap (end.at, period));
      const Vec2& out = plan_value (end.out);
      std::cerr << "; the longest begins at " << at.x () << ' '
                << terrain::surface_elevation_value (end.rail) << ' ' << at.z ()
                << " facing out " << out.x () << ' ' << out.z ();
    }
    std::cerr << '\n';
  }
}
