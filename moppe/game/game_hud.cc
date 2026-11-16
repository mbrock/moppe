// What the HUD is told each frame: the prompts and readings of camp,
// fishing, and the basket, the waymarks, the chart, the map sheet, the trail
// map, and the opening's titles.

#include <moppe/game/game.hh>
#include <moppe/terrain/torus.hh>

namespace moppe {
  namespace game {
    void MoppeGame::draw_trail_map (render::DrawList& dl,
                                    int width_pts,
                                    int height_pts,
                                    const Vec3& subject,
                                    Vec3 heading) const {
      if (width_pts < 480 || height_pts < 360)
        return;
      const terrain::TerrainDomain& grid = trail_network ().domain;
      const auto& alignment = trail_network ().alignment.points;
      if (alignment.size () < 2)
        return;
      const float period_x =
        grid.width () * (grid.spacing_x ()).numerical_value_in (moppe::u::m);
      const float period_z =
        grid.height () * (grid.spacing_z ()).numerical_value_in (moppe::u::m);
      const float home_x = alignment.front ().x_m;
      const float home_z = alignment.front ().z_m;
      const auto wrap_delta = [] (float delta, float period) {
        if (delta > period * 0.5f)
          delta -= period;
        if (delta < -period * 0.5f)
          delta += period;
        return delta;
      };
      const auto relative_alignment = [&] (terrain::TrailAlignmentPoint point) {
        return Vec3 (point.x_m - home_x, 0, point.z_m - home_z);
      };

      Vec3 low (0, 0, 0);
      Vec3 high (0, 0, 0);
      for (const terrain::TrailAlignmentPoint alignment_point : alignment) {
        const Vec3 point = relative_alignment (alignment_point);
        low[0] = std::min (static_cast<float> (low[0]), point[0]);
        low[2] = std::min (static_cast<float> (low[2]), point[2]);
        high[0] = std::max (static_cast<float> (high[0]), point[0]);
        high[2] = std::max (static_cast<float> (high[2]), point[2]);
      }
      const float world_span =
        std::max ({ high[0] - low[0], high[2] - low[2], 100.0f }) * 1.16f;
      const float center_x = 0.5f * (low[0] + high[0]);
      const float center_z = 0.5f * (low[2] + high[2]);
      const float map_size = std::min (154.0f, height_pts * 0.24f);
      const float map_x = 12.0f;
      const float map_y = height_pts - map_size - 12.0f;
      const float inset = 9.0f;
      const float scale = (map_size - 2.0f * inset) / world_span;
      const auto map_point = [&] (const Vec3& point) {
        return Vec3 (map_x + map_size * 0.5f + (point[0] - center_x) * scale,
                     map_y + map_size * 0.5f - (point[2] - center_z) * scale,
                     0);
      };

      render::DrawState state;
      state.blend = true;
      state.depth_test = false;
      state.depth_write = false;
      state.cull = false;
      dl.state (state);
      dl.lit (false);
      dl.fogged (false);
      // Keep the map field opaque. With frame interpolation, a translucent
      // HUD field is first composited over the current rendered scene and
      // then decomposited by MetalFX for the generated midpoint. Fast
      // ground motion makes that reconstruction visibly pulse in flight.
      dl.color (0.01f, 0.025f, 0.035f, 1.0f);
      dl.begin (render::Prim::Quads);
      dl.vertex (map_x, map_y);
      dl.vertex (map_x + map_size, map_y);
      dl.vertex (map_x + map_size, map_y + map_size);
      dl.vertex (map_x, map_y + map_size);
      dl.end ();
      dl.color (0.22f, 0.42f, 0.46f, 0.9f);
      dl.line (map_x, map_y, map_x + map_size, map_y, 1.0f);
      dl.line (
        map_x + map_size, map_y, map_x + map_size, map_y + map_size, 1.0f);
      dl.line (
        map_x + map_size, map_y + map_size, map_x, map_y + map_size, 1.0f);
      dl.line (map_x, map_y + map_size, map_x, map_y, 1.0f);

      dl.color (1.0f, 0.58f, 0.12f, 0.96f);
      for (std::size_t point = 0; point < alignment.size (); ++point) {
        const Vec3 a = relative_alignment (alignment[point]);
        Vec3 b =
          relative_alignment (alignment[(point + 1) % alignment.size ()]);
        b[0] = a[0] + wrap_delta (b[0] - a[0], period_x);
        b[2] = a[2] + wrap_delta (b[2] - a[2], period_z);
        const Vec3 ma = map_point (a);
        const Vec3 mb = map_point (b);
        dl.line (ma[0], ma[1], mb[0], mb[1], 2.4f);
      }

      const Vec3 home_map = map_point (Vec3 (0, 0, 0));
      dl.color (1.0f, 0.9f, 0.45f, 1.0f);
      dl.begin (render::Prim::Quads);
      dl.vertex (home_map[0] - 3.0f, home_map[1] - 3.0f);
      dl.vertex (home_map[0] + 3.0f, home_map[1] - 3.0f);
      dl.vertex (home_map[0] + 3.0f, home_map[1] + 3.0f);
      dl.vertex (home_map[0] - 3.0f, home_map[1] + 3.0f);
      dl.end ();

      const Vec3 relative_subject (wrap_delta (subject[0] - home_x, period_x),
                                   0,
                                   wrap_delta (subject[2] - home_z, period_z));
      Vec3 player = map_point (relative_subject);
      player[0] = std::clamp (
        static_cast<float> (player[0]), map_x + 5.0f, map_x + map_size - 5.0f);
      player[1] = std::clamp (
        static_cast<float> (player[1]), map_y + 5.0f, map_y + map_size - 5.0f);
      heading[1] = 0.0f;
      if (length2 (heading) < 1e-5f)
        heading = Vec3 (0, 0, 1);
      heading = normalized (heading);
      const Vec3 side (-heading[2], 0, heading[0]);
      dl.color (0.35f, 0.95f, 1.0f, 1.0f);
      dl.begin (render::Prim::Triangles);
      dl.vertex (player[0] + heading[0] * 7.0f, player[1] - heading[2] * 7.0f);
      dl.vertex (player[0] - heading[0] * 4.0f + side[0] * 4.0f,
                 player[1] + heading[2] * 4.0f - side[2] * 4.0f);
      dl.vertex (player[0] - heading[0] * 4.0f - side[0] * 4.0f,
                 player[1] + heading[2] * 4.0f + side[2] * 4.0f);
      dl.end ();
      dl.state (render::DrawState ());
      dl.lit (true);
      dl.fogged (true);
      dl.color (1, 1, 1, 1);
    }

    void MoppeGame::add_fishing_readings (HudState& state) const {
      const Fishing& fishing = logic ().m_fishing;
      const Creel& creel = fishing.creel;
      state.fish_total = creel.kept ();
      const float since =
        static_cast<float> (logic ().m_total_time - creel.last_time);
      if (since < 5.0f) {
        const std::string name = fish_name (creel.last.kind);
        if (creel.last_lost) {
          m_catch_note = "It got away.";
        } else {
          const bool vowel =
            std::string_view ("aeiou").find (name[0]) != std::string::npos;
          m_catch_note = (vowel ? "An " : "A ") + name + ", " +
                         std::to_string (static_cast<int> (std::lround (
                           100.0f * meters_value (creel.last.length)))) +
                         " cm!";
        }
        state.catch_note = m_catch_note.c_str ();
        state.catch_age_s = since;
      }
      if (!angler ())
        return;
      const bool afloat = logic ().m_mode == M_BOAT;
      switch (fishing.line.phase) {
      case Line::Phase::idle:
        if (afloat ? boat_drifting () && water_ahead ()
                   : !state.mushroom_in_reach &&
                       state.cook == HudState::Cook::none &&
                       state.camp != HudState::Camp::lie_down && water_ahead ())
          state.angle = HudState::Angle::cast;
        break;
      case Line::Phase::waiting:
        state.angle = HudState::Angle::wait;
        break;
      case Line::Phase::biting:
        state.angle = HudState::Angle::strike;
        break;
      case Line::Phase::playing:
        state.angle = HudState::Angle::playing;
        break;
      default:
        break;
      }
    }

    void MoppeGame::add_camp_readings (HudState& state) const {
      const Camp& camp = logic ().m_camp;
      state.resting = camp.resting;
      state.clock_hours = m_sky.clock.numerical_value_in (si::hour);
      if (static_cast<float> (logic ().m_total_time - camp.ate_time) < 6.0f) {
        // What there was, the fish first, then the eggs, then the
        // mushrooms.
        std::vector<std::string> dishes;
        if (camp.ate_fish)
          dishes.push_back (fish_name (camp.ate.kind));
        if (camp.ate_eggs > 0)
          dishes.push_back (camp.ate_eggs > 1 ? "eggs" : "an egg");
        if (camp.ate_count > 0)
          dishes.push_back (camp.ate_count > 1 ? "mushrooms" : "a mushroom");
        std::string meal;
        for (std::size_t i = 0; i < dishes.size (); ++i)
          meal += (i == 0                    ? ""
                   : i + 1 == dishes.size () ? " and "
                                             : ", ") +
                  dishes[i];
        if (meal.starts_with ("a "))
          meal = meal.substr (2);
        else if (meal.starts_with ("an "))
          meal = meal.substr (3);
        const bool one = !camp.ate_fish && camp.ate_count + camp.ate_eggs == 1;
        m_meal_note =
          camp.ate_cooked > 1.4f
            ? "Burnt to a cinder. Still, it is supper."
            : (one ? "A fried " : "Fried ") + meal + " by the fire. Lovely.";
        state.meal = m_meal_note.c_str ();
        state.meal_age_s =
          static_cast<float> (logic ().m_total_time - camp.ate_time);
      }
      // What Space and F do here, and what can be done about the camp
      // without a prompt for it (HudState::thumb).
      if (logic ().m_mode != m_thumb_mode) {
        m_thumb_mode = logic ().m_mode;
        m_thumb_from = logic ().m_total_time;
      }
      // The bike's jets and the boat's tied-back governor are prompted
      // for a while after getting on, until they have been tried once.
      const bool afloat = logic ().m_mode == M_BOAT;
      const bool boosts = logic ().m_mode == M_BIKE || afloat;
      if (boosts && logic ().m_boost_input > 0.1f)
        m_boost_tried[afloat] = true;
      state.thumb_hint = boosts && !m_boost_tried[afloat] &&
                         logic ().m_total_time - m_thumb_from < 15.0;
      // The wheelie is prompted once the jets' prompt is done with, for
      // half a minute after getting on, until one has been ridden.
      if (logic ().m_mode == M_BIKE && session ().bike ().wheeling ())
        m_wheelie_tried = true;
      state.wheelie_hint = logic ().m_mode == M_BIKE && !state.thumb_hint &&
                           !m_wheelie_tried &&
                           logic ().m_total_time - m_thumb_from < 45.0;
      switch (logic ().m_mode) {
      case M_FOOT:
        if (!camp.resting) {
          state.thumb = "Jump";
          state.can_take_down = beside_hammock ();
          Vec3 to_fire = session ().walker ().position () - camp.hearth;
          to_fire[1] = 0.0f;
          state.can_douse_fire = camp.fire && length2 (to_fire) < 3.0f * 3.0f;
          state.can_light_fire =
            !camp.fire &&
            (hearth_ahead ().has_value () ||
             hearth_at_hand (m_places, session ().walker ().position ()));
          if (const std::optional<std::size_t> beacon = beacon_at_hand ()) {
            const bool lit = logic ().m_beacons[*beacon].burning ();
            state.can_douse_fire = lit;
            state.can_light_fire = !lit && !their_beacon (*beacon);
          }
        }
        break;
      case M_BIKE:
        state.thumb = "Boost";
        state.dismount = "Get off";
        state.skids = true;
        state.wheelies = true;
        break;
      case M_MOUNTAIN_BIKE:
        state.thumb = "Hop";
        state.dismount = "Get off";
        state.skids = true;
        break;
      case M_LONG_JOHN:
        state.dismount = "Get off";
        state.skids = true;
        break;
      case M_GLIDER:
        state.thumb = "Flare";
        break;
      case M_JEEP:
        state.thumb = "Brake";
        state.dismount = "Step out";
        break;
      case M_PASSENGER:
        state.dismount = "Step out";
        break;
      case M_TRACTOR:
        state.thumb = "Brake";
        state.dismount = "Climb down";
        break;
      case M_CADDY:
        state.thumb = "Brake";
        state.dismount = "Step out";
        break;
      case M_BOAT:
        state.thumb = "Flat out";
        state.dismount = "Step ashore";
        break;
      case M_JET_SKI:
        state.dismount = "Get off";
        break;
      case M_HELICOPTER:
        state.thumb = "Climb";
        state.flying = true;
        state.can_step_out = session ().can_step_out_of_helicopter ();
        // Letting go is prompted only once the load hangs near the
        // ground, though the key lets it fall from any height.
        if (logic ().m_sling.hooked &&
            session ().helicopter ().altitude () < sling_hang + 4.0f * u::m)
          state.slung = machine_shape (logic ().m_sling.load).name;
        if (const std::optional<Machine> under =
              session ().hook_within_reach ())
          state.hook = machine_shape (*under).name;
        if (logic ().m_sling.hooked)
          state.dismount = "Let go";
        else if (state.hook)
          state.dismount = "Hook on";
        else if (state.can_step_out)
          state.dismount = "Step out";
        break;
      }
      if (logic ().m_mode != M_FOOT || state.mushroom_in_reach)
        return;
      state.cook = supper_at_hand ();
      state.cook_fish =
        camp.pan.on ? camp.pan.has_fish : logic ().m_fishing.creel.kept () > 0;
      state.cook_eggs =
        camp.pan.on ? camp.pan.eggs > 0 : logic ().m_basket.eggs > 0;
      if (const std::optional<std::size_t> beacon = beacon_at_hand ();
          beacon && !logic ().m_beacons[*beacon].burning () &&
          !their_beacon (*beacon) && !camp.resting)
        state.camp = HudState::Camp::light_beacon;
      else if (camp.resting)
        state.camp = HudState::Camp::get_up;
      else if (beside_hammock ())
        state.camp = HudState::Camp::lie_down;
      else if (logic ().m_hammock_at_hand)
        state.camp = HudState::Camp::hang;
      else if (camp.hung && camp.fire_time < -50.0 &&
               hammock_distance (
                 camp.hammock, session ().walker ().position ()) < 12.0f * u::m)
        state.camp = HudState::Camp::light_fire;
    }

    void MoppeGame::add_waymark (HudState& state,
                                 const FrameView& frame,
                                 int width,
                                 int height,
                                 const char* name,
                                 unsigned who,
                                 const Vec3& at,
                                 float near,
                                 float far,
                                 const DisplayColor& tint,
                                 float vanish) {
      if (state.waymark_count >= static_cast<int> (state.waymarks.size ()))
        return;
      const FrameCamera& camera = frame.camera;
      const Vec3 to = at - camera.position;
      const float metres = length (to);
      // Fading in past `near`, and out again toward `vanish`, if set.
      const float shown =
        std::clamp ((metres - near) / (far - near), 0.0f, 1.0f) *
        (vanish > 0.0f
           ? std::clamp ((vanish - metres) / (0.25f * vanish), 0.0f, 1.0f)
           : 1.0f);
      if (shown <= 0.0f)
        return;
      const float across = dot (to, camera.right);
      const float above = dot (to, camera.up);
      const float ahead = dot (to, camera.frame_forward);
      const float half =
        std::tan (0.5f * camera.field_of_view.numerical_value_in (u::rad));
      HudState::Waymark mark;
      mark.name = name;
      mark.who = who;
      mark.position = at;
      mark.metres = metres;
      mark.alpha = shown;
      mark.red = tint.red;
      mark.green = tint.green;
      mark.blue = tint.blue;
      const float x =
        ahead > 0.1f ? across / (ahead * half * camera.aspect) : 0.0f;
      const float y = ahead > 0.1f ? above / (ahead * half) : 0.0f;
      if (ahead > 0.1f && std::fabs (x) < 0.86f && std::fabs (y) < 0.80f) {
        // In view: a small arrow over it, pointing down at it.
        mark.in_view = true;
        mark.x = (0.5f + 0.5f * x) * width;
        mark.y = (0.5f - 0.5f * y) * height - 12.0f;
        mark.angle = -1.5707963f;
      } else {
        // Out of view: at the edge, toward where turning would find it.
        float angle = std::atan2 (above, across);
        if (std::fabs (across) + std::fabs (above) < 1e-3f)
          angle = -1.5707963f;
        mark.angle = angle;
        mark.x = 0.5f * width + 0.43f * width * std::cos (angle);
        mark.y = 0.5f * height - 0.40f * height * std::sin (angle);
      }
      state.waymarks[state.waymark_count++] = mark;
    }

    void MoppeGame::add_waymarks (HudState& state,
                                  const FrameView& frame,
                                  int width,
                                  int height) const {
      const Mode mode = logic ().m_mode;
      const DisplayColor pale (0.96f, 0.96f, 0.96f);
      if (m_companion && mode != M_PASSENGER &&
          m_companion->mode != M_PASSENGER) {
        const CompanionSnapshot& theirs = *m_companion;
        Vec3 at = theirs.bike.position;
        if (theirs.walker)
          at = theirs.walker->position;
        else if (theirs.glider)
          at = theirs.glider->position;
        else if (theirs.mode == M_JEEP)
          at = theirs.jeep.position;
        else if (theirs.mode == M_HELICOPTER)
          at = theirs.helicopter.position + Vec3 (0, 1.0f, 0);
        else if (theirs.mode == M_TRACTOR)
          at = theirs.tractor.position + Vec3 (0, 0.8f, 0);
        else if (theirs.mode == M_CADDY)
          at = theirs.caddy.position + Vec3 (0, 1.0f, 0);
        else if (theirs.mode == M_MOUNTAIN_BIKE)
          at = theirs.mountain_bike.position + Vec3 (0, 1.4f, 0);
        else if (theirs.mode == M_LONG_JOHN)
          at = theirs.long_john.position + Vec3 (0, 1.4f, 0);
        else if (theirs.mode == M_BOAT)
          at = theirs.boat.position + Vec3 (0, 1.2f, 0);
        else if (theirs.mode == M_JET_SKI)
          at = theirs.jet_ski.position + Vec3 (0, 1.3f, 0);
        // In the colour of the jacket they wear.
        DisplayColor jacket = pale;
        for (std::size_t c = 0; c < figure::colours.size (); ++c)
          if (std::string_view (figure::colour_names[c]) == "jacket")
            jacket = mix_display (
              figure::outfit_colour (c, their_outfit ()), pale, 0.35f);
        add_waymark (state,
                     frame,
                     width,
                     height,
                     "companion",
                     HudState::Waymark::Companion,
                     at + Vec3 (0, 2.0f, 0),
                     12.0f,
                     24.0f,
                     jacket);
      }
      // The nearest named place, while it is near but not yet reached.
      {
        const terrain::RoadNetwork& roads = generated_world ().roads ();
        const Vec3 here = frame.hud.subject_position;
        const char* name = nullptr;
        float nearest_m = place_waymark_m, x = 0.0f, z = 0.0f;
        for (std::size_t p = 0;
             p < roads.places.size () && p < m_place_names.size ();
             ++p) {
          const terrain::RoadPlace& place = roads.places[p];
          if (place.kind == terrain::RoadPlace::Kind::Trailhead)
            continue;
          const float dx = terrain::minimum_image_delta (place.x_m - here[0],
                                                         m_map.period_x_m ());
          const float dz = terrain::minimum_image_delta (place.z_m - here[2],
                                                         m_map.period_z_m ());
          if (const float metres = std::hypot (dx, dz); metres < nearest_m) {
            nearest_m = metres;
            name = m_place_names[p].c_str ();
            x = here[0] + dx;
            z = here[2] + dz;
          }
        }
        if (name)
          add_waymark (state,
                       frame,
                       width,
                       height,
                       name,
                       HudState::Waymark::Place,
                       Vec3 (x, ground_height (Vec3 (x, 0, z)) + 4.0f, z),
                       40.0f,
                       70.0f,
                       DisplayColor (1.0f, 0.86f, 0.55f),
                       place_waymark_m);
      }
      // Every machine but the one being driven, and the bike while it
      // hangs under the wing.
      for (int i = 0; i < machine_count; ++i) {
        const Machine machine = static_cast<Machine> (i);
        if (driven_machine (mode) == machine ||
            (machine == Machine::bike && session ().can_drop_bike ()))
          continue;
        add_waymark (
          state,
          frame,
          width,
          height,
          "",
          HudState::Waymark::of (machine),
          session ().machine_body (machine).position +
            Vec3 (0, meters_value (machine_shape (machine).mark_height), 0),
          30.0f,
          50.0f,
          pale);
      }
    }

    HudState MoppeGame::hud_state_for (const FrameHud& reading) {
      HudState state;
      state.speed_kmh = reading.speed_kmh;
      state.boost_ready01 = reading.boost_ready01;
      state.health01 = reading.health01;
      state.odometer_m = reading.odometer_m;
      state.lives = reading.lives;
      state.stars = reading.stars;
      state.score = reading.score;
      state.airtime_s = reading.airtime_s;
      state.spin_degrees = reading.spin_degrees;
      state.landed_airtime_s = reading.landed_airtime_s;
      state.landed_spin_degrees = reading.landed_spin_degrees;
      state.landed_points = reading.landed_points;
      state.landed_clean = reading.landed_clean;
      state.landed_age_s = reading.landed_age_s;
      state.wheelie_m = reading.wheelie_m;
      state.last_wheelie_m = reading.last_wheelie_m;
      state.last_wheelie_points = reading.last_wheelie_points;
      state.best_wheelie_m = reading.best_wheelie_m;
      state.wheelie_age_s = reading.wheelie_age_s;
      state.on_foot = reading.on_foot;
      state.gliding = reading.gliding;
      state.can_deploy_glider = reading.can_deploy_glider;
      state.can_drop_bike = reading.can_drop_bike;
      state.can_mount = reading.can_mount;
      state.can_drive = reading.can_drive;
      state.can_fly = reading.can_fly;
      state.can_drive_tractor = reading.can_drive_tractor;
      state.can_drive_caddy = reading.can_drive_caddy;
      state.can_take_boat = reading.can_take_boat;
      state.can_ride_jet_ski = reading.can_ride_jet_ski;
      state.can_ride_mountain_bike = reading.can_ride_mountain_bike;
      state.can_ride_long_john = reading.can_ride_long_john;
      state.haul = reading.haul;
      state.hauling = reading.hauling;
      state.stuck = reading.stuck;
      state.vertical_speed_mps = reading.vertical_speed_mps;
      state.frame_time_s = reading.frame_time_s;
      state.heading_radians = reading.heading_radians;
      return state;
    }

    void MoppeGame::add_basket_readings (HudState& state) const {
      const Basket& basket = logic ().m_basket;
      state.basket_total = basket.total () + basket.eggs;
      state.basket_eggs = basket.eggs;
      state.reached_name =
        basket.last_egg ? "egg" : mushroom_name (basket.last_kind);
      state.reached_age_s =
        static_cast<float> (logic ().m_total_time - basket.last_reach_time);
      state.reached_refused = basket.last_refused;
      if (logic ().m_mode != M_FOOT)
        return;
      if (const std::optional<std::uint32_t> found =
            m_mushrooms.within_reach (session ().walker ().position (),
                                      session ().walker ().heading ())) {
        const MushroomKind kind = m_mushrooms.site (*found).kind;
        state.mushroom_in_reach = mushroom_name (kind);
        state.mushroom_edible = mushroom_edible (kind);
      } else if (egg_at_hand ()) {
        state.egg_in_reach = true;
        state.basket_takes_egg = basket.eggs < basket_holds_eggs;
      }
    }

    void MoppeGame::paint_map (render::Renderer& r) {
      MOPPE_PROFILE_ZONE ("startup.paint_map");
#ifdef __EMSCRIPTEN__
      // A browser's page has less to spare for one picture.
      const int limit = 2048;
#else
      const int limit = 4096;
#endif
      const int size = std::min (limit, 2 * recipe ().resolution ());
      m_map.set (r,
                 paint_relief_map (surface (),
                                   surface_readings (),
                                   generated_world ().water_surface (),
                                   rivers (),
                                   trail_network (),
                                   generated_world ().roads (),
                                   world ().water_level,
                                   size));
    }

    std::vector<MapSheet::Mark> MoppeGame::map_marks () const {
      using Mark = MapSheet::Mark;
      const DisplayColor dark (0.09f, 0.09f, 0.10f);
      std::vector<Mark> marks;
      const Mode mode = logic ().m_mode;
      marks.push_back ({ .kind = Mark::Kind::Walker,
                         .position = session ().subject_position (),
                         .heading = subject_heading (),
                         .colour = dark,
                         .name = "" });
      if (m_companion && mode != M_PASSENGER &&
          m_companion->mode != M_PASSENGER) {
        const CompanionSnapshot& theirs = *m_companion;
        Vec3 at = theirs.bike.position;
        if (theirs.walker)
          at = theirs.walker->position;
        else if (theirs.glider)
          at = theirs.glider->position;
        else if (theirs.mode == M_JEEP)
          at = theirs.jeep.position;
        else if (theirs.mode == M_HELICOPTER)
          at = theirs.helicopter.position;
        else if (theirs.mode == M_TRACTOR)
          at = theirs.tractor.position;
        else if (theirs.mode == M_CADDY)
          at = theirs.caddy.position;
        else if (theirs.mode == M_MOUNTAIN_BIKE)
          at = theirs.mountain_bike.position;
        else if (theirs.mode == M_LONG_JOHN)
          at = theirs.long_john.position;
        else if (theirs.mode == M_BOAT)
          at = theirs.boat.position;
        else if (theirs.mode == M_JET_SKI)
          at = theirs.jet_ski.position;
        DisplayColor jacket = dark;
        for (std::size_t c = 0; c < figure::colours.size (); ++c)
          if (std::string_view (figure::colour_names[c]) == "jacket")
            jacket = figure::outfit_colour (c, their_outfit ());
        marks.push_back ({ .kind = Mark::Kind::Companion,
                           .position = at,
                           .heading = Vec3 (0, 0, -1),
                           .colour = jacket,
                           .name = "companion" });
      }
      if (mode != M_BIKE && !session ().can_drop_bike ())
        marks.push_back ({ .kind = Mark::Kind::Bike,
                           .position = session ().bike ().position (),
                           .colour = dark,
                           .name = "bike" });
      if (mode != M_JEEP)
        marks.push_back ({ .kind = Mark::Kind::Jeep,
                           .position = session ().jeep ().position (),
                           .colour = dark,
                           .name = "jeep" });
      if (mode != M_TRACTOR)
        marks.push_back ({ .kind = Mark::Kind::Jeep,
                           .position = session ().tractor ().position (),
                           .colour = DisplayColor (0.70f, 0.12f, 0.08f),
                           .name = "tractor" });
      if (mode != M_CADDY)
        marks.push_back ({ .kind = Mark::Kind::Jeep,
                           .position = session ().caddy ().position (),
                           .colour = DisplayColor (0.22f, 0.50f, 0.45f),
                           .name = "Caddy" });
      if (mode != M_MOUNTAIN_BIKE)
        marks.push_back ({ .kind = Mark::Kind::Bike,
                           .position = session ().mountain_bike ().position (),
                           .colour = DisplayColor (0.06f, 0.30f, 0.22f),
                           .name = "mountainbike" });
      if (mode != M_LONG_JOHN)
        marks.push_back ({ .kind = Mark::Kind::Bike,
                           .position = session ().long_john ().position (),
                           .colour = DisplayColor (0.58f, 0.575f, 0.57f),
                           .name = "Long John" });
      if (mode != M_HELICOPTER)
        marks.push_back ({ .kind = Mark::Kind::Jeep,
                           .position = session ().helicopter ().position (),
                           .colour = DisplayColor (0.20f, 0.30f, 0.20f),
                           .name = "helicopter" });
      if (mode != M_BOAT)
        marks.push_back ({ .kind = Mark::Kind::Jeep,
                           .position = session ().boat ().position (),
                           .colour = DisplayColor (0.62f, 0.48f, 0.30f),
                           .name = "boat" });
      if (mode != M_JET_SKI)
        marks.push_back ({ .kind = Mark::Kind::Jeep,
                           .position = session ().jet_ski ().position (),
                           .colour = DisplayColor (0.04f, 0.56f, 0.58f),
                           .name = "jet ski" });
      const Camp& camp = logic ().m_camp;
      if (camp.hung || camp.fire)
        marks.push_back (
          { .kind = Mark::Kind::Camp,
            .position = camp.hung ? hammock_middle (camp.hammock) : camp.hearth,
            .colour = DisplayColor (0.80f, 0.38f, 0.08f),
            .name = "camp" });
      marks.push_back ({ .kind = Mark::Kind::Trailhead,
                         .position = m_home_base_position,
                         .colour = DisplayColor (0.80f, 0.13f, 0.11f),
                         .name = "trailhead" });
      // The road network's places, by name; last, so drawn under the
      // rest.
      const terrain::RoadNetwork& roads = generated_world ().roads ();
      for (std::size_t p = 0;
           p < roads.places.size () && p < m_place_names.size ();
           ++p) {
        const terrain::RoadPlace& place = roads.places[p];
        using Place = terrain::RoadPlace::Kind;
        if (place.kind == Place::Trailhead)
          continue;
        const bool quiet = place.abandoned_years_ago > 0.0f;
        marks.push_back (
          { .kind = place.kind == Place::Landing      ? Mark::Kind::Landing
                    : place.kind == Place::LakeOutlet ? Mark::Kind::Outlet
                                                      : Mark::Kind::Sanctuary,
            .position = Vec3 (place.x_m, place.height_m, place.z_m),
            .colour = place.kind == Place::Sanctuary
                        ? (quiet ? DisplayColor (0.36f, 0.24f, 0.26f)
                                 : DisplayColor (0.56f, 0.12f, 0.16f))
                        : DisplayColor (0.10f, 0.30f, 0.62f),
            .name = m_place_names[p].c_str (),
            .place = true });
      }
      return marks;
    }

    void MoppeGame::add_chart (HudState& state, const FrameView& frame) const {
      if (!m_map.picture ())
        return;
      HudState::Chart& chart = state.chart;
      chart.picture = m_map.picture ();
      chart.period_x_m = m_map.period_x_m ();
      chart.period_z_m = m_map.period_z_m ();
      const Vec3 at = frame.hud.subject_position;
      chart.x_m = at[0];
      chart.z_m = at[2];
      // Ahead is where the camera looks; looking down or up, its top
      // (or its foot) leans the way it faces.
      const Vec3 forward = frame.camera.forward;
      const Vec3 up = frame.camera.up;
      const float lean = forward[1] < 0.0f ? 1.0f : -1.0f;
      chart.ahead_x = forward[0] + lean * up[0];
      chart.ahead_z = forward[2] + lean * up[2];
      const Vec3 facing = frame.hud.subject_heading;
      chart.facing_x = facing[0];
      chart.facing_z = facing[2];
      chart.daylight = frame.lighting.daylight;
      using Kind = MapSheet::Mark::Kind;
      using Pin = HudState::Chart::Pin;
      for (const MapSheet::Mark& mark : map_marks ()) {
        if (mark.kind == Kind::Walker ||
            chart.pin_count >= static_cast<int> (chart.pins.size ()))
          continue;
        Pin& pin = chart.pins[chart.pin_count++];
        pin.kind = mark.kind == Kind::Companion   ? Pin::Kind::Companion
                   : mark.kind == Kind::Bike      ? Pin::Kind::Bike
                   : mark.kind == Kind::Camp      ? Pin::Kind::Camp
                   : mark.kind == Kind::Trailhead ? Pin::Kind::Trailhead
                   : mark.kind == Kind::Landing   ? Pin::Kind::Landing
                   : mark.kind == Kind::Outlet    ? Pin::Kind::Outlet
                   : mark.kind == Kind::Sanctuary ? Pin::Kind::Sanctuary
                                                  : Pin::Kind::Machine;
        pin.x_m = mark.position[0];
        pin.z_m = mark.position[2];
        pin.red = mark.colour.red;
        pin.green = mark.colour.green;
        pin.blue = mark.colour.blue;
      }
    }

    void
    MoppeGame::draw_map (render::Renderer& r, int width_pts, int height_pts) {
      if (!m_map.painted ())
        paint_map (r);
      m_hud_dl.clear ();
      m_hud_text.clear ();
      const platform::Insets safe_insets = platform::safe_insets ();
      m_hud_dl.translate (safe_insets.left, safe_insets.top, 0);
      m_map.draw (m_hud_dl,
                  m_hud_text,
                  m_hud.font (),
                  map_marks (),
                  "MOPPE  \u00b7  SEED " +
                    std::to_string (recipe ().seed ().value),
                  width_pts,
                  height_pts);
    }

    void MoppeGame::draw_opening_titles (int width_pts, int height_pts) {
      if (!m_opening.active ())
        return;
      if (const float veil = m_opening.veil (); veil > 0.002f) {
        render::DrawState over;
        over.blend = true;
        over.depth_test = false;
        over.depth_write = false;
        over.cull = false;
        m_hud_dl.state (over);
        m_hud_dl.lit (false);
        m_hud_dl.fogged (false);
        m_hud_dl.color (0.0f, 0.0f, 0.0f, veil);
        m_hud_dl.begin (render::Prim::Quads);
        m_hud_dl.vertex (-64.0f, -64.0f);
        m_hud_dl.vertex (width_pts + 64.0f, -64.0f);
        m_hud_dl.vertex (width_pts + 64.0f, height_pts + 64.0f);
        m_hud_dl.vertex (-64.0f, height_pts + 64.0f);
        m_hud_dl.end ();
        m_hud_dl.state (render::DrawState ());
      }
      for (const OpeningCaptionView& caption : m_opening.captions ())
        m_hud.draw_title_card (m_hud_text,
                               caption.style == OpeningCaption::Style::Title,
                               *caption.text,
                               caption.alpha,
                               caption.y,
                               width_pts,
                               height_pts);
    }
  }
}
