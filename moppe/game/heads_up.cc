// What the HUD is told each frame and draws: the prompts and readings of
// camp, fishing, and the basket, the waymarks, the chart, the map sheet,
// the trail map, and the quit question.

#include <moppe/game/affordances.hh>
#include <moppe/game/bus_render.hh>
#include <moppe/game/camp.hh>
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
  namespace {
    // Where the other player is: on foot, in the air, or in the machine
    // they drive, and, `over_driver`, as high over it as they sit.
    Vec3 whereabouts (const CompanionSnapshot& theirs, bool over_driver) {
      if (theirs.walker)
        return position_value (theirs.walker->position);
      if (theirs.glider)
        return position_value (theirs.glider->position);
      const Machine machine =
        driven_machine (theirs.mode).value_or (Machine::bike);
      const Vec3 at =
        visit (theirs.fleet, machine, [] (Machine, const auto& pose) -> Vec3 {
          return position_value (pose.position);
        });
      if (!over_driver)
        return at;
      return at +
             Vec3 (0, meters_value (machine_kind (machine).driver_mark), 0);
    }

    // The bus the player rides, as it is now.
    std::optional<BusPose> ridden_bus (const HudWorld& w) {
      const std::vector<BusPose> buses =
        w.bus.where (bus_time (w.session.logic ()));
      const std::size_t k = w.session.logic ().m_bus;
      if (w.session.logic ().m_doing != Doing::on_the_bus () ||
          k >= buses.size ())
        return std::nullopt;
      return buses[k];
    }

    // A name at the head of a sentence.
    std::string first_up (std::string_view name) {
      std::string word (name);
      if (!word.empty () && word[0] >= 'a' && word[0] <= 'z')
        word[0] = static_cast<char> (word[0] - 'a' + 'A');
      return word;
    }

    // How the HUD words an affordance: with a key for it, and without.
    std::pair<std::string, std::string> worded (const Affordance& a,
                                                const HudWorld& w) {
      using Verb = Affordance::Verb;
      using Words = std::pair<std::string, std::string>;
      const GameLogicState& logic = w.session.logic ();
      const auto same = [] (std::string words) {
        return Words { words, words };
      };
      const std::string name = machine_kind (a.machine).name;
      switch (a.verb) {
      case Verb::let_go_rope:
        return same ("Let go of " + name);
      case Verb::tend_line:
        switch (logic.m_fishing.line.phase) {
        case Line::Phase::waiting:
          return { "Reel in", "Waiting for a bite" };
        case Line::Phase::biting:
          return { "Strike!", "A bite!" };
        default:
          return same ("Reeling it in");
        }
      case Verb::pick: {
        const std::string kind =
          mushroom_name (w.mushrooms.site (a.object).kind);
        if (a.refused)
          return same (first_up (kind) + " -- poisonous, leave it be");
        return { "Pick the " + kind, first_up (kind) + " within reach" };
      }
      case Verb::take_egg:
        if (a.refused)
          return same ("The basket holds no more eggs");
        return { "Take the egg", "An egg in the nest" };
      case Verb::cook: {
        // A fish goes in first, then eggs, then mushrooms.
        const Camp& camp = logic.m_camp;
        const bool fish =
          camp.pan.on ? camp.pan.has_fish : logic.m_fishing.creel.kept () > 0;
        const bool eggs =
          camp.pan.on ? camp.pan.eggs > 0 : logic.m_basket.eggs > 0;
        switch (supper_at_hand (w.session, w.around)) {
        case Supper::fry:
          return fish   ? Words { "Fry the fish", "Fish to fry" }
                 : eggs ? Words { "Fry the eggs", "Eggs to fry" }
                        : Words { "Fry the mushrooms", "Mushrooms to fry" };
        case Supper::frying:
          return same (fish   ? "The fish is frying"
                       : eggs ? "The eggs are frying"
                              : "The mushrooms are frying");
        case Supper::eat:
          return { "Eat", "Supper is ready" };
        case Supper::eat_burnt:
          return { "Eat them anyway", "Supper is burnt" };
        case Supper::none:
          break;
        }
        return same ("");
      }
      case Verb::get_up:
        return same ("Get up");
      case Verb::lie_down:
        return { "Lie down", "The hammock" };
      case Verb::hang_hammock:
        return { "Hang the hammock", "Two good trees for the hammock" };
      case Verb::take_down_hammock:
        return same ("Take down the hammock");
      case Verb::tend_beacon:
        return { "Light the beacon", "The beacon's fire is laid" };
      case Verb::light_fire:
        return same ("Light a fire");
      case Verb::douse_fire:
        return same ("Put out the fire");
      case Verb::cast:
        return { "Cast a line", "Water to fish" };
      case Verb::drop_bike:
        return same ("Release the Trail 90");
      case Verb::deploy_glider:
        return { "Deploy glider", "Glider ready" };
      case Verb::let_load_go:
        return { "Let " + name + " go", first_up (name) + " is on the line" };
      case Verb::hook:
        return { "Hook " + name, first_up (name) + " is under the hook" };
      case Verb::haul:
        // The boat and the jet ski are dragged the other way, back into
        // the water.
        if (a.machine == Machine::boat)
          return { "Drag the boat by its handle", "The boat is aground" };
        if (a.machine == Machine::jet_ski)
          return { "Drag the jet ski by its bow", "The jet ski is aground" };
        return { "Haul " + name + " out", first_up (name) + " is stuck" };
      case Verb::ride_along:
        return { "Ride along in " + name, "A seat is free in " + name };
      case Verb::take:
        return { machine_kind (a.machine).take,
                 machine_kind (a.machine).at_hand };
      case Verb::step_out:
        return same (machine_kind (a.machine).leave);
      case Verb::board_bus:
        return { "Board the bus", "The bus" };
      case Verb::leave_bus: {
        const std::optional<BusPose> pose = ridden_bus (w);
        if (!pose)
          return same ("Get off");
        if (pose->at_stop)
          return same ("Get off at " + w.bus.stops ()[*pose->at_stop].name);
        const std::string next = w.bus.stops ()[pose->next_stop].name;
        if (logic.m_bus_alight)
          return { "Stay on", "Getting off at " + next };
        return { "Get off at " + next, "Next stop: " + next };
      }
      case Verb::stuck:
        return same ("Stuck! Keep at it, or step out and haul");
      }
      return same ("");
    }
  }

  void HeadsUp::add_prompt (HudState& state, const HudWorld& w) {
    using Verb = Affordance::Verb;
    const std::vector<Affordance> here =
      affordances (w.session, *w.around.surface, &w.around);
    for (const Affordance& a : here) {
      if (!a.prompted)
        continue;
      std::tie (m_prompts.keyed, m_prompts.unkeyed) = worded (a, w);
      // What will not do, and what goes on by itself, shows no key.
      const bool keyless =
        a.refused ||
        (a.verb == Verb::tend_line &&
         w.session.logic ().m_fishing.line.phase == Line::Phase::playing);
      state.prompt_key = keyless ? std::nullopt : a.button;
      state.prompt_keyed = m_prompts.keyed.c_str ();
      state.prompt_unkeyed = m_prompts.unkeyed.c_str ();
      break;
    }
    // What the fire and hammock keys do without a prompt for it.
    const bool fire = w.session.logic ().m_camp.fire;
    for (const Affordance& a : here)
      switch (a.verb) {
      case Verb::take_down_hammock:
        state.can_take_down = true;
        break;
      case Verb::douse_fire:
        state.can_douse_fire = true;
        break;
      case Verb::tend_beacon:
        if (w.session.logic ().m_beacons[a.object].burning ())
          state.can_douse_fire = true;
        else
          state.can_light_fire = !a.refused;
        break;
      case Verb::light_fire:
        // A fire burning elsewhere is moved only by its own key.
        state.can_light_fire = !a.refused && !fire;
        break;
      default:
        break;
      }
  }

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
    const float home_x = plan_value (alignment.front ()).x ();
    const float home_z = plan_value (alignment.front ()).z ();
    const auto wrap_delta = [] (float delta, float period) {
      if (delta > period * 0.5f)
        delta -= period;
      if (delta < -period * 0.5f)
        delta += period;
      return delta;
    };
    const auto relative_alignment = [&] (const plan_position_t& point) {
      return Vec3 (
        plan_value (point).x () - home_x, 0, plan_value (point).z () - home_z);
    };

    Vec3 low (0, 0, 0);
    Vec3 high (0, 0, 0);
    for (const plan_position_t& alignment_point : alignment) {
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
        camp.ate_cooked > 1.4f * proportion[mp_units::one]
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
      }
      break;
    case Doing::Way::glider:
      state.thumb = "Flare";
      break;
    case Doing::Way::passenger:
      state.dismount = "Step out";
      break;
    case Doing::Way::bus:
      state.dismount = "Get off";
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
      if (w.session.logic ().m_sling.hooked)
        state.dismount = "Let go";
      else if (w.session.hook_within_reach ())
        state.dismount = "Hook on";
      else if (!state.can_step_out)
        state.dismount = nullptr;
      break;
    }
    }
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
    const Vec3 to = at - position_value (camera.position);
    const float metres = length (to);
    // Fading in past `near`, and out again toward `vanish`, if set.
    const float shown =
      std::clamp ((metres - near) / (far - near), 0.0f, 1.0f) *
      (vanish > 0.0f
         ? std::clamp ((vanish - metres) / (0.25f * vanish), 0.0f, 1.0f)
         : 1.0f);
    if (shown <= 0.0f)
      return;
    const float across = dot (to, direction_value (camera.right));
    const float above = dot (to, direction_value (camera.up));
    const float ahead = dot (to, direction_value (camera.frame_forward));
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
      ahead > 0.1f
        ? across / (ahead * half * camera.aspect.numerical_value_in (one))
        : 0.0f;
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
    if (w.company.companion && !passenger_in (mode) &&
        !passenger_in (w.company.companion->mode)) {
      const CompanionSnapshot& theirs = *w.company.companion;
      const Vec3 at = whereabouts (theirs, true);
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
      const Vec3 here = position_value (frame.hud.subject_position);
      const char* name = nullptr;
      float nearest_m = place_waymark_m, x = 0.0f, z = 0.0f;
      for (std::size_t p = 0;
           p < roads.places.size () && p < w.place_names.size ();
           ++p) {
        const terrain::RoadPlace& place = roads.places[p];
        if (place.kind == terrain::RoadPlace::Kind::Trailhead)
          continue;
        const plan_position_t& at = place.at;
        const float dx = terrain::minimum_image_delta (
          plan_value (at).x () - here[0], map.period_x_m ());
        const float dz = terrain::minimum_image_delta (
          plan_value (at).z () - here[2], map.period_z_m ());
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
    state.speed_kmh =
      reading.speed.numerical_value_in (si::kilo<si::metre> / si::hour);
    state.boost_ready01 = reading.boost_ready.numerical_value_in (one);
    state.health01 = reading.health.numerical_value_in (one);
    state.odometer_m = meters_value (reading.odometer);
    state.lives = reading.lives;
    state.stars = reading.stars;
    state.score = reading.score;
    state.airtime_s = seconds_value (reading.airtime);
    state.spin_degrees = reading.spin.numerical_value_in (u::deg);
    state.landed_airtime_s = seconds_value (reading.landed_airtime);
    state.landed_spin_degrees = reading.landed_spin.numerical_value_in (u::deg);
    state.landed_points = reading.landed_points;
    state.landed_clean = reading.landed_clean;
    state.landed_age_s = seconds_value (reading.landed_age);
    state.wheelie_m = meters_value (reading.wheelie);
    state.last_wheelie_m = meters_value (reading.last_wheelie);
    state.last_wheelie_points = reading.last_wheelie_points;
    state.best_wheelie_m = meters_value (reading.best_wheelie);
    state.wheelie_age_s = seconds_value (reading.wheelie_age);
    state.on_foot = reading.on_foot;
    state.gliding = reading.gliding;
    state.vertical_speed_mps = speed_value (reading.vertical_speed);
    state.frame_time_s = seconds_value (reading.frame_time);
    state.heading_radians = reading.heading.numerical_value_in (u::rad);
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
    if (w.company.companion && !passenger_in (mode) &&
        !passenger_in (w.company.companion->mode)) {
      const CompanionSnapshot& theirs = *w.company.companion;
      const Vec3 at = whereabouts (theirs, false);
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
      marks.push_back ({ .kind = Mark::Kind::Camp,
                         .position = camp.hung ? hammock_middle (camp.hammock)
                                               : position_value (camp.hearth),
                         .colour = DisplayColor (0.80f, 0.38f, 0.08f),
                         .name = "camp" });
    // The buses where the timetable has them, and their stops; the
    // stops last of all, so drawn under everything.
    for (const BusPose& bus : w.bus.where (bus_time (w.session.logic ())))
      marks.push_back ({ .kind = Mark::Kind::Bus,
                         .position = position_value (bus.position),
                         .heading = direction_value (bus.forward),
                         .colour = DisplayColor (0.66f, 0.09f, 0.07f),
                         .name = "bus" });
    using Place = terrain::RoadPlace::Kind;
    marks.push_back ({ .kind = Mark::Kind::Place,
                       .position = w.home_base,
                       .colour = place_kind (Place::Trailhead).ink,
                       .name = "trailhead" });
    // The road network's places, by name; last, so drawn under the
    // rest.
    const terrain::RoadNetwork& roads = w.world.roads ();
    for (std::size_t p = 0;
         p < roads.places.size () && p < w.place_names.size ();
         ++p) {
      const terrain::RoadPlace& place = roads.places[p];
      const PlaceKind& kind = place_kind (place.kind);
      if (!kind.named)
        continue;
      const bool quiet = place.abandoned > age_t::zero ();
      marks.push_back ({ .kind = Mark::Kind::Place,
                         .position = position_value (
                           terrain::at_elevation (place.at, place.height)),
                         .colour = quiet ? kind.quiet_ink : kind.ink,
                         .name = w.place_names[p].c_str (),
                         .place = place.kind });
    }
    // A stop called after a place it stands at is named by the place.
    for (const BusStop& stop : w.bus.stops ())
      marks.push_back ({ .kind = Mark::Kind::BusStop,
                         .position = position_value (stop.sign),
                         .heading = direction_value (stop.forward),
                         .colour = DisplayColor (0.97f, 0.80f, 0.12f),
                         .name = std::ranges::find (w.place_names, stop.name) !=
                                     w.place_names.end ()
                                   ? ""
                                   : stop.name.c_str () });
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
    const Vec3 at = position_value (frame.hud.subject_position);
    chart.x_m = at[0];
    chart.z_m = at[2];
    // Ahead is where the camera looks; looking down or up, its top
    // (or its foot) leans the way it faces.
    const Vec3 forward = direction_value (frame.camera.forward);
    const Vec3 up = direction_value (frame.camera.up);
    const float lean = forward[1] < 0.0f ? 1.0f : -1.0f;
    chart.ahead_x = forward[0] + lean * up[0];
    chart.ahead_z = forward[2] + lean * up[2];
    const Vec3 facing = direction_value (frame.hud.subject_heading);
    chart.facing_x = facing[0];
    chart.facing_z = facing[2];
    chart.daylight = frame.lighting.daylight.numerical_value_in (one);
    using Kind = MapSheet::Mark::Kind;
    using Pin = HudState::Chart::Pin;
    for (const MapSheet::Mark& mark : map_marks (w)) {
      if (mark.kind == Kind::Walker ||
          chart.pin_count >= static_cast<int> (chart.pins.size ()))
        continue;
      Pin& pin = chart.pins[chart.pin_count++];
      pin.kind = mark.kind == Kind::Companion ? Pin::Kind::Companion
                 : mark.kind == Kind::Bike    ? Pin::Kind::Bike
                 : mark.kind == Kind::Camp    ? Pin::Kind::Camp
                 : mark.kind == Kind::Place   ? Pin::Kind::Place
                 : mark.kind == Kind::Bus     ? Pin::Kind::Bus
                 : mark.kind == Kind::BusStop ? Pin::Kind::BusStop
                                              : Pin::Kind::Machine;
      pin.place = mark.place;
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
    dl.translate (safe_insets.left.numerical_value_in (platform::view_point),
                  safe_insets.top.numerical_value_in (platform::view_point),
                  0);
    map.draw (dl,
              text,
              hud.font (),
              map_marks (w),
              "MOPPE  \u00b7  SEED " +
                std::to_string (w.world.recipe ().seed ().value),
              width_pts,
              height_pts);
  }

  void HeadsUp::add_bus_board (const HudWorld& w,
                               const FrameView& frame,
                               int width,
                               int height) {
    constexpr float shown_m = 40.0f, fading_m = 25.0f;
    const render::Font* font = hud.font ();
    if (!font)
      return;
    const Vec3 eye = w.session.subject_position ();
    const float period_x = w.places.period_x, period_z = w.places.period_z;
    const std::optional<std::size_t> stop =
      bus_stop_near (w.bus,
                     position (eye),
                     shown_m * u::m,
                     plan_extent (period_x * u::m, period_z * u::m));
    if (!stop)
      return;
    const BusStop& at = w.bus.stops ()[*stop];
    const position_t& sign = at.sign;
    const float dx = terrain::minimum_image_delta (
      position_value (sign)[0] - eye[0], period_x);
    const float dz = terrain::minimum_image_delta (
      position_value (sign)[2] - eye[2], period_z);
    const float shown = std::clamp (
      (shown_m - std::hypot (dx, dz)) / (shown_m - fading_m), 0.0f, 1.0f);
    // Its foot over the bus's roof and the luggage on its rack, where
    // the camera sees it.
    const FrameCamera& camera = frame.camera;
    const Vec3 foot =
      Vec3 (eye[0] + dx, position_value (sign)[1] + 3.4f, eye[2] + dz);
    const Vec3 to = foot - position_value (camera.position);
    const float ahead = dot (to, direction_value (camera.frame_forward));
    if (ahead < 0.5f || shown <= 0.0f)
      return;
    const float half =
      std::tan (0.5f * camera.field_of_view.numerical_value_in (u::rad));
    const float x =
      (0.5f + 0.5f * dot (to, direction_value (camera.right)) /
                (ahead * half * camera.aspect.numerical_value_in (one))) *
      static_cast<float> (width);
    const float y =
      (0.5f - 0.5f * dot (to, direction_value (camera.up)) / (ahead * half)) *
      static_cast<float> (height);
    render::TextStyle title;
    title.size = 20.0f;
    render::TextStyle small;
    small.size = 15.0f;
    small.tracking = 0.03f;
    const std::string due =
      bus_due_words (w.bus.due (*stop, bus_time (w.session.logic ())));
    const DisplayColor ink (1.0f, 0.86f, 0.32f);
    for (const bool shade : { true, false }) {
      render::TextStyle t = title, d = small;
      t.red = shade ? 0.0f : ink.red;
      t.green = shade ? 0.0f : ink.green;
      t.blue = shade ? 0.0f : ink.blue;
      d.red = d.green = d.blue = shade ? 0.0f : 1.0f;
      t.alpha = (shade ? 0.5f : 1.0f) * shown;
      d.alpha = (shade ? 0.5f : 0.95f) * shown;
      if (shade)
        t.softness = d.softness = 5.0f;
      font->draw (text, x, y - 23.0f, at.name, t, render::TextAlign::Center);
      font->draw (text, x, y - 4.0f, due, d, render::TextAlign::Center);
    }
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
    const auto in_points = [] (platform::view_points_t length) {
      return length.numerical_value_in (platform::view_point);
    };
    dl.translate (in_points (safe_insets.left), in_points (safe_insets.top), 0);
    const int hud_width =
      r.width_pts () - (int)in_points (safe_insets.left + safe_insets.right);
    const int hud_height =
      r.height_pts () - (int)in_points (safe_insets.top + safe_insets.bottom);
    if (visibility.cinematic_hud) {
      opening.draw_titles (hud, dl, text, hud_width, hud_height);
      hud.draw_ride_prompt (
        text,
        frame.overlay.cinematic_prompt_alpha.numerical_value_in (one),
        hud_width,
        hud_height);
    } else if (visibility.game_hud) {
      HudState hud_state = hud_state_for (frame.hud);
      add_basket_readings (hud_state, w);
      add_camp_readings (hud_state, w);
      add_fishing_readings (hud_state, w);
      add_prompt (hud_state, w);
      w.company.add_readings (hud_state, w.session);
      add_waymarks (hud_state, w, frame, hud_width, hud_height);
      add_bus_board (w, frame, hud_width, hud_height);
      add_chart (hud_state, frame, w);
      hud.draw (dl, text, hud_state, hud_width, hud_height);
      if (hud.diagnostics ())
        draw_trail_map (w,
                        dl,
                        hud_width,
                        hud_height,
                        position_value (frame.hud.subject_position),
                        direction_value (frame.hud.subject_heading));
      if (map.open ())
        draw_map (r, w, hud_width, hud_height);
      if (quit_asked)
        hud.draw_quit_question (dl, text, hud_width, hud_height);
    }
    hud.publish_offers ();
    text.translate (in_points (safe_insets.left), in_points (safe_insets.top));

    // Even a clean inspection capture needs this empty HUD pass: it is
    // also the final post-chain composite into the drawable.
    r.draw_hud_text (text);
    r.draw_hud (dl);
  }
}
