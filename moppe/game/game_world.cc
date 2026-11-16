// Activating a generated world: its water, surface, stars, spawn, forest,
// boulders, and mushrooms, uploading it to the renderer once loaded, and the
// sky and weather over it as the day turns.

#include <moppe/game/game.hh>

namespace moppe {
  namespace game {
    GroundCover MoppeGame::ground_cover (const Vec3& at) const {
      const auto read = [&] (auto quantity) {
        return spatial::sample<quantity> (surface_readings (),
                                          moppe::position (at))
          .numerical_value_in (one);
      };
      const float tread =
        smoothstep (0.80f, 0.88f, read (map::trail_influence));
      const float bare =
        smoothstep (0.50f, 0.70f, read (map::erosion_exposure));
      const float wet = smoothstep (0.45f, 0.80f, read (map::surface_moisture));
      GroundCover ground;
      ground.leaves = m_forest.litter_at (at);
      const float loose = std::max (tread, bare);
      ground.dust = (0.2f + 0.8f * loose) * (1.0f - 0.6f * wet) *
                    (1.0f - 0.7f * ground.leaves);
      const auto blend = [] (DisplayColor a, DisplayColor b, float t) {
        return DisplayColor (a.red + (b.red - a.red) * t,
                             a.green + (b.green - a.green) * t,
                             a.blue + (b.blue - a.blue) * t);
      };
      ground.dust_color = blend (DisplayColor (0.56f, 0.53f, 0.43f),
                                 DisplayColor (0.60f, 0.52f, 0.40f),
                                 loose);
      ground.clod_color = blend (DisplayColor (0.27f, 0.32f, 0.16f),
                                 DisplayColor (0.42f, 0.34f, 0.24f),
                                 loose);
      // Under fallen leaves the wheel digs up dark forest soil.
      ground.clod_color = blend (
        ground.clod_color, DisplayColor (0.34f, 0.26f, 0.17f), ground.leaves);
      return ground;
    }

    Vec3 MoppeGame::trail_cell_position (terrain::CellIndex cell) const {
      if (cell == terrain::no_cell)
        return {};
      const terrain::TerrainDomain& grid = trail_network ().domain;
      const std::size_t width = grid.width ();
      const float x = (cell.value % width) *
                      (grid.spacing_x ()).numerical_value_in (moppe::u::m);
      const float z = (cell.value / width) *
                      (grid.spacing_z ()).numerical_value_in (moppe::u::m);
      return Vec3 (x,
                   terrain::surface_elevation_value (
                     spatial::sample<terrain::surface_elevation> (
                       surface (), moppe::position (Vec3 (x, 0.0f, z)))),
                   z);
    }

    Vec3 MoppeGame::trail_alignment_position (
      const terrain::TrailAlignmentPoint& point) const {
      return Vec3 (
        point.x_m,
        terrain::surface_elevation_value (
          spatial::sample<terrain::surface_elevation> (
            surface (), moppe::position (Vec3 (point.x_m, 0.0f, point.z_m)))),
        point.z_m);
    }

    Vec3 MoppeGame::trail_direction_from_home () const {
      if (trail_network ().alignment.points.size () < 2)
        return Vec3 (0, 0, 1);
      Vec3 direction =
        trail_alignment_position (trail_network ().alignment.points[1]) -
        trail_alignment_position (trail_network ().alignment.points[0]);
      direction[1] = 0.0f;
      return length2 (direction) > 1e-5f ? normalized (direction)
                                         : Vec3 (0, 0, 1);
    }

    void MoppeGame::build_home_base_marker () {
      m_home_base_marker.clear ();
      build_cairn (m_home_base_marker,
                   m_home_base_position,
                   trail_direction_from_home (),
                   static_cast<std::uint32_t> (recipe ().seed ().value),
                   [this] (float x, float z) {
                     return ground_height (Vec3 (x, 0.0f, z));
                   });
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
      m_map.forget ();
      m_place_names =
        name_places (m_generated_world->roads (),
                     static_cast<std::uint32_t> (m_recipe.seed ().value));
      {
        const terrain::TerrainDomain& domain = surface ().domain ();
        m_fingerposts =
          plan_fingerposts (m_generated_world->roads (),
                            domain.period_x ().numerical_value_in (u::m),
                            domain.period_z ().numerical_value_in (u::m));
        std::cerr << "moppe: fingerposts: " << m_fingerposts.size () << '\n';
      }
      m_fingerposts_made = false;
      m_fingerpost_mesh.reset ();
      m_fingerpost_lettering.reset ();
    }

    void MoppeGame::prepare_world_water () {
      MOPPE_PROFILE_ZONE ("startup.prepare_world_water");
      render::Renderer& r = *m_renderer;
      // Horizontal water was prepared with the world on the loading worker.
      // The render handoff builds only the few vertical nickpoint curtains,
      // never a dense mesh along every river reach.
      m_waterfall_surface.rebuild (r, surface (), rivers ());
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
      begin_day ();
      forget_companion ();
      m_home_base_position =
        trail_cell_position (trail_network ().plan.home_base);
      build_home_base_marker ();
      m_spawn_position =
        m_home_base_position - trail_direction_from_home () * 8.0f;
      m_spawn_position[1] =
        terrain::surface_elevation_value (
          spatial::sample<terrain::surface_elevation> (
            surface (),
            moppe::position (
              Vec3 (m_spawn_position[0], 0.0f, m_spawn_position[2])))) +
        1.2f;
      session ().bike ().reset (m_spawn_position);
      session ().bike ().set_heading (trail_direction_from_home ());
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
        }
      }
      // MOPPE_DEMO=jeep, tractor, caddy, mountain_bike, or long_john: the
      // autopilot drives that instead.
      if (demo && logic ().m_mode == M_BIKE) {
        if (std::string_view (demo) == "jeep")
          session ().set_mode (M_JEEP);
        else if (std::string_view (demo) == "tractor")
          session ().set_mode (M_TRACTOR);
        else if (std::string_view (demo) == "caddy")
          session ().set_mode (M_CADDY);
        else if (std::string_view (demo) == "mountain_bike")
          session ().set_mode (M_MOUNTAIN_BIKE);
        else if (std::string_view (demo) == "long_john")
          session ().set_mode (M_LONG_JOHN);
      }
    }

    void MoppeGame::move_spawn_to_forest () {
      // The fullest 40-metre cell of the forest plan's conifers.
      const ForestPlan& plan = generated_world ().forest ();
      const Vec3 period = extent_value (plan.period);
      constexpr float cell = 40.0f;
      const int nx = std::max (1, static_cast<int> (period[0] / cell));
      const int nz = std::max (1, static_cast<int> (period[2] / cell));
      std::vector<int> conifers (static_cast<std::size_t> (nx) * nz, 0);
      for (const ForestSite& site : plan.sites) {
        if (site.form != ForestForm::conifer)
          continue;
        const Vec3 at = position_value (site.position);
        const int x = std::clamp (static_cast<int> (at[0] / cell), 0, nx - 1);
        const int z = std::clamp (static_cast<int> (at[2] / cell), 0, nz - 1);
        ++conifers[static_cast<std::size_t> (z) * nx + x];
      }
      const auto densest =
        std::max_element (conifers.begin (), conifers.end ()) -
        conifers.begin ();
      m_spawn_position =
        Vec3 ((static_cast<float> (densest % nx) + 0.5f) * cell,
              0.0f,
              (static_cast<float> (densest / nx) + 0.5f) * cell);
      m_spawn_position[1] = ground_height (m_spawn_position) + 1.2f;
      session ().bike ().reset (m_spawn_position);
      std::cerr << "forest demo: densest conifer stand at " << m_spawn_position
                << " (" << conifers[densest] << " spruce in 40 m)\n";
    }

    bool MoppeGame::on_road (const Vec3& at, float reach, bool decks) const {
      const terrain::RoadSurface& roads = generated_world ().road_surface ();
      const terrain::RoadSample road = roads.nearest (at[0], at[2]);
      const terrain::RoadFormation& formation = roads.formation ();
      return road.found && road.kind == terrain::Road::Kind::Road &&
             (decks || !road.bridge) &&
             std::abs (road.across) <
               formation.half_width + formation.shoulder + reach;
    }

    void MoppeGame::grow_global_forest () {
      MOPPE_PROFILE_ZONE ("startup.build_global_forest");
      // Nothing grows where the places stand, nor on a road.
      ForestPlan plan = generated_world ().forest ();
      std::erase_if (plan.sites, [this] (const ForestSite& site) {
        const Vec3 at = position_value (site.position);
        return cleared (m_places, at) || on_road (at, 0.6f, true);
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
      const float sea = sea_level.numerical_value_in (u::m);
      const float highest = terrain::measure_height_range (surface ()).maximum;
      const auto start = std::chrono::steady_clock::now ();
      BoulderPlan plan = plan_boulders (surface (),
                                        surface_readings (),
                                        generated_world ().water_surface (),
                                        rivers (),
                                        recipe ().seed ().value ^ 0x6b0d1e55U,
                                        sea_level,
                                        std::max (highest - sea, 1.0f) * u::m);
      std::cerr << "moppe: boulder plan: "
                << std::chrono::duration_cast<std::chrono::milliseconds> (
                     std::chrono::steady_clock::now () - start)
                     .count ()
                << " ms" << std::endl;
      // Loose rock is cleared from where the places stand and off the
      // roads; the stones laid across running water stay, under the
      // bridges too.
      std::erase_if (plan.sites, [this] (const BoulderSite& site) {
        const Vec3 at = position_value (site.centre);
        return !site.solid &&
               (cleared (m_places, at) ||
                on_road (at, site.radius.numerical_value_in (u::m), false));
      });
      m_boulders.rebuild (*m_renderer, plan);
      // Where to cross a river dry-shod, for whoever wants to find one.
      const auto nearest = std::ranges::min_element (
        plan.crossings, {}, [this] (const StoneCrossing& crossing) {
          const Vec3 at = position_value (crossing.from);
          return std::hypot (at[0] - m_spawn_position[0],
                             at[2] - m_spawn_position[2]);
        });
      if (nearest != plan.crossings.end ()) {
        const Vec3 at = position_value (nearest->from);
        std::cerr << "moppe: stepping stones: " << plan.crossings.size ()
                  << " crossings, the nearest "
                  << std::hypot (at[0] - m_spawn_position[0],
                                 at[2] - m_spawn_position[2])
                  << " m from the spawn, at " << at[0] << ' ' << at[2]
                  << std::endl;
      }
    }

    void MoppeGame::grow_mushrooms () {
      MOPPE_PROFILE_ZONE ("startup.grow_mushrooms");
      m_mushrooms.rebuild (
        plan_mushrooms (generated_world ().forest (),
                        surface (),
                        surface_readings (),
                        generated_world ().water_surface (),
                        recipe ().seed ().value ^ 0x3c5f00d5U,
                        world ().water_level,
                        m_home_base_position));
      if (const auto first = m_mushrooms.nearest (m_spawn_position, 400.0f)) {
        const Vec3 at = m_mushrooms.site (*first).base;
        std::cerr << "moppe: nearest mushrooms ("
                  << mushroom_plural (m_mushrooms.site (*first).kind) << ") "
                  << std::hypot (at[0] - m_spawn_position[0],
                                 at[2] - m_spawn_position[2])
                  << " m from the spawn" << std::endl;
        // The foraging script starts a few steps from them.
        const char* walk = moppe::environment ("MOPPE_WALK");
        if (walk &&
            (std::string_view (walk) == "forage" ||
             std::string_view (walk) == "supper") &&
            logic ().m_mode == M_FOOT) {
          Vec3 away = m_spawn_position - at;
          away[1] = 0.0f;
          away = length2 (away) > 1e-4f ? normalized (away) : Vec3 (1, 0, 0);
          Vec3 stand = at + away * 4.0f;
          stand[1] = ground_height (stand) + 0.1f;
          session ().walker ().spawn (moppe::position (stand), away * -1.0f);
          logic ().m_fp_eye = session ().walker ().eye_position ();
        }
      }
    }

    void MoppeGame::settle_obstacles () {
      std::vector<mov::Trunk> obstacles = m_forest.trunks ();
      obstacles.insert (obstacles.end (),
                        m_boulders.colliders ().begin (),
                        m_boulders.colliders ().end ());
      const Vec3 period = extent_value (generated_world ().forest ().period);
      m_trunk_field.set_trunks (std::move (obstacles), period[0], period[2]);
      if (m_trunk_field.trunk_count ())
        std::cerr << "moppe: obstacle colliders: "
                  << m_trunk_field.trunk_count ()
                  << " (Box3D, streamed around the rider)" << std::endl;
    }

    GroundHeight MoppeGame::ground_reader () const {
      return
        [this] (float x, float z) { return ground_height (Vec3 (x, 0.0f, z)); };
    }

    float MoppeGame::ground_height (const Vec3& at) const {
      return terrain::surface_elevation_value (
        spatial::sample<terrain::surface_elevation> (
          surface (), moppe::position (Vec3 (at[0], 0.0f, at[2]))));
    }

    void MoppeGame::finish_loading (render::Renderer& r,
                                    std::unique_ptr<GeneratedWorld> completed) {
      MOPPE_PROFILE_ZONE ("MoppeGame::finish_loading");
      activate_completed_world (std::move (completed));
      prepare_world_water ();
      prepare_world_surface ();
      place_stars_and_player ();
      plan_world_places ();
      grow_global_forest ();
      scatter_boulders ();
      grow_mushrooms ();
      settle_obstacles ();
      build_world_places ();
      let_out_animals ();
      park_jeep ();
      park_tractor ();
      park_mountain_bike ();
      park_long_john ();
      park_helicopter ();
      // MOPPE_DEMO=boat: the autopilot takes the boat out instead, and
      // MOPPE_WALK=boat-fish takes it out to fish.
      const char* demo_name = moppe::environment ("MOPPE_DEMO");
      const char* walk_name = moppe::environment ("MOPPE_WALK");
      const bool boat_demo =
        (demo_name && std::string_view (demo_name) == "boat") ||
        (walk_name && std::string_view (walk_name) == "boat-fish");
      park_boat (boat_demo);
      // MOPPE_DEMO=jet_ski: the autopilot rides the jet ski out instead.
      const bool jet_ski_demo =
        demo_name && std::string_view (demo_name) == "jet_ski";
      park_jet_ski (jet_ski_demo);
      park_caddy ();
      // A demo already at the wheel of a machine just parked follows it.
      if (logic ().m_mode != M_FOOT && logic ().m_mode != M_BIKE)
        session ().settle_camera (surface ());
      if (boat_demo || jet_ski_demo) {
        session ().set_mode (boat_demo ? M_BOAT : M_JET_SKI);
        logic ().m_cam_mode = CAM_CHASE;
        session ().settle_camera (surface ());
      }
      stage_rescue ();
      // MOPPE_DEMO=helicopter: the autopilot takes it up instead, hooks
      // the jeep, parked beside it, on its line, and flies it off.
      if (const char* demo = moppe::environment ("MOPPE_DEMO");
          demo && std::string_view (demo) == "helicopter") {
        const mov::Helicopter& heli = session ().helicopter ();
        session ().jeep ().place (heli.point (Vec3 (6.0f, 0.0f, 0.0f)),
                                  heli.forward ());
        session ().set_mode (M_HELICOPTER);
        logic ().m_cam_mode = CAM_CHASE;
      }
      stage_camp_script ();
      stage_fish_script ();
      stage_place_script ();
      // The mouse looks around during play; M hands it back.
      platform::set_pointer_captured (true);
      plan_opening_journey ();
      remember_seed (world (),
                     recipe ().generation_profile (),
                     static_cast<int> (recipe ().seed ().value));
      upload_world_terrain (r);
      // The chart in the HUD's corner wants the map from the first
      // frame of play.
      const char* hud = moppe::environment ("MOPPE_HUD");
      if (!hud || std::string_view (hud) != "none")
        paint_map (r);
      m_ready = true;
      MOPPE_PROFILE_PLOT ("startup.ready", 1);
      std::cerr << "moppe: world ready " << m_loading.status ().elapsed
                << " s after loading began" << std::endl;

      // A screenshot plays a shot list it names (MOPPE_OPENING=file),
      // so that a still can be taken from any pose the grammar writes.
      const char* list = moppe::environment ("MOPPE_OPENING");
      const bool posed = list && std::filesystem::is_regular_file (list) &&
                         !m_screenshot_path.empty ();
      const bool automated = (!m_screenshot_path.empty () && !posed) ||
                             moppe::environment ("MOPPE_DEMO") ||
                             moppe::environment ("MOPPE_WALK");
      if (posed || (!automated && !m_skip_cinematic_requested)) {
        start_opening ();
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
      r.set_roads ({ .segment_rows = roads.segment_rows (),
                     .index = roads.index (),
                     .buckets_x = roads.buckets_x (),
                     .buckets_z = roads.buckets_z (),
                     .half_width = roads.formation ().half_width,
                     .shoulder = roads.formation ().shoulder,
                     .crown = roads.formation ().crown,
                     .blend = terrain::RoadSurface::blend_m,
                     .cut_slope = roads.formation ().cut_slope,
                     .fill_slope = roads.formation ().fill_slope });
      // The typed water and ground presentations can upload only after
      // set_terrain has established the texture dimensions.
      upload_water (r,
                    generated_world ().water_surface (),
                    spatial::get<terrain::surface_elevation> (surface ()),
                    world ().water_level,
                    world ().map_size);
      upload_surface_readings (r,
                               surface (),
                               surface_readings (),
                               generated_world ().water_surface ());
    }

    void MoppeGame::update_world_atmosphere (float total_time) {
      // Weather remains part of the world while actors are paused.
      cloud_cover_t cloudiness =
        (std::sin (total_time * 0.0003f) * 0.4f + 0.5f +
         0.3f * std::pow (std::sin (total_time * 0.0008f), 2.0f) +
         std::sin (total_time * 0.02f) * 0.05f) *
        cloud_cover[one];
      const Weather weather = read_weather ();
      logic ().m_weather = weather;
      cloudiness = std::clamp (cloudiness,
                               weather.cloud_floor * cloud_cover[one],
                               weather.cloud_ceiling * cloud_cover[one]);
      logic ().m_cloudiness = cloudiness;

      // Fog stays mostly sky-blue. Directional warmth is added in the
      // shaders only when looking toward the sun.
      read_sky ();
      // By night the haze keeps the sky's own dark blue, and an overcast
      // greys only as far as there is light to grey it.
      const float lit = 0.12f + 0.88f * m_sky.daylight;
      logic ().m_fog = mix_display (
        mix_display (m_sky.horizon,
                     scale_display (DisplayColor (0.90f, 0.94f, 1.0f), lit),
                     0.18f),
        scale_display (weather.fog_tint, lit),
        weather.fog_tint_amount);
    }

    Weather MoppeGame::read_weather () {
      if (m_held_sky)
        return weather_of (*m_held_sky);
      if (!m_day.turning || !m_session || !m_weather_timeline)
        return {};
      const double hours = hours_since_midnight (logic ().m_day_hours);
      const SkyKind kind = m_weather_timeline->kind_at (hours);
      if (kind != m_logged_sky) {
        m_logged_sky = kind;
        std::cerr << "moppe: weather: " << sky_kind_name (kind) << " from "
                  << std::fmod (hours, 24.0) << " h" << std::endl;
      }
      return m_weather_timeline->at (hours);
    }

    MoppeGame::Day MoppeGame::plan_day () const {
      Day day;
      const char* minutes = moppe::environment ("MOPPE_DAY");
      const char* hour = moppe::environment ("MOPPE_CLOCK");
      if (hour)
        day.first_hour = std::clamp ((float)::atof (hour), 0.0f, 24.0f);
      const float length = minutes ? (float)::atof (minutes) : 48.0f;
      if (length > 0.0f)
        day.pace = 24.0 / (60.0 * length);
      day.turning = day.pace > 0.0 || hour;
      return day;
    }

    void MoppeGame::begin_day () {
      m_weather_timeline.emplace (
        static_cast<std::uint32_t> (recipe ().seed ().value));
      m_logged_sky.reset ();
      logic ().m_day_hours =
        clock_time (m_day.first_hour
                      ? *m_day.first_hour
                      : m_almanac.clock_for_sun_height (m_graphics.sun_height)
                          .numerical_value_in (si::hour));
      read_sky ();
      if (m_day.turning)
        std::cerr << "moppe: the day begins at "
                  << m_sky.clock.numerical_value_in (si::hour)
                  << " h and takes "
                  << (m_day.pace > 0.0 ? 24.0 / (60.0 * m_day.pace) : 0.0)
                  << " minutes" << std::endl;
    }

    void MoppeGame::read_sky () {
      m_sky = m_day.turning && m_session
                ? m_almanac.at (hours_since_midnight (logic ().m_day_hours))
                : m_almanac.held (m_graphics.sun_height);
    }
  }
}
