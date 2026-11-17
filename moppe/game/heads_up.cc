// What the HUD is told each frame and draws: the prompts and readings of
// camp, fishing, and the basket, the waymarks, the chart, the map sheet,
// the trail map, and the quit question.

#include <moppe/game/companionship.hh>
#include <moppe/game/figure.hh>
#include <moppe/game/fishing.hh>
#include <moppe/game/game_session.hh>
#include <moppe/game/generated_world.hh>
#include <moppe/game/heads_up.hh>
#include <moppe/game/opening_director.hh>
#include <moppe/platform/platform.hh>
#include <moppe/profile.hh>

namespace moppe::game {
  void HeadsUp::draw_trail_map (const HudWorld& w,
                                render::DrawList& dl,
                                int width_pts,
                                int height_pts,
                                const Vec3& subject,
                                Vec3 heading) const {
    if (width_pts < 480 || height_pts < 360)
      return;
    const terrain::TerrainDomain& grid = w.world.trails ().domain;
    const auto& alignment = w.world.trails ().alignment.points;
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
    dl.line (map_x + map_size, map_y, map_x + map_size, map_y + map_size, 1.0f);
    dl.line (map_x + map_size, map_y + map_size, map_x, map_y + map_size, 1.0f);
    dl.line (map_x, map_y + map_size, map_x, map_y, 1.0f);

    dl.color (1.0f, 0.58f, 0.12f, 0.96f);
    for (std::size_t point = 0; point < alignment.size (); ++point) {
      const Vec3 a = relative_alignment (alignment[point]);
      Vec3 b = relative_alignment (alignment[(point + 1) % alignment.size ()]);
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

  void HeadsUp::add_fishing_readings (HudState& state, const HudWorld& w) {
    const Fishing& fishing = w.session.logic ().m_fishing;
    const Creel& creel = fishing.creel;
    state.fish_total = creel.kept ();
    const float since =
      seconds_between (w.session.logic ().m_total_time, creel.last_time);
    if (since < 5.0f) {
      const std::string name = fish_name (creel.last.kind);
      if (creel.last_lost) {
        m_prompts.catch_note = "It got away.";
      } else {
        const bool vowel =
          std::string_view ("aeiou").find (name[0]) != std::string::npos;
        m_prompts.catch_note = (vowel ? "An " : "A ") + name + ", " +
                               std::to_string (static_cast<int> (std::lround (
                                 100.0f * meters_value (creel.last.length)))) +
                               " cm!";
      }
      state.catch_note = m_prompts.catch_note.c_str ();
      state.catch_age_s = since;
    }
    if (!game::angler (w.session))
      return;
    const bool afloat =
      w.session.logic ().m_doing == Doing::driving (Machine::boat);
    switch (fishing.line.phase) {
    case Line::Phase::idle:
      if (afloat
            ? game::boat_drifting (w.session, w.around) &&
                game::water_ahead (w.session, w.around)
            : !state.mushroom_in_reach && state.cook == HudState::Cook::none &&
                state.camp != HudState::Camp::lie_down &&
                game::water_ahead (w.session, w.around))
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

  void HeadsUp::add_camp_readings (HudState& state, const HudWorld& w) {
    const Camp& camp = w.session.logic ().m_camp;
    state.resting = camp.resting;
    state.clock_hours = w.sky.clock.numerical_value_in (si::hour);
    if (seconds_between (w.session.logic ().m_total_time, camp.ate_time) <
        6.0f) {
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
      m_prompts.meal_note =
        camp.ate_cooked > 1.4f
          ? "Burnt to a cinder. Still, it is supper."
          : (one ? "A fried " : "Fried ") + meal + " by the fire. Lovely.";
      state.meal = m_prompts.meal_note.c_str ();
      state.meal_age_s =
        seconds_between (w.session.logic ().m_total_time, camp.ate_time);
    }
    // What Space and F do here, and what can be done about the camp
    // without a prompt for it (HudState::thumb).
    if (w.session.logic ().m_doing != m_prompts.thumb_mode) {
      m_prompts.thumb_mode = w.session.logic ().m_doing;
      m_prompts.thumb_from = w.session.logic ().m_total_time;
    }
    // The bike's jets and the boat's tied-back governor are prompted
    // for a while after getting on, until they have been tried once.
    const bool afloat =
      w.session.logic ().m_doing == Doing::driving (Machine::boat);
    const bool boosts =
      w.session.logic ().m_doing == Doing::driving (Machine::bike) || afloat;
    if (boosts && w.session.logic ().m_controls.boost > 0.1f)
      m_prompts.boost_tried[afloat] = true;
    state.thumb_hint = boosts && !m_prompts.boost_tried[afloat] &&
                       w.session.logic ().m_total_time - m_prompts.thumb_from <
                         15.0 * si::second;
    // The wheelie is prompted once the jets' prompt is done with, for
    // half a minute after getting on, until one has been ridden.
    if (w.session.logic ().m_doing == Doing::driving (Machine::bike) &&
        w.session.bike ().wheeling ())
      m_prompts.wheelie_tried = true;
    state.wheelie_hint =
      w.session.logic ().m_doing == Doing::driving (Machine::bike) &&
      !state.thumb_hint && !m_prompts.wheelie_tried &&
      w.session.logic ().m_total_time - m_prompts.thumb_from <
        45.0 * si::second;
    switch (w.session.logic ().m_doing.way) {
    case Doing::Way::foot:
      if (!camp.resting) {
        state.thumb = "Jump";
        state.can_take_down = game::beside_hammock (w.session);
        Vec3 to_fire = w.session.walker ().position () - camp.hearth;
        to_fire[1] = 0.0f;
        state.can_douse_fire = camp.fire && length2 (to_fire) < 3.0f * 3.0f;
        state.can_light_fire =
          !camp.fire &&
          (game::hearth_ahead (w.session, w.around).has_value () ||
           hearth_at_hand (w.places, w.session.walker ().position ()));
        if (const std::optional<std::size_t> beacon =
              game::beacon_at_hand (w.session, w.around)) {
          const bool lit = w.session.logic ().m_beacons[*beacon].burning ();
          state.can_douse_fire = lit;
          state.can_light_fire =
            !lit && !game::their_beacon (w.session.logic (), *beacon);
        }
      }
      break;
    case Doing::Way::glider:
      state.thumb = "Flare";
      break;
    case Doing::Way::passenger:
      state.dismount = "Step out";
      break;
    case Doing::Way::driving: {
      const Machine machine = w.session.logic ().m_doing.machine;
      const MachineKind& kind = machine_kind (machine);
      state.thumb = kind.thumb;
      state.dismount = kind.leave;
      state.skids = kind.skids;
      state.wheelies = kind.wheelies;
      if (machine != Machine::helicopter)
        break;
      state.flying = true;
      state.can_step_out = w.session.can_step_out_of_helicopter ();
      // Letting go is prompted only once the load hangs near the
      // ground, though the key lets it fall from any height.
      if (w.session.logic ().m_sling.hooked &&
          w.session.helicopter ().altitude () < sling_hang + 4.0f * u::m)
        state.slung = machine_kind (w.session.logic ().m_sling.load).name;
      if (const std::optional<Machine> under = w.session.hook_within_reach ())
        state.hook = machine_kind (*under).name;
      if (w.session.logic ().m_sling.hooked)
        state.dismount = "Let go";
      else if (state.hook)
        state.dismount = "Hook on";
      else if (!state.can_step_out)
        state.dismount = nullptr;
      break;
    }
    }
    if (w.session.logic ().m_doing != Doing::on_foot () ||
        state.mushroom_in_reach)
      return;
    state.cook = game::supper_at_hand (w.session, w.around);
    state.cook_fish = camp.pan.on
                        ? camp.pan.has_fish
                        : w.session.logic ().m_fishing.creel.kept () > 0;
    state.cook_eggs =
      camp.pan.on ? camp.pan.eggs > 0 : w.session.logic ().m_basket.eggs > 0;
    if (const std::optional<std::size_t> beacon =
          game::beacon_at_hand (w.session, w.around);
        beacon && !w.session.logic ().m_beacons[*beacon].burning () &&
        !game::their_beacon (w.session.logic (), *beacon) && !camp.resting)
      state.camp = HudState::Camp::light_beacon;
    else if (camp.resting)
      state.camp = HudState::Camp::get_up;
    else if (game::beside_hammock (w.session))
      state.camp = HudState::Camp::lie_down;
    else if (w.session.logic ().m_hammock_at_hand)
      state.camp = HudState::Camp::hang;
    else if (camp.hung && !hearth_laid (camp) &&
             hammock_distance (camp.hammock, w.session.walker ().position ()) <
               12.0f * u::m)
      state.camp = HudState::Camp::light_fire;
  }

  void HeadsUp::add_waymark (HudState& state,
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

  void HeadsUp::add_waymarks (HudState& state,
                              const HudWorld& w,
                              const FrameView& frame,
                              int width,
                              int height) const {
    const Doing mode = w.session.logic ().m_doing;
    const DisplayColor pale (0.96f, 0.96f, 0.96f);
    if (w.company.companion && mode != Doing::riding_along () &&
        w.company.companion->mode != Doing::riding_along ()) {
      const CompanionSnapshot& theirs = *w.company.companion;
      Vec3 at = theirs.bike.position;
      if (theirs.walker)
        at = theirs.walker->position;
      else if (theirs.glider)
        at = theirs.glider->position;
      else if (theirs.mode == Doing::driving (Machine::jeep))
        at = theirs.jeep.position;
      else if (theirs.mode == Doing::driving (Machine::helicopter))
        at = theirs.helicopter.position + Vec3 (0, 1.0f, 0);
      else if (theirs.mode == Doing::driving (Machine::tractor))
        at = theirs.tractor.position + Vec3 (0, 0.8f, 0);
      else if (theirs.mode == Doing::driving (Machine::caddy))
        at = theirs.caddy.position + Vec3 (0, 1.0f, 0);
      else if (theirs.mode == Doing::driving (Machine::mountain_bike))
        at = theirs.mountain_bike.position + Vec3 (0, 1.4f, 0);
      else if (theirs.mode == Doing::driving (Machine::long_john))
        at = theirs.long_john.position + Vec3 (0, 1.4f, 0);
      else if (theirs.mode == Doing::driving (Machine::boat))
        at = theirs.boat.position + Vec3 (0, 1.2f, 0);
      else if (theirs.mode == Doing::driving (Machine::jet_ski))
        at = theirs.jet_ski.position + Vec3 (0, 1.3f, 0);
      // In the colour of the jacket they wear.
      DisplayColor jacket = pale;
      for (std::size_t c = 0; c < figure::colours.size (); ++c)
        if (std::string_view (figure::colour_names[c]) == "jacket")
          jacket = mix_display (
            figure::outfit_colour (c, w.company.their_outfit ()), pale, 0.35f);
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
      const terrain::RoadNetwork& roads = w.world.roads ();
      const Vec3 here = frame.hud.subject_position;
      const char* name = nullptr;
      float nearest_m = place_waymark_m, x = 0.0f, z = 0.0f;
      for (std::size_t p = 0;
           p < roads.places.size () && p < w.place_names.size ();
           ++p) {
        const terrain::RoadPlace& place = roads.places[p];
        if (place.kind == terrain::RoadPlace::Kind::Trailhead)
          continue;
        const float dx =
          terrain::minimum_image_delta (place.x_m - here[0], map.period_x_m ());
        const float dz =
          terrain::minimum_image_delta (place.z_m - here[2], map.period_z_m ());
        if (const float metres = std::hypot (dx, dz); metres < nearest_m) {
          nearest_m = metres;
          name = w.place_names[p].c_str ();
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
                     Vec3 (x, w.ground.height (Vec3 (x, 0, z)) + 4.0f, z),
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
          (machine == Machine::bike && w.session.can_drop_bike ()))
        continue;
      add_waymark (
        state,
        frame,
        width,
        height,
        "",
        HudState::Waymark::of (machine),
        w.session.machine_body (machine).position +
          Vec3 (0, meters_value (machine_kind (machine).mark_height), 0),
        30.0f,
        50.0f,
        pale);
    }
  }

  HudState HeadsUp::hud_state_for (const FrameHud& reading) {
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
    if (reading.at_hand) {
      state.take = machine_kind (*reading.at_hand).take;
      state.at_hand = machine_kind (*reading.at_hand).at_hand;
    }
    state.haul = reading.haul;
    state.hauling = reading.hauling;
    state.stuck = reading.stuck;
    state.vertical_speed_mps = reading.vertical_speed_mps;
    state.frame_time_s = reading.frame_time_s;
    state.heading_radians = reading.heading_radians;
    return state;
  }

  void HeadsUp::add_basket_readings (HudState& state, const HudWorld& w) {
    const Basket& basket = w.session.logic ().m_basket;
    state.basket_total = basket.total () + basket.eggs;
    state.basket_eggs = basket.eggs;
    state.reached_name =
      basket.last_egg ? "egg" : mushroom_name (basket.last_kind);
    state.reached_age_s =
      seconds_between (w.session.logic ().m_total_time, basket.last_reach_time);
    state.reached_refused = basket.last_refused;
    if (w.session.logic ().m_doing != Doing::on_foot ())
      return;
    if (const std::optional<std::uint32_t> found = w.mushrooms.within_reach (
          w.session.walker ().position (), w.session.walker ().heading ())) {
      const MushroomKind kind = w.mushrooms.site (*found).kind;
      state.mushroom_in_reach = mushroom_name (kind);
      state.mushroom_edible = mushroom_edible (kind);
    } else if (game::egg_at_hand (w.session, w.around)) {
      state.egg_in_reach = true;
      state.basket_takes_egg = basket.eggs < basket_holds_eggs;
    }
  }

  void HeadsUp::paint_map (render::Renderer& r, const GeneratedWorld& world) {
    MOPPE_PROFILE_ZONE ("startup.paint_map");
#ifdef __EMSCRIPTEN__
    // A browser's page has less to spare for one picture.
    const int limit = 2048;
#else
    const int limit = 4096;
#endif
    const int size = std::min (limit, 2 * world.recipe ().resolution ());
    map.set (r,
             paint_relief_map (world.surface (),
                               world.readings (),
                               world.water_surface (),
                               world.rivers (),
                               world.trails (),
                               world.roads (),
                               world.params ().water_level,
                               size));
  }

  std::vector<MapSheet::Mark> HeadsUp::map_marks (const HudWorld& w) {
    using Mark = MapSheet::Mark;
    const DisplayColor dark (0.09f, 0.09f, 0.10f);
    std::vector<Mark> marks;
    const Doing mode = w.session.logic ().m_doing;
    marks.push_back ({ .kind = Mark::Kind::Walker,
                       .position = w.session.subject_position (),
                       .heading = w.session.subject_heading (),
                       .colour = dark,
                       .name = "" });
    if (w.company.companion && mode != Doing::riding_along () &&
        w.company.companion->mode != Doing::riding_along ()) {
      const CompanionSnapshot& theirs = *w.company.companion;
      Vec3 at = theirs.bike.position;
      if (theirs.walker)
        at = theirs.walker->position;
      else if (theirs.glider)
        at = theirs.glider->position;
      else if (theirs.mode == Doing::driving (Machine::jeep))
        at = theirs.jeep.position;
      else if (theirs.mode == Doing::driving (Machine::helicopter))
        at = theirs.helicopter.position;
      else if (theirs.mode == Doing::driving (Machine::tractor))
        at = theirs.tractor.position;
      else if (theirs.mode == Doing::driving (Machine::caddy))
        at = theirs.caddy.position;
      else if (theirs.mode == Doing::driving (Machine::mountain_bike))
        at = theirs.mountain_bike.position;
      else if (theirs.mode == Doing::driving (Machine::long_john))
        at = theirs.long_john.position;
      else if (theirs.mode == Doing::driving (Machine::boat))
        at = theirs.boat.position;
      else if (theirs.mode == Doing::driving (Machine::jet_ski))
        at = theirs.jet_ski.position;
      DisplayColor jacket = dark;
      for (std::size_t c = 0; c < figure::colours.size (); ++c)
        if (std::string_view (figure::colour_names[c]) == "jacket")
          jacket = figure::outfit_colour (c, w.company.their_outfit ());
      marks.push_back ({ .kind = Mark::Kind::Companion,
                         .position = at,
                         .heading = Vec3 (0, 0, -1),
                         .colour = jacket,
                         .name = "companion" });
    }
    if (mode != Doing::driving (Machine::bike) && !w.session.can_drop_bike ())
      marks.push_back ({ .kind = Mark::Kind::Bike,
                         .position = w.session.bike ().position (),
                         .colour = dark,
                         .name = "bike" });
    if (mode != Doing::driving (Machine::jeep))
      marks.push_back ({ .kind = Mark::Kind::Jeep,
                         .position = w.session.jeep ().position (),
                         .colour = dark,
                         .name = "jeep" });
    if (mode != Doing::driving (Machine::tractor))
      marks.push_back ({ .kind = Mark::Kind::Jeep,
                         .position = w.session.tractor ().position (),
                         .colour = DisplayColor (0.70f, 0.12f, 0.08f),
                         .name = "tractor" });
    if (mode != Doing::driving (Machine::caddy))
      marks.push_back ({ .kind = Mark::Kind::Jeep,
                         .position = w.session.caddy ().position (),
                         .colour = DisplayColor (0.22f, 0.50f, 0.45f),
                         .name = "Caddy" });
    if (mode != Doing::driving (Machine::mountain_bike))
      marks.push_back ({ .kind = Mark::Kind::Bike,
                         .position = w.session.mountain_bike ().position (),
                         .colour = DisplayColor (0.06f, 0.30f, 0.22f),
                         .name = "mountainbike" });
    if (mode != Doing::driving (Machine::long_john))
      marks.push_back ({ .kind = Mark::Kind::Bike,
                         .position = w.session.long_john ().position (),
                         .colour = DisplayColor (0.58f, 0.575f, 0.57f),
                         .name = "Long John" });
    if (mode != Doing::driving (Machine::helicopter))
      marks.push_back ({ .kind = Mark::Kind::Jeep,
                         .position = w.session.helicopter ().position (),
                         .colour = DisplayColor (0.20f, 0.30f, 0.20f),
                         .name = "helicopter" });
    if (mode != Doing::driving (Machine::boat))
      marks.push_back ({ .kind = Mark::Kind::Jeep,
                         .position = w.session.boat ().position (),
                         .colour = DisplayColor (0.62f, 0.48f, 0.30f),
                         .name = "boat" });
    if (mode != Doing::driving (Machine::jet_ski))
      marks.push_back ({ .kind = Mark::Kind::Jeep,
                         .position = w.session.jet_ski ().position (),
                         .colour = DisplayColor (0.04f, 0.56f, 0.58f),
                         .name = "jet ski" });
    const Camp& camp = w.session.logic ().m_camp;
    if (camp.hung || camp.fire)
      marks.push_back (
        { .kind = Mark::Kind::Camp,
          .position = camp.hung ? hammock_middle (camp.hammock) : camp.hearth,
          .colour = DisplayColor (0.80f, 0.38f, 0.08f),
          .name = "camp" });
    marks.push_back ({ .kind = Mark::Kind::Trailhead,
                       .position = w.home_base,
                       .colour = DisplayColor (0.80f, 0.13f, 0.11f),
                       .name = "trailhead" });
    // The road network's places, by name; last, so drawn under the
    // rest.
    const terrain::RoadNetwork& roads = w.world.roads ();
    for (std::size_t p = 0;
         p < roads.places.size () && p < w.place_names.size ();
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
          .name = w.place_names[p].c_str (),
          .place = true });
    }
    return marks;
  }

  void HeadsUp::add_chart (HudState& state,
                           const FrameView& frame,
                           const HudWorld& w) const {
    if (!map.picture ())
      return;
    HudState::Chart& chart = state.chart;
    chart.picture = map.picture ();
    chart.period_x_m = map.period_x_m ();
    chart.period_z_m = map.period_z_m ();
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
    for (const MapSheet::Mark& mark : map_marks (w)) {
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

  void HeadsUp::draw_map (render::Renderer& r,
                          const HudWorld& w,
                          int width_pts,
                          int height_pts) {
    if (!map.painted ())
      paint_map (r, w.world);
    dl.clear ();
    text.clear ();
    const platform::Insets safe_insets = platform::safe_insets ();
    dl.translate (safe_insets.left, safe_insets.top, 0);
    map.draw (dl,
              text,
              hud.font (),
              map_marks (w),
              "MOPPE  \u00b7  SEED " +
                std::to_string (w.world.recipe ().seed ().value),
              width_pts,
              height_pts);
  }

  void HeadsUp::draw (render::Renderer& r,
                      const HudWorld& w,
                      const OpeningDirector& opening,
                      const FrameView& frame) {
    const FrameVisibility& visibility = frame.visibility;

    // HUD, kept inside the safe area (notch / home indicator).
    dl.clear ();
    text.clear ();
    const platform::Insets safe_insets = platform::safe_insets ();
    dl.translate (safe_insets.left, safe_insets.top, 0);
    const int hud_width =
      r.width_pts () - (int)(safe_insets.left + safe_insets.right);
    const int hud_height =
      r.height_pts () - (int)(safe_insets.top + safe_insets.bottom);
    if (visibility.cinematic_hud) {
      opening.draw_titles (hud, dl, text, hud_width, hud_height);
      hud.draw_ride_prompt (
        text, frame.overlay.cinematic_prompt_alpha, hud_width, hud_height);
    } else if (visibility.game_hud) {
      HudState hud_state = hud_state_for (frame.hud);
      add_basket_readings (hud_state, w);
      add_camp_readings (hud_state, w);
      add_fishing_readings (hud_state, w);
      w.company.add_readings (hud_state, w.session);
      add_waymarks (hud_state, w, frame, hud_width, hud_height);
      add_chart (hud_state, frame, w);
      hud.draw (dl, text, hud_state, hud_width, hud_height);
      if (hud.diagnostics ())
        draw_trail_map (w,
                        dl,
                        hud_width,
                        hud_height,
                        frame.hud.subject_position,
                        frame.hud.subject_heading);
      if (map.open ())
        draw_map (r, w, hud_width, hud_height);
      if (quit_asked)
        hud.draw_quit_question (dl, text, hud_width, hud_height);
    }
    hud.publish_offers ();
    text.translate (safe_insets.left, safe_insets.top);

    // Even a clean inspection capture needs this empty HUD pass: it is
    // also the final post-chain composite into the drawable.
    r.draw_hud_text (text);
    r.draw_hud (dl);
  }
}
