// The scripted walks for captures (MOPPE_WALK): where each starts and the
// deterministic input it drives the walker with.

#include <moppe/game/game.hh>

namespace moppe {
  namespace game {
    void MoppeGame::stage_fish_script () {
      const char* walk = moppe::environment ("MOPPE_WALK");
      if (!walk || std::string_view (walk) != "fish" ||
          logic ().m_mode != M_FOOT)
        return;
      const terrain::WaterSheets& sheets = generated_world ().water_surface ();
      const auto depth_at = [&] (const Vec3& at) {
        return still_water_at (world (), surface (), &sheets, at)
                 .numerical_value_in (u::m) -
               world_ground ().height (at);
      };
      constexpr float step = 4.0f;
      for (int ring = 0; ring < 160; ++ring) {
        const float radius = step * ring;
        const int count =
          std::max (1, static_cast<int> (2.0f * PI * radius / step));
        for (int k = 0; k < count; ++k) {
          const float a = 2.0f * PI * k / count;
          Vec3 stand =
            m_spawn_position + Vec3 (std::cos (a), 0.0f, std::sin (a)) * radius;
          if (depth_at (stand) > -0.3f)
            continue;
          for (int d = 0; d < 16; ++d) {
            const float b = 2.0f * PI * d / 16;
            const Vec3 out (std::cos (b), 0.0f, std::sin (b));
            if (depth_at (stand + out * 4.5f) < 0.5f ||
                depth_at (stand + out * 8.0f) < 0.8f)
              continue;
            stand[1] = world_ground ().height (stand) + 0.1f;
            session ().walker ().spawn (moppe::position (stand), out);
            logic ().m_fp_eye = session ().walker ().eye_position ();
            std::cerr << "moppe: fish script: a bank "
                      << length (stand - m_spawn_position)
                      << " m from the spawn" << std::endl;
            return;
          }
        }
      }
      std::cerr << "moppe: fish script: no open water near the spawn\n";
    }

    void MoppeGame::stage_place_script () {
      const char* walk = moppe::environment ("MOPPE_WALK");
      if (!walk || logic ().m_mode != M_FOOT)
        return;
      const std::string_view script (walk);
      std::optional<std::pair<Vec3, Vec3>> stand;
      if (script == "landing" && m_places.layout.landing) {
        // On the strand, looking past the jetty to the skiff beside it.
        const Landing& landing = *m_places.layout.landing;
        const Vec3 back = landing.jetty.root - landing.berth;
        const Vec3 aside =
          landing.berth -
          (landing.jetty.root + landing.jetty.out * landing.jetty.length);
        stand = { landing.berth + back * 0.55f + aside * 6.0f, landing.berth };
      } else if (script == "sanctuary" &&
                 !m_places.layout.sanctuaries.empty ()) {
        const Sanctuary& s = m_places.layout.sanctuaries.front ();
        // Inside the wall by its gap, on the level top, facing the fire.
        stand = { s.centre + s.gate * (s.wall_radius - 1.8f), s.centre };
      } else if (script == "house" || script == "indoors") {
        // Before the farmhouse's open door, or inside by the back wall
        // looking out through it.
        for (const Farm& farm : m_places.layout.farms)
          for (const Building& b : farm.buildings)
            if (!stand && !farm.ruined && b.kind == Building::Kind::house) {
              const Vec3 left (b.forward[2], 0.0f, -b.forward[0]);
              const Vec3 door = b.at + b.forward * 2.5f - left * 0.9f;
              stand =
                script == "house"
                  ? std::pair (door + b.forward * 6.0f, door)
                  : std::pair (b.at - b.forward * 1.6f + left * 0.6f, door);
            }
      } else if (script.starts_with ("animals") &&
                 !session ().animals ().animals ().empty ()) {
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
        float best_d = 1e9f;
        for (const bool stock : { true, false }) {
          for (const Animal& a : session ().animals ().animals ()) {
            const float d = std::hypot (a.position[0] - m_spawn_position[0],
                                        a.position[2] - m_spawn_position[2]);
            if (wanted (a, stock) && d < best_d) {
              best_d = d;
              best = &a;
            }
          }
          if (best)
            break;
        }
        if (best) {
          Vec3 middle {};
          int n = 0;
          for (const Animal& a : session ().animals ().animals ())
            if (a.herd == best->herd) {
              middle = middle + a.position;
              ++n;
            }
          middle = middle * (1.0f / n);
          // Of a few ways in, the first on dry ground.
          const AnimalGround g = m_places.animal_ground (world_ground ());
          const SpeciesTraits& t = traits (best->species);
          const float away = t.stock ? 9.0f : 2.0f * meters_value (t.wary);
          for (int k = 0; k < 12 && !stand; ++k) {
            const float angle = 0.5236f * k;
            const Vec3 at =
              middle + Vec3 (std::cos (angle), 0.0f, std::sin (angle)) * away;
            if (g.dry (at[0], at[2]))
              stand = { at, middle };
          }
          std::optional<Vec3> side;
          if (stand) {
            const Vec3 in = normalized (Vec3 (
              middle[0] - stand->first[0], 0.0f, middle[2] - stand->first[2]));
            side = Vec3 (in[2], 0.0f, -in[0]);
          }
          m_capture_camera.watch (
            static_cast<std::size_t> (best -
                                      session ().animals ().animals ().data ()),
            side);
        }
      } else if (script == "barn" || script == "byre" || script == "eggs" ||
                 script == "hens") {
        // MOPPE_WALK=barn: before the doors of the barn lived in nearest
        // the spawn, looking in; byre: inside it, looking at the stalls;
        // eggs: by its nest boxes, facing them; hens: looking at the
        // hens' corner, its perches and nest boxes.
        const Building* nearest = nullptr;
        float best = 1e9f;
        for (const Farm& farm : m_places.layout.farms)
          for (const Building& b : farm.buildings)
            if (farm.kept && !farm.ruined && b.kind == Building::Kind::barn) {
              const float d = std::hypot (b.at[0] - m_spawn_position[0],
                                          b.at[2] - m_spawn_position[2]);
              if (d < best) {
                best = d;
                nearest = &b;
              }
            }
        if (nearest) {
          const Mat4 frame = building_frame (*nearest);
          m_walk.barn = frame;
          const auto at = [&] (float x, float z) {
            return frame.transform_point (Vec3 (x, 0.0f, z));
          };
          if (script == "barn")
            stand = { at (0.8f, barn_front + 7.0f), at (0.0f, 0.0f) };
          else if (script == "byre")
            stand = { at (-2.0f, 2.6f), at (3.0f, -3.2f) };
          else if (script == "hens")
            stand = { at (-1.6f, 0.6f), at (-5.0f, -3.3f) };
          else
            stand = { at (-4.3f, -2.6f), at (-6.0f, -2.6f) };
        }
      } else if (script == "farm" && !m_places.layout.farms.empty () &&
                 !m_places.layout.farms.front ().buildings.empty ()) {
        const Building& b = m_places.layout.farms.front ().buildings.front ();
        stand = { b.at + b.forward * 16.0f +
                    Vec3 (b.forward[2], 0.0f, -b.forward[0]) * 9.0f,
                  b.at };
      }
      if (!stand)
        return;
      Vec3 at = stand->first;
      at[1] = world_ground ().height (at) + 0.1f;
      if (const std::optional<float> floor =
            floor_at (m_places.layout, at[0], at[2]))
        at[1] = std::max (static_cast<float> (at[1]), *floor + 0.1f);
      Vec3 facing = stand->second - at;
      facing[1] = 0.0f;
      session ().walker ().spawn (moppe::position (at), normalized (facing));
      logic ().m_fp_eye = session ().walker ().eye_position ();
      session ().settle_camera (surface ());
      std::cerr << "moppe: walk: " << script << " at " << at[0] << ' ' << at[2]
                << std::endl;
    }

    void MoppeGame::stage_camp_script () {
      const char* walk = moppe::environment ("MOPPE_WALK");
      if (!walk || std::string_view (walk) != "camp" ||
          logic ().m_mode != M_FOOT)
        return;
      const std::vector<mov::Trunk> near =
        m_trunk_field.gather (m_spawn_position, 700.0f);
      // Under open sky, so there are stars to lie and look at.
      m_walk.camp.site = nearest_hammock_site (
        near, m_spawn_position, world_ground ().reader (), 60.0f);
      if (!m_walk.camp.site) {
        std::cerr << "moppe: camp script: no two trees near the spawn "
                     "would take a hammock\n";
        return;
      }
      const HammockSite& site = *m_walk.camp.site;
      const Vec3 middle = hammock_middle (site);
      const Vec3 across = hammock_across (site);
      // How near the nearest trunk stands to the walk in from a side.
      const auto clearance = [&] (float side) {
        float least = 100.0f;
        for (const mov::Trunk& trunk : near) {
          Vec3 to = trunk.root - middle;
          to[1] = 0.0f;
          const float out = dot (to, across) * side;
          if (out < 1.0f || out > 9.0f)
            continue;
          least = std::min (least, length (to - across * (out * side)));
        }
        return least;
      };
      m_walk.camp.side = clearance (1.0f) >= clearance (-1.0f) ? 1.0f : -1.0f;
      Vec3 stand = middle + across * (m_walk.camp.side * 8.5f) +
                   hammock_along (site) * 1.5f;
      stand[1] = world_ground ().height (stand) + 0.1f;
      Vec3 facing = middle - stand;
      facing[1] = 0.0f;
      session ().walker ().spawn (moppe::position (stand), normalized (facing));
      logic ().m_fp_eye = session ().walker ().eye_position ();
      std::cerr << "moppe: camp script: two trees "
                << length (site.strap[1] - site.strap[0]) << " m apart, "
                << length (middle - m_spawn_position) << " m from the spawn"
                << std::endl;
    }

    InputFrame MoppeGame::camp_script (seconds_t interval) {
      const float dt = seconds_value (interval);
      InputFrame input;
      WalkScriptState::Camp& script = m_walk.camp;
      if (!script.site)
        return input;
      const HammockSite& site = *script.site;
      const Walker& walker = session ().walker ();
      const Vec3 feet = walker.position ();
      const float t = m_walk.time;
      const float waited = t - script.since;
      const auto next = [&] {
        ++script.step;
        script.since = t;
      };
      // Turns toward `target` and walks until within `near` of it.
      const auto walk_to = [&] (const Vec3& target, float near) {
        Vec3 toward = target - feet;
        toward[1] = 0.0f;
        const float distance = length (toward);
        if (distance <= near)
          return true;
        const Vec3 heading = walker.heading ();
        const Vec3 left (heading[2], 0.0f, -heading[0]);
        const float leftward = dot (toward, left) / distance;
        const float ahead = dot (toward, heading) / distance;
        const float turn = ahead < 0.0f ? (leftward >= 0.0f ? -1.0f : 1.0f)
                                        : -std::asin (leftward);
        input.look_yaw = std::clamp (turn, -2.2f * dt, 2.2f * dt);
        input.drive =
          ahead > 0.8f ? std::min (0.8f, 0.3f + 0.3f * distance) : 0.0f;
        return false;
      };
      Vec3 middle = hammock_middle (site);
      middle[1] = world_ground ().height (middle);
      const Vec3 fireside = middle +
                            hammock_across (site) * (script.side * 3.6f) -
                            hammock_along (site) * 0.6f;
      // The walker's gaze levels out as they go.
      input.look_pitch =
        std::clamp (-0.12f - logic ().m_look_pitch, -1.0f * dt, 1.0f * dt);
      switch (script.step) {
      case 0:
        if (walk_to (middle, 0.45f))
          next ();
        break;
      case 1:
        if (waited > 0.7f) {
          input.hang_hammock = true;
          next ();
        }
        break;
      case 2:
        if (waited > 1.6f)
          next ();
        break;
      case 3:
        // The fire is laid a step ahead of where the walker stops.
        if (walk_to (fireside, 1.35f))
          next ();
        break;
      case 4:
        if (waited > 0.5f) {
          input.light_fire = true;
          next ();
        }
        break;
      case 5:
        if (waited > 4.0f)
          next ();
        break;
      case 6:
        if (walk_to (middle, 1.0f))
          next ();
        break;
      case 7:
        // E picks a mushroom first if one is at hand; press until lying.
        if (logic ().m_camp.resting)
          next ();
        else if (std::fmod (waited, 0.9f) < dt + 1e-4f && waited > 0.5f)
          input.deploy_glider = true;
        break;
      default:
        // Lying there: after a look from outside, through their eyes,
        // gaze wandering a little over the sky.
        if (script.step == 8 && waited > 5.0f &&
            logic ().m_cam_mode == CAM_CHASE) {
          logic ().m_cam_mode = CAM_HELMET;
          m_renderer->reset_temporal_state ();
          next ();
        }
        input.look_pitch = 0.0f;
        input.look_yaw = 0.10f * std::sin (waited * 0.21f) * dt;
        input.turn = 0.25f * std::sin (waited * 0.9f);
        break;
      }
      return input;
    }

    InputFrame MoppeGame::scripted_walk (std::string_view script,
                                         seconds_t interval) {
      const float dt = seconds_value (interval);
      const float t = m_walk.time;
      m_walk.time += dt;
      InputFrame input;
      const auto press = [t, dt] (float at) {
        return t >= at && t < at + 0.1f + dt ? 1.0f : 0.0f;
      };
      // At a place (stage_place_script), the walker stands and looks.
      if (script == "landing" || script == "sanctuary" || script == "farm" ||
          script == "indoors" || script == "hens" || script == "byre")
        return input;
      // Before the farmhouse's door: in through it, a few steps into the
      // room, and a look round.
      if (script == "house") {
        if (t > 1.0f && t < 5.6f)
          input.drive = 0.6f;
        if (t > 6.5f && t < 9.5f)
          input.look_yaw = 1.2f * dt;
        return input;
      }
      if (script.starts_with ("animals")) {
        // Watch them graze a while, then walk up to them.
        input.drive = t > 14.0f && t < 30.0f ? 1.0f : 0.0f;
        return input;
      }
      if (script == "camp")
        return camp_script (interval);
      if (script == "fish") {
        // Cast, strike at the take, and cast again; with two fish in
        // the creel, turn from the water, light a fire, fry one, and eat.
        const Line& line = logic ().m_fishing.line;
        if (m_walk.supper_since < 0.0f &&
            logic ().m_fishing.creel.kept () >= 2 && !line_out (line))
          m_walk.supper_since = t;
        if (m_walk.supper_since >= 0.0f) {
          const float since = t - m_walk.supper_since;
          input.look_yaw = since < 1.2f ? PI / 1.2f * dt : 0.0f;
          input.drive = since > 1.2f && since < 3.4f ? 1.0f : 0.0f;
          input.look_pitch =
            std::clamp (-0.55f - logic ().m_look_pitch, -1.0f * dt, 1.0f * dt);
          input.light_fire =
            press (m_walk.supper_since + 4.2f) > 0.0f && !logic ().m_camp.fire;
          input.deploy_glider = press (7.5f + m_walk.supper_since) > 0.0f ||
                                press (19.0f + m_walk.supper_since) > 0.0f;
          return input;
        }
        input.look_pitch =
          std::clamp (-0.2f - logic ().m_look_pitch, -1.0f * dt, 1.0f * dt);
        if (line_out (line))
          m_walk.fish_rest = t;
        const float age = seconds_between (logic ().m_total_time, line.since);
        input.deploy_glider =
          (!line_out (line) && t - m_walk.fish_rest > 1.5f) ||
          (line.phase == Line::Phase::biting && age > 0.4f);
        return input;
      }
      if (script == "boat-fish") {
        // Out from the shore, the engine stopped to drift, a look out
        // over the side, and a cast; strike at the take and land two.
        // Then cast again and open the throttle, which winds it in.
        const Line& line = logic ().m_fishing.line;
        const float age = seconds_between (logic ().m_total_time, line.since);
        if (t < 5.0f || m_walk.boat_fish_off >= 0.0f) {
          input.drive = 1.0f;
          return input;
        }
        input.look_yaw =
          std::clamp (1.25f - logic ().m_look_yaw, -1.0f * dt, 1.0f * dt);
        input.look_pitch =
          std::clamp (0.3f - logic ().m_look_pitch, -1.0f * dt, 1.0f * dt);
        if (line_out (line))
          m_walk.fish_rest = t;
        if (logic ().m_fishing.creel.kept () >= 2 &&
            line.phase == Line::Phase::waiting && age > 3.0f)
          m_walk.boat_fish_off = t;
        input.deploy_glider =
          (!line_out (line) && t - m_walk.fish_rest > 1.5f &&
           game::boat_drifting (session (), surroundings ())) ||
          (line.phase == Line::Phase::biting && age > 0.4f);
        return input;
      }
      // The mount key is a toggle, taken once a press.
      const auto tap = [t, dt] (float at) { return t >= at && t < at + dt; };
      if (script == "haul") {
        // Take up the rope, walk off with the tractor in tow, turning a
        // little, let go, and climb up.
        input.toggle_mount = tap (1.0f) || tap (12.5f) || tap (14.0f);
        input.drive = t > 1.6f && t < 11.5f ? 1.0f : 0.0f;
        input.look_yaw = t > 4.0f && t < 8.0f ? 0.12f * dt : 0.0f;
        return input;
      }
      if (script == "bump") {
        // Into the jeep, into the tractor's side, back off, and again.
        input.toggle_mount = tap (0.5f);
        input.drive = t > 1.0f && t < 4.2f    ? 1.0f
                      : t > 5.5f && t < 7.5f  ? -1.0f
                      : t > 8.0f && t < 11.0f ? 1.0f
                                              : 0.0f;
        input.boost = (t > 4.2f && t < 5.5f) || t > 11.0f ? 1.0f : 0.0f;
        return input;
      }
      if (script == "eggs" && m_walk.barn) {
        // Take three eggs from the nest boxes, walk out through the
        // doors and down the ramp into the yard, light a fire there,
        // fry them, and eat.
        const Walker& walker = session ().walker ();
        const Vec3 feet = walker.position ();
        const auto steer = [&] (const Vec3& goal, float pace) {
          Vec3 toward = goal - feet;
          toward[1] = 0.0f;
          const float distance = length (toward);
          const Vec3 heading = walker.heading ();
          const Vec3 left (heading[2], 0.0f, -heading[0]);
          const float leftward =
            dot (toward, left) / std::max (distance, 1e-3f);
          const float ahead =
            dot (toward, heading) / std::max (distance, 1e-3f);
          const float turn = ahead < 0.0f ? (leftward >= 0.0f ? -1.0f : 1.0f)
                                          : -std::asin (leftward);
          input.look_yaw = std::clamp (turn, -2.5f * dt, 2.5f * dt);
          input.drive = ahead > 0.7f ? pace : 0.0f;
          return distance;
        };
        if (m_walk.supper_since >= 0.0f) {
          input.look_pitch =
            std::clamp (-0.55f - logic ().m_look_pitch, -1.0f * dt, 1.0f * dt);
          input.light_fire =
            press (m_walk.supper_since + 0.8f) > 0.0f && !logic ().m_camp.fire;
          input.deploy_glider = press (4.0f + m_walk.supper_since) > 0.0f ||
                                press (15.5f + m_walk.supper_since) > 0.0f;
          return input;
        }
        if (logic ().m_basket.eggs < 3 && m_walk.egg_leg == 0) {
          input.look_pitch =
            std::clamp (-0.5f - logic ().m_look_pitch, -1.0f * dt, 1.0f * dt);
          // To the nearest box with an egg in it, and into it.
          std::optional<std::size_t> best;
          for (std::size_t k = 0;
               k < m_places.nests.size () && k < logic ().m_nests.size ();
               ++k)
            if (logic ().m_nests[k].eggs > 0 &&
                (!best || length2 (m_places.nests[k].at - feet) <
                            length2 (m_places.nests[*best].at - feet)))
              best = k;
          if (!best)
            return input;
          const Vec3 front =
            m_places.nests[*best].at + m_places.nests[*best].out * 0.75f;
          if (game::egg_at_hand (session (), surroundings ())) {
            steer (m_places.nests[*best].at, 0.0f);
            if (std::fmod (t, 0.9f) < dt + 1e-4f)
              input.deploy_glider = true;
          } else {
            steer (front, 0.4f);
          }
          return input;
        }
        // Out through the doors, down the ramp, and into the yard.
        const Mat4& barn = *m_walk.barn;
        const Vec3 legs[3] = { barn.transform_point (Vec3 (-1.0f, 0, 2.6f)),
                               barn.transform_point (Vec3 (0.0f, 0, 7.0f)),
                               barn.transform_point (Vec3 (2.5f, 0, 9.5f)) };
        if (m_walk.egg_leg == 0)
          m_walk.egg_leg = 1;
        input.look_pitch =
          std::clamp (-0.2f - logic ().m_look_pitch, -1.0f * dt, 1.0f * dt);
        if (steer (legs[m_walk.egg_leg - 1], 0.6f) < 0.6f &&
            ++m_walk.egg_leg > 3)
          m_walk.supper_since = t;
        return input;
      }
      if (script == "forage" || script == "supper") {
        // "supper" forages three mushrooms, walks a few steps on,
        // lights a fire, fries them, and eats.
        if (script == "supper" && m_walk.supper_since < 0.0f &&
            logic ().m_basket.total () >= 3)
          m_walk.supper_since = t;
        if (m_walk.supper_since >= 0.0f) {
          const float since = t - m_walk.supper_since;
          input.drive = since < 2.5f ? 1.0f : 0.0f;
          input.look_pitch =
            std::clamp (-0.55f - logic ().m_look_pitch, -1.0f * dt, 1.0f * dt);
          input.light_fire =
            press (m_walk.supper_since + 3.2f) > 0.0f && !logic ().m_camp.fire;
          input.deploy_glider = press (6.5f + m_walk.supper_since) > 0.0f ||
                                press (18.0f + m_walk.supper_since) > 0.0f;
          return input;
        }
        // Walk to the nearest mushroom, turning toward it, and pick it
        // once within reach; poisonous ones are left after one try.
        const Walker& walker = session ().walker ();
        const Vec3 feet = walker.position ();
        const auto target =
          m_mushrooms.nearest (feet, 60.0f, m_walk.forage_refused);
        if (target) {
          Vec3 toward = m_mushrooms.site (*target).base - feet;
          toward[1] = 0.0f;
          const float distance = length (toward);
          const Vec3 heading = walker.heading ();
          const Vec3 left (heading[2], 0.0f, -heading[0]);
          const float leftward =
            dot (toward, left) / std::max (distance, 1e-3f);
          const float ahead =
            dot (toward, heading) / std::max (distance, 1e-3f);
          // Look yaw turns to the right.
          const float turn = ahead < 0.0f ? (leftward >= 0.0f ? -1.0f : 1.0f)
                                          : -std::asin (leftward);
          input.look_yaw = std::clamp (turn, -2.5f * dt, 2.5f * dt);
          input.look_pitch =
            std::clamp (-0.6f - logic ().m_look_pitch, -1.0f * dt, 1.0f * dt);
          const bool reachable =
            m_mushrooms.within_reach (feet, heading) == target;
          input.drive = !reachable && ahead > 0.7f
                          ? std::min (1.0f, 0.3f + 0.3f * distance)
                          : 0.0f;
          const float beat = std::fmod (t, 0.9f);
          if (reachable && beat < dt + 1e-4f) {
            input.deploy_glider = true;
            if (!mushroom_edible (m_mushrooms.site (*target).kind))
              m_walk.forage_refused.push_back (*target);
          }
        }
        return input;
      }
      if (script == "walk") {
        input.drive = 1.0f;
      } else if (script == "run") {
        input.drive = 1.0f;
        input.run = true;
      } else if (script == "jump") {
        input.drive = 1.0f;
        input.run = true;
        // A jump every 1.6 s, each a fresh press.
        const float beat = std::fmod (t, 1.6f);
        input.boost = beat >= 1.0f && beat < 1.1f ? 1.0f : 0.0f;
      } else if (script == "turn") {
        // Standing, a slow look aside that the head and chest follow,
        // a look round further that steps the feet round, and a quick
        // spin twice round that they step after; then a walk with a
        // sharp look aside, and a run swung round in an arc.
        const auto during = [t] (float from, float to) {
          return t >= from && t < to;
        };
        input.drive = during (12.0f, 20.0f) ? 1.0f : 0.0f;
        input.run = during (16.0f, 20.0f);
        input.look_yaw = (during (1.0f, 3.0f)     ? 0.5f
                          : during (4.5f, 5.5f)   ? 1.6f
                          : during (7.5f, 8.5f)   ? -4.0f * PI
                          : during (13.5f, 13.8f) ? -5.2f
                          : during (17.0f, 19.5f) ? 1.3f
                                                  : 0.0f) *
                         dt;
        return input;
      } else {
        input.drive = t > 1.5f && t < 12.5f ? 1.0f : 0.0f;
        input.run = t > 5.0f && t < 11.0f;
        input.boost = std::max (press (8.0f), press (9.6f));
      }
      input.look_yaw = 0.25f * std::sin (t * 0.35f) * dt;
      return input;
    }
  }
}
