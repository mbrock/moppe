// Activating a generated world: its water, surface, stars, spawn, forest,
// boulders, and mushrooms, uploading it to the renderer once loaded, and the
// sky and weather over it as the day turns.

#include <moppe/environment.hh>
#include <moppe/game/cairn.hh>
#include <moppe/game/game.hh>
#include <moppe/game/place_names.hh>
#include <moppe/game/seed_memory.hh>
#include <moppe/game/surface_presentation.hh>
#include <moppe/game/water_presentation.hh>
#include <moppe/mov/box3d_vec.hh>
#include <moppe/profile.hh>
#include <moppe/terrain/readings.hh>

namespace moppe {
  namespace game {
    namespace {
      // How far apart two places stand on the map, as std::hypot measures.
      meters_t plan_distance (const position_t& from, const position_t& to) {
        const plan_displacement_t apart = plan (from) - plan (to);
        return hypot (x_of (apart), z_of (apart));
      }
    }

    GroundCover MoppeGame::ground_cover (const position_t& at) const {
      const auto read = [&] (auto quantity) {
        return spatial::sample<quantity> (generated_world ().readings (), at);
      };
      constexpr auto part = proportion[one];
      const proportion_t tread =
        band (0.80f * part, 0.88f * part, read (map::trail_influence));
      const proportion_t bare =
        band (0.50f * part, 0.70f * part, read (map::erosion_exposure));
      const proportion_t wet =
        band (0.45f * part, 0.80f * part, read (map::surface_moisture));
      GroundCover ground;
      ground.leaves = m_forest.litter_at (plan (at));
      const proportion_t loose = std::max (tread, bare);
      const proportion_t whole = 1.0f * part;
      ground.dust = ((0.2f * part + 0.8f * loose) * (whole - 0.6f * wet) *
                     (whole - 0.7f * ground.leaves))
                      .numerical_value_in (one) *
                    part;
      const auto blend = [] (DisplayColor a, DisplayColor b, float t) {
        return DisplayColor (a.red + (b.red - a.red) * t,
                             a.green + (b.green - a.green) * t,
                             a.blue + (b.blue - a.blue) * t);
      };
      ground.dust_color = blend (DisplayColor (0.56f, 0.53f, 0.43f),
                                 DisplayColor (0.60f, 0.52f, 0.40f),
                                 loose.numerical_value_in (one));
      ground.clod_color = blend (DisplayColor (0.27f, 0.32f, 0.16f),
                                 DisplayColor (0.42f, 0.34f, 0.24f),
                                 loose.numerical_value_in (one));
      // Under fallen leaves the wheel digs up dark forest soil.
      ground.clod_color = blend (ground.clod_color,
                                 DisplayColor (0.34f, 0.26f, 0.17f),
                                 ground.leaves.numerical_value_in (one));
      return ground;
    }

    void MoppeGame::build_home_base_marker () {
      m_home_base_marker.clear ();
      build_cairn (m_home_base_marker,
                   moppe::position (m_home_base_position),
                   world_ground ().trail_heading (),
                   static_cast<std::uint32_t> (recipe ().seed ().value),
                   world_ground ().elevation_field ());
    }

    void MoppeGame::activate_completed_world (
      std::unique_ptr<GeneratedWorld> completed) {
      MOPPE_PROFILE_ZONE ("MoppeGame::activate_completed_world");
      if (!completed)
        throw std::logic_error ("no completed world to activate");

      // Keep the outgoing session and world alive until the new session has
      // bound to the completed world. The session owns every direct
      // terrain/surface borrower, so it must retire before its old world.
      std::unique_ptr<GeneratedWorld> retired_world =
        std::move (m_generated_world);
      std::unique_ptr<GameSession> retired_session = std::move (m_session);
      m_generated_world = std::move (completed);
      m_params = m_generated_world->params ();
      m_recipe = m_generated_world->recipe ();
      // Before the session places anything: its bodies stand on the
      // roads.
      mov::attach_roads (surface (), &m_generated_world->road_surface ());
      m_session = std::make_unique<GameSession> (world (), surface ());
      retired_session.reset ();
      retired_world.reset ();
      m_heads_up.map.forget ();
      m_place_names =
        name_places (m_generated_world->roads (),
                     static_cast<std::uint32_t> (m_recipe.seed ().value));
      const terrain::TerrainDomain& domain = surface ().domain ();
      m_signs.plan (m_generated_world->roads (),
                    plan_extent (domain.period_x (), domain.period_z ()));
    }

    void MoppeGame::prepare_world_water () {
      MOPPE_PROFILE_ZONE ("startup.prepare_world_water");
      render::Renderer& r = *m_renderer;
      // Horizontal water was prepared with the world on the loading worker.
      // The render handoff builds only the few vertical nickpoint curtains,
      // never a dense mesh along every river reach.
      m_waterfall_surface.rebuild (r, surface (), generated_world ().rivers ());
      m_bridges.rebuild (r,
                         surface (),
                         generated_world ().roads (),
                         generated_world ().road_surface ());
    }

    void MoppeGame::prepare_world_surface () {
      MOPPE_PROFILE_ZONE ("startup.prepare_world_surface");
      session ().bike ().set_water_level (world ().water_level);
    }

    void MoppeGame::place_stars_and_player () {
      MOPPE_PROFILE_ZONE ("startup.place_stars_and_player");
      session ().stars ().generate (surface (), world (), 80);
      m_sky.begin (logic (),
                   static_cast<std::uint32_t> (recipe ().seed ().value));
      m_company.forget ();
      const position_t home = world_ground ().trail_cell (
        generated_world ().trails ().plan.home_base);
      m_home_base_position = position_value (home);
      build_home_base_marker ();
      // Eight metres back down the trail, a little above its ground.
      const direction_t heading = world_ground ().trail_heading ();
      const plan_position_t spawn =
        plan (home - quantity_cast<isq::position_vector> (
                       along (heading, 8.0f * u::m)));
      m_spawn_position = position_value (terrain::at_elevation (
        spawn, world_ground ().elevation (spawn) + 1.2f * u::m));
      session ().bike ().reset (m_spawn_position);
      session ().bike ().set_heading (direction_value (heading));
      const char* demo = moppe::environment ("MOPPE_DEMO");
      if (demo && std::string_view (demo) == "forest")
        move_spawn_to_forest ();
      // It is a walking game first: the rider stands beside the parked
      // bike. The demo autopilot still starts riding, unless a scripted
      // walk (MOPPE_WALK) asks for the feet.
      if (!demo || moppe::environment ("MOPPE_WALK")) {
        session ().start_on_foot ();
        // Scripted walks can watch from behind or in front of the figure.
        if (const char* view = moppe::environment ("MOPPE_WALK_CAMERA")) {
          const std::string_view name (view);
          if (name == "chase" || name == "side")
            logic ().m_cam_mode = CAM_CHASE;
          else if (name == "front")
            logic ().m_cam_mode = CAM_FRONT;
          else if (name == "eyes")
            logic ().m_cam_mode = CAM_HELMET;
          // And riding too, after getting on (the bus-ride script's).
          logic ().m_ride_cam = logic ().m_cam_mode;
        }
      }
      // MOPPE_DEMO=jeep, tractor, caddy, mountain_bike, or long_john: the
      // autopilot drives that instead.
      if (demo && logic ().m_doing == Doing::driving (Machine::bike)) {
        if (std::string_view (demo) == "jeep")
          session ().set_mode (Doing::driving (Machine::jeep));
        else if (std::string_view (demo) == "tractor")
          session ().set_mode (Doing::driving (Machine::tractor));
        else if (std::string_view (demo) == "caddy")
          session ().set_mode (Doing::driving (Machine::caddy));
        else if (std::string_view (demo) == "mountain_bike")
          session ().set_mode (Doing::driving (Machine::mountain_bike));
        else if (std::string_view (demo) == "long_john")
          session ().set_mode (Doing::driving (Machine::long_john));
      }
    }

    void MoppeGame::move_spawn_to_forest () {
      // The fullest 40-metre cell of the forest plan's conifers.
      const ForestPlan& plan = generated_world ().forest ();
      constexpr meters_t cell = 40.0f * u::m;
      // Whole cells along a span.
      const auto cells = [&] (meters_t span) {
        return static_cast<int> ((span / cell).numerical_value_in (one));
      };
      const int nx = std::max (1, cells (extent_value (plan.period)[0] * u::m));
      const int nz = std::max (1, cells (extent_value (plan.period)[2] * u::m));
      std::vector<int> conifers (static_cast<std::size_t> (nx) * nz, 0);
      for (const ForestSite& site : plan.sites) {
        if (site.form != ForestForm::Conifer)
          continue;
        const plan_position_t at = moppe::plan (site.position);
        const int x = std::clamp (cells (x_of (at)), 0, nx - 1);
        const int z = std::clamp (cells (z_of (at)), 0, nz - 1);
        ++conifers[static_cast<std::size_t> (z) * nx + x];
      }
      const auto densest =
        std::max_element (conifers.begin (), conifers.end ()) -
        conifers.begin ();
      const plan_position_t spot =
        plan_position ((static_cast<float> (densest % nx) + 0.5f) * cell,
                       (static_cast<float> (densest / nx) + 0.5f) * cell);
      m_spawn_position = position_value (terrain::at_elevation (
        spot, world_ground ().elevation (spot) + 1.2f * u::m));
      session ().bike ().reset (m_spawn_position);
      std::cerr << "forest demo: densest conifer stand at " << m_spawn_position
                << " (" << conifers[densest] << " spruce in 40 m)\n";
    }

    void MoppeGame::grow_global_forest () {
      MOPPE_PROFILE_ZONE ("startup.build_global_forest");
      // Nothing grows where the places stand, nor on a road.
      ForestPlan plan = generated_world ().forest ();
      std::erase_if (plan.sites, [this] (const ForestSite& site) {
        return cleared (m_places.layout, site.position) ||
               world_ground ().on_road (
                 moppe::plan (site.position), 0.6f * u::m, true);
      });
      m_forest.rebuild (*m_renderer, plan);
      std::cerr << "global forest: " << m_forest.tree_count ()
                << " canopy representatives, "
                << m_forest.resident_bytes () / (1024 * 1024)
                << " MB resident\n";
    }

    void MoppeGame::scatter_boulders () {
      MOPPE_PROFILE_ZONE ("startup.scatter_boulders");
      const meters_t sea_level = world ().water_level;
      // The land's own relief over the sea, never under a metre.
      const meters_t relief = std::max<meters_t> (
        quantity_cast<isq::length> (
          terrain::measure_elevation_range (surface ()).maximum -
          terrain::surface_elevation_point (sea_level)),
        1.0f * u::m);
      const auto start = std::chrono::steady_clock::now ();
      BoulderPlan plan =
        plan_boulders (surface (),
                       generated_world ().readings (),
                       generated_world ().water_surface (),
                       generated_world ().rivers (),
                       recipe ().seed ().value ^ 0x6b0d1e55U,
                       terrain::surface_elevation_point (sea_level),
                       relief);
      std::cerr << "moppe: boulder plan: "
                << std::chrono::duration_cast<std::chrono::milliseconds> (
                     std::chrono::steady_clock::now () - start)
                     .count ()
                << " ms" << std::endl;
      // Loose rock is cleared from where the places stand and off the
      // roads; the stones laid across running water stay, under the
      // bridges too.
      std::erase_if (plan.sites, [this] (const BoulderSite& site) {
        return !site.solid &&
               (cleared (m_places.layout, site.centre) ||
                world_ground ().on_road (
                  moppe::plan (site.centre), site.radius, false));
      });
      m_boulders.rebuild (*m_renderer, plan);
      // Where to cross a river dry-shod, for whoever wants to find one.
      const position_t spawn = moppe::position (m_spawn_position);
      const auto nearest = std::ranges::min_element (
        plan.crossings, {}, [&] (const StoneCrossing& crossing) {
          return plan_distance (crossing.from, spawn);
        });
      if (nearest != plan.crossings.end ()) {
        const plan_position_t at = moppe::plan (nearest->from);
        std::cerr << "moppe: stepping stones: " << plan.crossings.size ()
                  << " crossings, the nearest "
                  << meters_value (plan_distance (nearest->from, spawn))
                  << " m from the spawn, at " << meters_value (x_of (at)) << ' '
                  << meters_value (z_of (at)) << std::endl;
      }
    }

    void MoppeGame::grow_mushrooms () {
      MOPPE_PROFILE_ZONE ("startup.grow_mushrooms");
      m_mushrooms.rebuild (
        plan_mushrooms (generated_world ().forest (),
                        surface (),
                        generated_world ().readings (),
                        generated_world ().water_surface (),
                        recipe ().seed ().value ^ 0x3c5f00d5U,
                        world ().water_level,
                        position (m_home_base_position)));
      if (const auto first =
            m_mushrooms.nearest (position (m_spawn_position), 400.0f * u::m)) {
        const position_t at = m_mushrooms.site (*first).base;
        const position_t spawn = moppe::position (m_spawn_position);
        std::cerr << "moppe: nearest mushrooms ("
                  << mushroom_plural (m_mushrooms.site (*first).kind) << ") "
                  << meters_value (plan_distance (at, spawn))
                  << " m from the spawn" << std::endl;
        // The foraging script starts a few steps from them.
        const char* walk = moppe::environment ("MOPPE_WALK");
        if (walk &&
            (std::string_view (walk) == "forage" ||
             std::string_view (walk) == "supper") &&
            logic ().m_doing == Doing::on_foot ()) {
          // Levelled, then normalized as a vector of the world.
          Vec3 level = displacement_value (spawn - at);
          level[1] = 0.0f;
          const direction_t away = unit_direction (
            length2 (level) > 1e-4f ? normalized (level) : Vec3 (1, 0, 0));
          const plan_position_t stand =
            moppe::plan (at + quantity_cast<isq::position_vector> (
                                along (away, 4.0f * u::m)));
          session ().walker ().spawn (
            terrain::at_elevation (
              stand, world_ground ().elevation (stand) + 0.1f * u::m),
            direction_value (away) * -1.0f);
          logic ().m_fp_eye = position (session ().walker ().eye_position ());
        }
      }
    }

    void MoppeGame::settle_obstacles (bool with_places) {
      std::vector<mov::Trunk> obstacles = m_forest.trunks ();
      obstacles.insert (obstacles.end (),
                        m_boulders.colliders ().begin (),
                        m_boulders.colliders ().end ());
      if (with_places) {
        obstacles.insert (obstacles.end (),
                          m_places.landscape.colliders ().begin (),
                          m_places.landscape.colliders ().end ());
        const std::vector<mov::Trunk> stops = bus_stop_colliders (m_places.bus);
        obstacles.insert (obstacles.end (), stops.begin (), stops.end ());
      }
      const spatial_extent_t& period = generated_world ().forest ().period;
      m_trunk_field.set_trunks (std::move (obstacles),
                                extent_value (period)[0],
                                extent_value (period)[2]);
      if (m_trunk_field.trunk_count () && !with_places)
        std::cerr << "moppe: obstacle colliders: "
                  << m_trunk_field.trunk_count ()
                  << " (Box3D, streamed around the rider)" << std::endl;
    }

    void MoppeGame::finish_loading (render::Renderer& r,
                                    std::unique_ptr<GeneratedWorld> completed) {
      MOPPE_PROFILE_ZONE ("MoppeGame::finish_loading");
      activate_completed_world (std::move (completed));
      prepare_world_water ();
      prepare_world_surface ();
      place_stars_and_player ();
      m_places.plan (world_ground (),
                     session (),
                     static_cast<std::uint32_t> (recipe ().seed ().value),
                     m_place_names);
      grow_global_forest ();
      scatter_boulders ();
      grow_mushrooms ();
      settle_obstacles (false);
      m_places.build (m_renderer, world_ground ());
      if (m_renderer && !m_places.landscape.colliders ().empty ())
        settle_obstacles (true);
      m_places.let_out_animals (world_ground (),
                                session (),
                                moppe::position (m_home_base_position),
                                moppe::position (m_spawn_position));
      m_parking.park_jeep (parking_lot ());
      m_parking.park_tractor (parking_lot ());
      m_parking.park_mountain_bike (parking_lot ());
      m_parking.park_long_john (parking_lot ());
      m_parking.park_helicopter (parking_lot ());
      // MOPPE_DEMO=boat: the autopilot takes the boat out instead, and
      // MOPPE_WALK=boat-fish takes it out to fish.
      const char* demo_name = moppe::environment ("MOPPE_DEMO");
      const char* walk_name = moppe::environment ("MOPPE_WALK");
      const bool boat_demo =
        (demo_name && std::string_view (demo_name) == "boat") ||
        (walk_name && std::string_view (walk_name) == "boat-fish");
      m_parking.park_boat (parking_lot (), boat_demo);
      // MOPPE_DEMO=jet_ski: the autopilot rides the jet ski out instead.
      const bool jet_ski_demo =
        demo_name && std::string_view (demo_name) == "jet_ski";
      m_parking.park_jet_ski (parking_lot (), jet_ski_demo);
      m_parking.park_caddy (parking_lot ());
      // A demo already at the wheel of a machine just parked follows it.
      if (logic ().m_doing != Doing::on_foot () &&
          logic ().m_doing != Doing::driving (Machine::bike))
        session ().settle_camera (surface ());
      if (boat_demo || jet_ski_demo) {
        session ().set_mode (boat_demo ? Doing::driving (Machine::boat)
                                       : Doing::driving (Machine::jet_ski));
        logic ().m_cam_mode = CAM_CHASE;
        session ().settle_camera (surface ());
      }
      game::stage_rescue (session (), world_ground ());
      // MOPPE_DEMO=helicopter: the autopilot takes it up instead, hooks
      // the jeep, parked beside it, on its line, and flies it off.
      if (const char* demo = moppe::environment ("MOPPE_DEMO");
          demo && std::string_view (demo) == "helicopter") {
        const mov::Helicopter& heli = session ().helicopter ();
        session ().jeep ().place (heli.point (Vec3 (6.0f, 0.0f, 0.0f)),
                                  heli.forward ());
        session ().set_mode (Doing::driving (Machine::helicopter));
        logic ().m_cam_mode = CAM_CHASE;
      }
      m_walk.stage (script_stage ());
      // MOPPE_MAP=whole|near: the map unfolded from the start, for a
      // capture of it.
      if (const char* map = moppe::environment ("MOPPE_MAP"))
        m_heads_up.map.show (std::string_view (map) == "near"
                               ? MapSheet::View::Near
                               : MapSheet::View::Whole);
      // The mouse looks around during play; M hands it back.
      platform::set_pointer_captured (true);
      m_opening.plan (generated_world (),
                      world_ground (),
                      m_sky.sky (),
                      session (),
                      position (m_home_base_position),
                      position (m_spawn_position));
      remember_seed (world (),
                     recipe ().generation_profile (),
                     static_cast<int> (recipe ().seed ().value));
      upload_world_terrain (r);
      // The chart in the HUD's corner wants the map from the first
      // frame of play.
      const char* hud = moppe::environment ("MOPPE_HUD");
      if (!hud || std::string_view (hud) != "none")
        m_heads_up.paint_map (r, generated_world ());
      m_ready = true;
      MOPPE_PROFILE_PLOT ("startup.ready", 1);
      std::cerr << "moppe: world ready "
                << seconds_value (m_loading.status ().elapsed)
                << " s after loading began" << std::endl;

      // A screenshot plays a shot list it names (MOPPE_OPENING=file),
      // so that a still can be taken from any pose the grammar writes.
      const char* list = moppe::environment ("MOPPE_OPENING");
      const bool posed = list && std::filesystem::is_regular_file (list) &&
                         m_capture.taking_still ();
      const bool automated = (m_capture.taking_still () && !posed) ||
                             moppe::environment ("MOPPE_DEMO") ||
                             moppe::environment ("MOPPE_WALK");
      if (posed || (!automated && !m_skip_cinematic_requested)) {
        m_opening.start (session (), surface (), m_sky);
        m_live_input.clear ();
      }
    }

    void MoppeGame::upload_world_terrain (render::Renderer& r) {
      MOPPE_PROFILE_ZONE ("startup.upload_world_terrain");
      m_terrain.setup (r,
                       surface (),
                       world (),
                       m_graphics,
                       &generated_world ().water_surface ());
      const terrain::RoadSurface& roads = generated_world ().road_surface ();
      r.set_roads (
        { .segment_rows = roads.segment_rows (),
          .index = roads.index (),
          .buckets_x = roads.buckets_x (),
          .buckets_z = roads.buckets_z (),
          .half_width = meters_value (roads.formation ().half_width),
          .shoulder = meters_value (roads.formation ().shoulder),
          .crown = roads.formation ().crown.numerical_value_in (one),
          .blend = meters_value (terrain::RoadSurface::blend),
          .cut_slope = roads.formation ().cut_slope.numerical_value_in (one),
          .fill_slope =
            roads.formation ().fill_slope.numerical_value_in (one) });
      // The typed water and ground presentations can upload only after
      // set_terrain has established the texture dimensions.
      upload_water (r,
                    generated_world ().water_surface (),
                    spatial::get<terrain::surface_elevation> (surface ()),
                    world ().water_level,
                    world ().map_size);
      upload_surface_readings (r,
                               surface (),
                               generated_world ().readings (),
                               generated_world ().water_surface ());
    }
  }
}
