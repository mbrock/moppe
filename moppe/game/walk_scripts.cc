// The scripted walks for captures (MOPPE_WALK): where each starts and the
// deterministic input it drives the walker with.

#include <moppe/environment.hh>
#include <moppe/game/capture_scripts.hh>
#include <moppe/game/fishing.hh>
#include <moppe/game/world_places.hh>
#include <moppe/render/renderer.hh>

#include <cstdlib>
#include <iostream>
#include <string>

namespace moppe::game {
  namespace {
    // A look turned by `turn`, but by no more than `rate` allows in a
    // step.
    radians_t turn_within (radians_t turn,
                           radians_per_second_t rate,
                           seconds_t interval) {
      const radians_t most = rate * interval;
      return std::clamp (turn, -most, most);
    }

    // The look's turn toward a goal lying `leftward` and `ahead` of the
    // walker (the sine and cosine of its bearing): straight round when
    // it is behind.
    radians_t turn_toward (magnitude_t leftward, magnitude_t ahead) {
      return ahead < 0.0f ? (leftward >= 0.0f ? -1.0f : 1.0f) * u::rad
                          : -std::asin (scalar_value (leftward)) * u::rad;
    }
  }

  void WalkScripts::stage (const ScriptStage& s) {
    stage_camp (s);
    stage_bank (s);
    stage_place (s);
  }

  void WalkScripts::stage_bank (const ScriptStage& s) {
    const char* walk = moppe::environment ("MOPPE_WALK");
    const bool swim = walk && std::string_view (walk) == "swim";
    if (!walk || (!swim && std::string_view (walk) != "fish") ||
        s.session.logic ().m_doing != Doing::on_foot ())
      return;
    const terrain::WaterSheets& sheets = s.ground.generated.water_surface ();
    const auto depth_at = [&] (const position_t& at) {
      return (still_water_at (s.ground.world, s.ground.surface (), &sheets, at)
                .numerical_value_in (u::m) -
              terrain::surface_elevation_value (
                s.ground.elevation (plan (at)))) *
             u::m;
    };
    constexpr meters_t step = 4.0f * u::m;
    const position_t near = swim ? s.session.boat ().position () : s.spawn;
    for (int ring = 0; ring < 160; ++ring) {
      const meters_t radius = step * ring;
      const int count =
        std::max (1,
                  static_cast<int> (
                    (2.0f * PI * radius / step).numerical_value_in (one)));
      for (int k = 0; k < count; ++k) {
        const radians_t a = 2.0f * PI * k / count * u::rad;
        position_t stand = position (position_value (near) +
                                     Vec3 (std::cos (radians_value (a)),
                                           0.0f,
                                           std::sin (radians_value (a))) *
                                       meters_value (radius));
        const meters_t depth = depth_at (stand);
        if (swim ? depth < 2.2f * u::m : depth > -0.3f * u::m)
          continue;
        for (int d = 0; d < 16; ++d) {
          const radians_t b = 2.0f * PI * d / 16 * u::rad;
          const direction_t out = unit_direction (Vec3 (
            std::cos (radians_value (b)), 0.0f, std::sin (radians_value (b))));
          // A short cast from dry ground, or room to dive and swim a
          // few lengths starting beside the moorings.
          const auto depth_out = [&] (meters_t distance) {
            return depth_at (stand + quantity_cast<isq::position_vector> (
                                       along (out, distance)));
          };
          if (swim ? depth_out (15.0f * u::m) < 4.8f * u::m ||
                       depth_out (36.0f * u::m) < 4.8f * u::m
                   : depth_out (4.5f * u::m) < 0.5f * u::m ||
                       depth_out (8.0f * u::m) < 0.8f * u::m)
            continue;
          stand = terrain::at_elevation (
            plan (stand), s.ground.elevation (plan (stand)) + 0.1f * u::m);
          if (swim) {
            stand = at_height (
              plan (stand),
              water_surface_at (
                s.ground.world, s.ground.surface (), &sheets, stand, 0.0) -
                1.45f * u::m);
          }
          s.session.walker ().spawn (stand, out);
          s.session.logic ().m_fp_eye = s.session.walker ().eye_position ();
          std::cerr << "moppe: " << walk << " script: a bank "
                    << length (position_value (stand) -
                               position_value (s.spawn))
                    << " m from the spawn" << std::endl;
          return;
        }
      }
    }
    std::cerr << "moppe: " << walk << " script: no open water near the spawn\n";
  }

  void WalkScripts::stage_place (const ScriptStage& s) {
    const char* walk = moppe::environment ("MOPPE_WALK");
    if (!walk || s.session.logic ().m_doing != Doing::on_foot ())
      return;
    const std::string_view script (walk);
    // Where the walker stands, and what they face.
    std::optional<std::pair<position_t, position_t>> stand;
    if (script == "landing" && s.places.layout.landing) {
      // On the strand, looking past the jetty to the skiff beside it.
      const Landing& landing = *s.places.layout.landing;
      const Vec3 root = position_value (landing.jetty.root);
      const Vec3 berth = position_value (landing.berth);
      const Vec3 back = root - berth;
      const Vec3 aside =
        berth - (root + direction_value (landing.jetty.out) *
                          landing.jetty.length.numerical_value_in (u::m));
      stand = { position (berth + back * 0.55f + aside * 6.0f), landing.berth };
    } else if (script == "sanctuary" && !s.places.layout.sanctuaries.empty ()) {
      const Sanctuary& sanctuary = s.places.layout.sanctuaries.front ();
      // Inside the wall by its gap, on the level top, facing the fire.
      const Vec3 centre = position_value (sanctuary.centre);
      stand = { position (
                  centre +
                  direction_value (sanctuary.gate) *
                    (sanctuary.wall_radius.numerical_value_in (u::m) - 1.8f)),
                sanctuary.centre };
    } else if (script == "house" || script == "indoors") {
      // Before the farmhouse's open door, or inside by the back wall
      // looking out through it.
      for (const Farm& farm : s.places.layout.farms)
        for (const Building& b : farm.buildings)
          if (!stand && !farm.ruined && b.kind == Building::Kind::house) {
            const Vec3 base = position_value (b.at);
            const Vec3 forward = direction_value (b.forward);
            const Vec3 left (forward[2], 0.0f, -forward[0]);
            const Vec3 door = base + forward * 2.5f - left * 0.9f;
            stand =
              script == "house"
                ? std::pair (position (door + forward * 6.0f), position (door))
                : std::pair (position (base - forward * 1.6f + left * 0.6f),
                             position (door));
          }
    } else if (script.starts_with ("animals") &&
               !s.session.animals ().animals ().empty ()) {
      // MOPPE_WALK=animals[:SPECIES]: a little outside the wariness of
      // the nearest herd of that kind (or of stock, or of anything),
      // looking at it.
      const std::string_view kind =
        script.size () > 8 ? script.substr (8) : std::string_view ();
      const auto wanted = [kind] (const Animal& a, bool stock) {
        if (kind.empty ())
          return traits (a.species).stock == stock;
        std::string name = traits (a.species).name;
        std::ranges::replace (name, ' ', '_');
        return name == kind;
      };
      const Animal* best = nullptr;
      meters_t best_d = 1e9f * u::m;
      for (const bool stock : { true, false }) {
        for (const Animal& a : s.session.animals ().animals ()) {
          const plan_displacement_t apart = plan (a.position) - plan (s.spawn);
          const meters_t d = libm::hypot (x_of (apart), z_of (apart));
          if (wanted (a, stock) && d < best_d) {
            best_d = d;
            best = &a;
          }
        }
        if (best)
          break;
      }
      if (best) {
        Vec3 sum {};
        int n = 0;
        for (const Animal& a : s.session.animals ().animals ())
          if (a.herd == best->herd) {
            sum = sum + position_value (a.position);
            ++n;
          }
        const position_t middle = position (sum * (1.0f / n));
        // Of a few ways in, the first on dry ground.
        const AnimalGround g = s.places.animal_ground (s.ground);
        const SpeciesTraits& t = traits (best->species);
        const meters_t away = t.stock ? 9.0f * u::m : 2.0f * t.wary;
        for (int k = 0; k < 12 && !stand; ++k) {
          const radians_t angle = 0.5236f * k * u::rad;
          const position_t at = position (
            position_value (middle) + Vec3 (std::cos (radians_value (angle)),
                                            0.0f,
                                            std::sin (radians_value (angle))) *
                                        meters_value (away));
          if (g.dry (
                plan_position (position_value (at)[0], position_value (at)[2])))
            stand = { at, middle };
        }
        std::optional<Vec3> side;
        if (stand) {
          const Vec3& from = position_value (stand->first);
          const Vec3 in =
            normalized (Vec3 (position_value (middle)[0] - from[0],
                              0.0f,
                              position_value (middle)[2] - from[2]));
          side = Vec3 (in[2], 0.0f, -in[0]);
        }
        s.cameras.watch (static_cast<std::size_t> (
                           best - s.session.animals ().animals ().data ()),
                         side ? std::optional (unit_direction (*side))
                              : std::nullopt);
      }
    } else if (script == "barn" || script == "byre" || script == "eggs" ||
               script == "hens") {
      // MOPPE_WALK=barn: before the doors of the barn lived in nearest
      // the spawn, looking in; byre: inside it, looking at the stalls;
      // eggs: by its nest boxes, facing them; hens: looking at the
      // hens' corner, its perches and nest boxes.
      const Building* nearest = nullptr;
      meters_t best = 1e9f * u::m;
      for (const Farm& farm : s.places.layout.farms)
        for (const Building& b : farm.buildings)
          if (farm.kept && !farm.ruined && b.kind == Building::Kind::barn) {
            const plan_displacement_t apart = plan (b.at) - plan (s.spawn);
            const meters_t d = libm::hypot (x_of (apart), z_of (apart));
            if (d < best) {
              best = d;
              nearest = &b;
            }
          }
      if (nearest) {
        const Mat4 frame = building_frame (*nearest);
        m_state.barn = frame;
        // A place in the barn's own frame, in metres.
        const auto at = [&] (float x, float z) {
          return position (frame.transform_point (Vec3 (x, 0.0f, z)));
        };
        if (script == "barn")
          stand = { at (0.8f, barn_front.numerical_value_in (u::m) + 7.0f),
                    at (0.0f, 0.0f) };
        else if (script == "byre")
          stand = { at (-2.0f, 2.6f), at (3.0f, -3.2f) };
        else if (script == "hens")
          stand = { at (-1.6f, 0.6f), at (-5.0f, -3.3f) };
        else
          stand = { at (-4.3f, -2.6f), at (-6.0f, -2.6f) };
      }
    } else if (script.starts_with ("bus-stop") &&
               !s.places.bus.stops ().empty ()) {
      // MOPPE_WALK=bus-stop[:N]: on the verge a little back from the
      // Nth stop (the first, where the first bus stands as play begins),
      // looking past its sign up the road the bus goes on along.
      std::size_t n = 0;
      if (script.size () > 9)
        n = static_cast<std::size_t> (std::strtoul (
              std::string (script.substr (9)).c_str (), nullptr, 10)) %
            s.places.bus.stops ().size ();
      const BusStop& stop = s.places.bus.stops ()[n];
      const position_t& sign = stop.sign;
      const direction_t& forward = stop.forward;
      const Vec3 right (
        -direction_value (forward)[2], 0.0f, direction_value (forward)[0]);
      stand = { position (position_value (sign) -
                          direction_value (forward) * 15.0f + right * 7.0f),
                position (position_value (sign) +
                          direction_value (forward) * 2.0f - right * 6.0f) };
    } else if (script == "bus-ride" && !s.places.bus.empty ()) {
      // MOPPE_WALK=bus-ride: by the door of the first bus, standing at
      // the first stop as play begins, facing it; the script boards.
      const BusPose bus = s.places.bus.where (play_time (0.0)).front ();
      const Vec3 door = position_value (bus_door (bus));
      const direction_t& forward = bus.forward;
      stand = { position (door - direction_value (forward) * 1.5f),
                position (door + direction_value (forward) * 2.0f) };
    } else if (script == "farm" && !s.places.layout.farms.empty () &&
               !s.places.layout.farms.front ().buildings.empty ()) {
      const Building& b = s.places.layout.farms.front ().buildings.front ();
      const Vec3 base = position_value (b.at);
      const Vec3 forward = direction_value (b.forward);
      stand = { position (base + forward * 16.0f +
                          Vec3 (forward[2], 0.0f, -forward[0]) * 9.0f),
                b.at };
    }
    if (!stand)
      return;
    position_t at = stand->first;
    at = terrain::at_elevation (plan (at),
                                s.ground.elevation (plan (at)) + 0.1f * u::m);
    if (const std::optional<terrain::SurfaceElevation> floor = floor_at (
          s.places.layout,
          plan_position (position_value (at)[0], position_value (at)[2])))
      position_value (at)[1] =
        std::max (static_cast<float> (position_value (at)[1]),
                  terrain::surface_elevation_value (*floor) + 0.1f);
    Vec3 facing = position_value (stand->second) - position_value (at);
    facing[1] = 0.0f;
    s.session.walker ().spawn (at, unit_direction (normalized (facing)));
    s.session.logic ().m_fp_eye = s.session.walker ().eye_position ();
    s.session.settle_camera (s.ground.surface ());
    std::cerr << "moppe: walk: " << script << " at " << position_value (at)[0]
              << ' ' << position_value (at)[2] << std::endl;
  }

  void WalkScripts::stage_camp (const ScriptStage& s) {
    const char* walk = moppe::environment ("MOPPE_WALK");
    if (!walk || std::string_view (walk) != "camp" ||
        s.session.logic ().m_doing != Doing::on_foot ())
      return;
    const std::vector<mov::Trunk> near =
      s.ground.trunks.gather (s.spawn, 700.0f * u::m);
    // Under open sky, so there are stars to lie and look at.
    m_state.camp.site = nearest_hammock_site (
      near, s.spawn, s.ground.elevation_field (), 60.0f * u::m);
    if (!m_state.camp.site) {
      std::cerr << "moppe: camp script: no two trees near the spawn "
                   "would take a hammock\n";
      return;
    }
    const HammockSite& site = *m_state.camp.site;
    const position_t middle = hammock_middle (site);
    const Vec3 across = direction_value (hammock_across (site));
    // How near the nearest trunk stands to the walk in from a side.
    const auto clearance = [&] (float side) {
      meters_t least = 100.0f * u::m;
      for (const mov::Trunk& trunk : near) {
        Vec3 to = position_value (trunk.root) - position_value (middle);
        to[1] = 0.0f;
        const meters_t out = dot (to, across) * side * u::m;
        if (out < 1.0f * u::m || out > 9.0f * u::m)
          continue;
        least = std::min (
          least, length (to - across * (meters_value (out) * side)) * u::m);
      }
      return least;
    };
    m_state.camp.side = clearance (1.0f) >= clearance (-1.0f) ? 1.0f : -1.0f;
    position_t stand =
      position (position_value (middle) + across * (m_state.camp.side * 8.5f) +
                direction_value (hammock_along (site)) * 1.5f);
    stand = terrain::at_elevation (
      plan (stand), s.ground.elevation (plan (stand)) + 0.1f * u::m);
    Vec3 facing = position_value (middle) - position_value (stand);
    facing[1] = 0.0f;
    s.session.walker ().spawn (stand, unit_direction (normalized (facing)));
    s.session.logic ().m_fp_eye = s.session.walker ().eye_position ();
    std::cerr << "moppe: camp script: two trees "
              << length (position_value (site.strap[1]) -
                         position_value (site.strap[0]))
              << " m apart, "
              << length (position_value (middle) - position_value (s.spawn))
              << " m from the spawn" << std::endl;
  }

  InputFrame WalkScripts::camp (const ScriptStage& s, seconds_t interval) {
    InputFrame input;
    WalkScriptState::Camp& script = m_state.camp;
    if (!script.site)
      return input;
    const HammockSite& site = *script.site;
    const Walker& walker = s.session.walker ();
    const position_t feet = walker.position ();
    const seconds_t t = m_state.time;
    const seconds_t waited = t - script.since;
    const auto next = [&] {
      ++script.step;
      script.since = t;
    };
    // Turns toward `target` and walks until within `near` of it.
    const auto walk_to = [&] (const position_t& target, meters_t near) {
      Vec3 toward = position_value (target) - position_value (feet);
      toward[1] = 0.0f;
      const meters_t distance = length (toward) * u::m;
      if (distance <= near)
        return true;
      const Vec3 heading = direction_value (walker.heading ());
      const Vec3 left (heading[2], 0.0f, -heading[0]);
      const magnitude_t leftward = dot (toward, left) * u::m / distance;
      const magnitude_t ahead = dot (toward, heading) * u::m / distance;
      input.look_yaw = turn_within (
        turn_toward (leftward, ahead), 2.2f * u::rad / u::s, interval);
      const control_signal_t pace = 0.3f + 0.3f / u::m * distance;
      input.drive = ahead > 0.8f ? std::min (0.8f * control_signal[one], pace)
                                 : 0.0f * control_signal[one];
      return false;
    };
    position_t middle = hammock_middle (site);
    middle =
      terrain::at_elevation (plan (middle), s.ground.elevation (plan (middle)));
    const position_t fireside =
      position (position_value (middle) +
                direction_value (hammock_across (site)) * (script.side * 3.6f) -
                direction_value (hammock_along (site)) * 0.6f);
    // The walker's gaze levels out as they go.
    input.look_pitch =
      turn_within (-0.12f * u::rad - s.session.logic ().m_look_pitch,
                   1.0f * u::rad / u::s,
                   interval);
    switch (script.step) {
    case 0:
      if (walk_to (middle, 0.45f * u::m))
        next ();
      break;
    case 1:
      if (waited > 0.7f * u::s) {
        input.hang_hammock = true;
        next ();
      }
      break;
    case 2:
      if (waited > 1.6f * u::s)
        next ();
      break;
    case 3:
      // The fire is laid a step ahead of where the walker stops.
      if (walk_to (fireside, 1.35f * u::m))
        next ();
      break;
    case 4:
      if (waited > 0.5f * u::s) {
        input.light_fire = true;
        next ();
      }
      break;
    case 5:
      if (waited > 4.0f * u::s)
        next ();
      break;
    case 6:
      if (walk_to (middle, 1.0f * u::m))
        next ();
      break;
    case 7:
      // E picks a mushroom first if one is at hand; press until lying.
      if (s.session.logic ().m_camp.resting)
        next ();
      else if (fmod (waited, 0.9f * u::s) < interval + 1e-4f * u::s &&
               waited > 0.5f * u::s)
        input.use = true;
      break;
    default:
      // Lying there: after a look from outside, through their eyes,
      // gaze wandering a little over the sky.
      if (script.step == 8 && waited > 5.0f * u::s &&
          s.session.logic ().m_cam_mode == CAM_CHASE) {
        s.session.logic ().m_cam_mode = CAM_HELMET;
        s.renderer->reset_temporal_state ();
        next ();
      }
      input.look_pitch = 0.0f * u::rad;
      input.look_yaw = 0.10f * std::sin (seconds_value (waited) * 0.21f) *
                       u::rad / u::s * interval;
      input.turn =
        0.25f * std::sin (seconds_value (waited) * 0.9f) * control_signal[one];
      break;
    }
    return input;
  }

  InputFrame WalkScripts::step (const ScriptStage& s,
                                std::string_view script,
                                seconds_t interval) {
    const seconds_t t = m_state.time;
    m_state.time += interval;
    InputFrame input;
    static constexpr control_signal_t ahead = 1.0f * control_signal[one];
    static constexpr control_signal_t back = -1.0f * control_signal[one];
    static constexpr control_signal_t still = 0.0f * control_signal[one];
    static constexpr proportion_t held = 1.0f * proportion[one];
    static constexpr proportion_t let_go = 0.0f * proportion[one];
    // A key held from `at` for a tenth of a second.
    const auto press = [t, interval] (seconds_t at) {
      return t >= at && t < at + 0.1f * u::s + interval ? held : let_go;
    };
    // At a place (stage_place_script), the walker stands and looks.
    if (script == "landing" || script == "sanctuary" || script == "farm" ||
        script == "indoors" || script == "hens" || script == "byre" ||
        script.starts_with ("bus-stop"))
      return input;
    // Before the farmhouse's door: in through it, a few steps into the
    // room, and a look round.
    if (script == "house") {
      if (t > 1.0f * u::s && t < 5.6f * u::s)
        input.drive = 0.6f * control_signal[one];
      if (t > 6.5f * u::s && t < 9.5f * u::s)
        input.look_yaw = 1.2f * u::rad / u::s * interval;
      return input;
    }
    // By the bus's door: on board.
    if (script == "bus-ride") {
      input.toggle_mount = press (0.5f * u::s) > 0.0f;
      return input;
    }
    if (script.starts_with ("animals")) {
      // Watch them graze a while, then walk up to them.
      input.drive = t > 14.0f * u::s && t < 30.0f * u::s ? ahead : still;
      return input;
    }
    if (script == "camp")
      return camp (s, interval);
    if (script == "swim") {
      input.drive = t > 1.5f * u::s && t < 21.0f * u::s ? ahead : still;
      input.run = t > 9.0f * u::s && t < 11.3f * u::s;
      input.boost = t > 17.0f * u::s && t < 20.0f * u::s ? held : let_go;
      return input;
    }
    if (script == "fish") {
      // Cast, strike at the take, and cast again; with two fish in
      // the creel, turn from the water, light a fire, fry one, and eat.
      const Line& line = s.session.logic ().m_fishing.line;
      if (m_state.supper_since < 0.0f * u::s &&
          s.session.logic ().m_fishing.creel.kept () >= 2 && !line_out (line))
        m_state.supper_since = t;
      if (m_state.supper_since >= 0.0f * u::s) {
        const seconds_t since = t - m_state.supper_since;
        input.look_yaw =
          (since < 1.2f * u::s ? PI / 1.2f : 0.0f) * u::rad / u::s * interval;
        input.drive =
          since > 1.2f * u::s && since < 3.4f * u::s ? ahead : still;
        input.look_pitch =
          turn_within (-0.55f * u::rad - s.session.logic ().m_look_pitch,
                       1.0f * u::rad / u::s,
                       interval);
        input.light_fire = press (m_state.supper_since + 4.2f * u::s) > 0.0f &&
                           !s.session.logic ().m_camp.fire;
        input.use = press (7.5f * u::s + m_state.supper_since) > 0.0f ||
                    press (19.0f * u::s + m_state.supper_since) > 0.0f;
        return input;
      }
      input.look_pitch =
        turn_within (-0.2f * u::rad - s.session.logic ().m_look_pitch,
                     1.0f * u::rad / u::s,
                     interval);
      if (line_out (line))
        m_state.fish_rest = t;
      const seconds_t age =
        seconds_between (s.session.logic ().m_total_time, line.since);
      input.use = (!line_out (line) && t - m_state.fish_rest > 1.5f * u::s) ||
                  (line.phase == Line::Phase::biting && age > 0.4f * u::s);
      return input;
    }
    if (script == "boat-fish") {
      // Out from the shore, the engine stopped to drift, a look out
      // over the side, and a cast; strike at the take and land two.
      // Then cast again and open the throttle, which winds it in.
      const Line& line = s.session.logic ().m_fishing.line;
      const seconds_t age =
        seconds_between (s.session.logic ().m_total_time, line.since);
      if (t < 5.0f * u::s || m_state.boat_fish_off >= 0.0f * u::s) {
        input.drive = ahead;
        return input;
      }
      input.look_yaw =
        turn_within (1.25f * u::rad - s.session.logic ().m_look_yaw,
                     1.0f * u::rad / u::s,
                     interval);
      input.look_pitch =
        turn_within (0.3f * u::rad - s.session.logic ().m_look_pitch,
                     1.0f * u::rad / u::s,
                     interval);
      if (line_out (line))
        m_state.fish_rest = t;
      if (s.session.logic ().m_fishing.creel.kept () >= 2 &&
          line.phase == Line::Phase::waiting && age > 3.0f * u::s)
        m_state.boat_fish_off = t;
      input.use = (!line_out (line) && t - m_state.fish_rest > 1.5f * u::s &&
                   game::boat_drifting (s.session, s.around)) ||
                  (line.phase == Line::Phase::biting && age > 0.4f * u::s);
      return input;
    }
    // The mount key is a toggle, taken once a press.
    const auto tap = [t, interval] (seconds_t at) {
      return t >= at && t < at + interval;
    };
    if (script == "haul") {
      // Take up the rope, walk off with the tractor in tow, turning a
      // little, let go, and climb up.
      input.toggle_mount =
        tap (1.0f * u::s) || tap (12.5f * u::s) || tap (14.0f * u::s);
      input.drive = t > 1.6f * u::s && t < 11.5f * u::s ? ahead : still;
      input.look_yaw = (t > 4.0f * u::s && t < 8.0f * u::s ? 0.12f : 0.0f) *
                       u::rad / u::s * interval;
      return input;
    }
    if (script == "bump") {
      // Into the jeep, into the tractor's side, back off, and again.
      input.toggle_mount = tap (0.5f * u::s);
      input.drive = t > 1.0f * u::s && t < 4.2f * u::s    ? ahead
                    : t > 5.5f * u::s && t < 7.5f * u::s  ? back
                    : t > 8.0f * u::s && t < 11.0f * u::s ? ahead
                                                          : still;
      input.boost = (t > 4.2f * u::s && t < 5.5f * u::s) || t > 11.0f * u::s
                      ? held
                      : let_go;
      return input;
    }
    if (script == "eggs" && m_state.barn) {
      // Take three eggs from the nest boxes, walk out through the
      // doors and down the ramp into the yard, light a fire there,
      // fry them, and eat.
      const Walker& walker = s.session.walker ();
      const Vec3 feet = position_value (walker.position ());
      const auto steer = [&] (const Vec3& goal, control_signal_t pace) {
        Vec3 toward = goal - feet;
        toward[1] = 0.0f;
        const meters_t distance = length (toward) * u::m;
        const Vec3 heading = direction_value (walker.heading ());
        const Vec3 left (heading[2], 0.0f, -heading[0]);
        const magnitude_t leftward =
          dot (toward, left) * u::m / std::max (distance, 1e-3f * u::m);
        const magnitude_t ahead_of =
          dot (toward, heading) * u::m / std::max (distance, 1e-3f * u::m);
        input.look_yaw = turn_within (
          turn_toward (leftward, ahead_of), 2.5f * u::rad / u::s, interval);
        input.drive = ahead_of > 0.7f ? pace : still;
        return distance;
      };
      if (m_state.supper_since >= 0.0f * u::s) {
        input.look_pitch =
          turn_within (-0.55f * u::rad - s.session.logic ().m_look_pitch,
                       1.0f * u::rad / u::s,
                       interval);
        input.light_fire = press (m_state.supper_since + 0.8f * u::s) > 0.0f &&
                           !s.session.logic ().m_camp.fire;
        input.use = press (4.0f * u::s + m_state.supper_since) > 0.0f ||
                    press (15.5f * u::s + m_state.supper_since) > 0.0f;
        return input;
      }
      if (s.session.logic ().m_basket.eggs < 3 && m_state.egg_leg == 0) {
        input.look_pitch =
          turn_within (-0.5f * u::rad - s.session.logic ().m_look_pitch,
                       1.0f * u::rad / u::s,
                       interval);
        // To the nearest box with an egg in it, and into it.
        std::optional<std::size_t> best;
        for (std::size_t k = 0; k < s.places.nests.size () &&
                                k < s.session.logic ().m_nests.size ();
             ++k)
          if (s.session.logic ().m_nests[k].eggs > 0 &&
              (!best ||
               length2 (position_value (s.places.nests[k].at) - feet) <
                 length2 (position_value (s.places.nests[*best].at) - feet)))
            best = k;
        if (!best)
          return input;
        const Vec3 front = position_value (s.places.nests[*best].at) +
                           direction_value (s.places.nests[*best].out) * 0.75f;
        if (game::egg_at_hand (s.session, s.around)) {
          steer (position_value (s.places.nests[*best].at), still);
          if (fmod (t, 0.9f * u::s) < interval + 1e-4f * u::s)
            input.use = true;
        } else {
          steer (front, 0.4f * control_signal[one]);
        }
        return input;
      }
      // Out through the doors, down the ramp, and into the yard.
      const Mat4& barn = *m_state.barn;
      const Vec3 legs[3] = { barn.transform_point (Vec3 (-1.0f, 0, 2.6f)),
                             barn.transform_point (Vec3 (0.0f, 0, 7.0f)),
                             barn.transform_point (Vec3 (2.5f, 0, 9.5f)) };
      if (m_state.egg_leg == 0)
        m_state.egg_leg = 1;
      input.look_pitch =
        turn_within (-0.2f * u::rad - s.session.logic ().m_look_pitch,
                     1.0f * u::rad / u::s,
                     interval);
      if (steer (legs[m_state.egg_leg - 1], 0.6f * control_signal[one]) <
            0.6f * u::m &&
          ++m_state.egg_leg > 3)
        m_state.supper_since = t;
      return input;
    }
    if (script == "forage" || script == "supper") {
      // "supper" forages three mushrooms, walks a few steps on,
      // lights a fire, fries them, and eats.
      if (script == "supper" && m_state.supper_since < 0.0f * u::s &&
          s.session.logic ().m_basket.total () >= 3)
        m_state.supper_since = t;
      if (m_state.supper_since >= 0.0f * u::s) {
        const seconds_t since = t - m_state.supper_since;
        input.drive = since < 2.5f * u::s ? ahead : still;
        input.look_pitch =
          turn_within (-0.55f * u::rad - s.session.logic ().m_look_pitch,
                       1.0f * u::rad / u::s,
                       interval);
        input.light_fire = press (m_state.supper_since + 3.2f * u::s) > 0.0f &&
                           !s.session.logic ().m_camp.fire;
        input.use = press (6.5f * u::s + m_state.supper_since) > 0.0f ||
                    press (18.0f * u::s + m_state.supper_since) > 0.0f;
        return input;
      }
      // Walk to the nearest mushroom, turning toward it, and pick it
      // once within reach; poisonous ones are left after one try.
      const Walker& walker = s.session.walker ();
      const Vec3 feet = position_value (walker.position ());
      const auto target = s.mushrooms.nearest (
        position (feet), 60.0f * u::m, m_state.forage_refused);
      if (target) {
        Vec3 toward = position_value (s.mushrooms.site (*target).base) - feet;
        toward[1] = 0.0f;
        const meters_t distance = length (toward) * u::m;
        const Vec3 heading = direction_value (walker.heading ());
        const Vec3 left (heading[2], 0.0f, -heading[0]);
        const magnitude_t leftward =
          dot (toward, left) * u::m / std::max (distance, 1e-3f * u::m);
        const magnitude_t ahead_of =
          dot (toward, heading) * u::m / std::max (distance, 1e-3f * u::m);
        // Look yaw turns to the right.
        input.look_yaw = turn_within (
          turn_toward (leftward, ahead_of), 2.5f * u::rad / u::s, interval);
        input.look_pitch =
          turn_within (-0.6f * u::rad - s.session.logic ().m_look_pitch,
                       1.0f * u::rad / u::s,
                       interval);
        const bool reachable =
          s.mushrooms.within_reach (position (feet),
                                    unit_direction (heading)) == target;
        const control_signal_t pace = 0.3f + 0.3f / u::m * distance;
        input.drive =
          !reachable && ahead_of > 0.7f ? std::min (ahead, pace) : still;
        const seconds_t beat = fmod (t, 0.9f * u::s);
        if (reachable && beat < interval + 1e-4f * u::s) {
          input.use = true;
          if (!mushroom_edible (s.mushrooms.site (*target).kind))
            m_state.forage_refused.push_back (*target);
        }
      }
      return input;
    }
    if (script == "walk") {
      input.drive = ahead;
    } else if (script == "run") {
      input.drive = ahead;
      input.run = true;
    } else if (script == "jump") {
      input.drive = ahead;
      input.run = true;
      // A jump every 1.6 s, each a fresh press.
      const seconds_t beat = fmod (t, 1.6f * u::s);
      input.boost = beat >= 1.0f * u::s && beat < 1.1f * u::s ? held : let_go;
    } else if (script == "turn") {
      // Standing, a slow look aside that the head and chest follow,
      // a look round further that steps the feet round, and a quick
      // spin twice round that they step after; then a walk with a
      // sharp look aside, and a run swung round in an arc.
      const auto during = [t] (float from, float to) {
        return t >= from * u::s && t < to * u::s;
      };
      input.drive = during (12.0f, 20.0f) ? ahead : still;
      input.run = during (16.0f, 20.0f);
      input.look_yaw = (during (1.0f, 3.0f)     ? 0.5f
                        : during (4.5f, 5.5f)   ? 1.6f
                        : during (7.5f, 8.5f)   ? -4.0f * PI
                        : during (13.5f, 13.8f) ? -5.2f
                        : during (17.0f, 19.5f) ? 1.3f
                                                : 0.0f) *
                       u::rad / u::s * interval;
      return input;
    } else {
      input.drive = t > 1.5f * u::s && t < 12.5f * u::s ? ahead : still;
      input.run = t > 5.0f * u::s && t < 11.0f * u::s;
      input.boost = std::max (press (8.0f * u::s), press (9.6f * u::s));
    }
    input.look_yaw =
      0.25f * std::sin (seconds_value (t) * 0.35f) * u::rad / u::s * interval;
    return input;
  }
}
