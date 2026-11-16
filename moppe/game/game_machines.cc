// Where the machines stand when a world begins (park_jeep, park_boat, and
// the rest), the open ground they are parked on, the boats' moorings, the
// water autopilots, and the stuck tractor's rescue scene.

#include <moppe/game/game.hh>

namespace moppe {
  namespace game {
    void MoppeGame::park_boat (bool outward) {
      static const bool lakes_first = [] {
        const char* water = moppe::environment ("MOPPE_BOAT");
        return water && std::string_view (water) == "lake";
      }();
      static const bool river = [] {
        const char* water = moppe::environment ("MOPPE_BOAT");
        return water && std::string_view (water) == "river";
      }();
      m_mooring.reset ();
      m_mooring_out.reset ();
      m_boat_route.clear ();
      m_boat_route_at = 0;
      if (river && moor_on_river ())
        return;
      if (!lakes_first && m_places.landing) {
        // At the end of the road, alongside the landing's jetty.
        const Landing& landing = *m_places.landing;
        mov::Boat& boat = session ().boat ();
        boat.set_trunks (&m_trunk_field);
        boat.place (landing.berth,
                    outward ? -landing.berth_forward : landing.berth_forward,
                    landing.berth[1]);
        m_mooring = landing.jetty.root;
        m_mooring_out = landing.jetty.out;
        std::cerr << "moppe: boat moored at the landing's jetty, "
                  << std::hypot (landing.berth[0] - m_spawn_position[0],
                                 landing.berth[2] - m_spawn_position[2])
                  << " m from the spawn, at " << landing.berth[0] << ' '
                  << landing.berth[2] << std::endl;
        return;
      }
      if (!lakes_first && moor_boat (true, outward))
        return;
      if (!moor_boat (false, outward))
        std::cerr << "moppe: boat: no open water near the spawn\n";
    }

    void MoppeGame::park_jet_ski (bool outward) {
      mov::JetSki& ski = session ().jet_ski ();
      ski.set_trunks (&m_trunk_field);
      // Where the boat was left, not where someone has since taken it.
      const mov::Boat& boat = session ().boat ();
      if (logic ().m_mode != M_BOAT || !m_boat_berth)
        m_boat_berth = { boat.position (), boat.forward () };
      const Vec3 berth = m_boat_berth->first;
      Vec3 bow = m_boat_berth->second;
      bow[1] = 0.0f;
      bow = length2 (bow) > 1e-6f ? normalized (bow) : Vec3 (0, 0, 1);
      const Vec3 beam (bow[2], 0.0f, -bow[0]);
      std::vector<Vec3> berths;
      if (m_places.landing && m_mooring &&
          length2 (*m_mooring - m_places.landing->jetty.root) < 1.0f) {
        // Across the jetty from the boat, a little nearer its head.
        const Jetty& jetty = m_places.landing->jetty;
        const Vec3 side (jetty.out[2], 0.0f, -jetty.out[0]);
        const float across = dot (berth - jetty.root, side);
        berths.push_back (
          berth -
          side * (across + std::copysign (jetty.half_width + 0.8f, across)) +
          jetty.out * 0.9f);
      }
      for (const float aside : { 3.2f, -3.2f, 5.5f, -5.5f })
        berths.push_back (berth + beam * aside);
      for (const float along : { -5.0f, 5.5f })
        berths.push_back (berth + bow * along);
      const Vec3 facing = outward && m_mooring_out ? *m_mooring_out : bow;
      for (const Vec3& at : berths)
        if (boat_depth_at (at) > 0.5f) {
          ski.place (at, facing, boat_depth_at (at) + ground_height (at));
          std::cerr << "moppe: jet ski afloat " << std::lround (from_spawn (at))
                    << " m from the spawn, at " << at[0] << ' ' << at[2]
                    << std::endl;
          return;
        }
      if (!m_mooring) {
        std::cerr << "moppe: jet ski: no water near the spawn\n";
        return;
      }
      // No water round the boat deep enough: drawn up on its shore.
      ski.place (*m_mooring, facing, -1e6f);
      std::cerr << "moppe: jet ski drawn up on the boat's shore, at "
                << (*m_mooring)[0] << ' ' << (*m_mooring)[2] << std::endl;
    }

    float MoppeGame::boat_depth_at (const Vec3& at) const {
      return still_water_at (
               world (), surface (), &generated_world ().water_surface (), at)
               .numerical_value_in (u::m) -
             ground_height (at);
    }

    bool MoppeGame::moor_on_river () {
      // The course from its head down to the mouth, unwrapped about the
      // periodic world.
      const float period[2] = {
        surface ().domain ().period_x ().numerical_value_in (u::m),
        surface ().domain ().period_z ().numerical_value_in (u::m),
      };
      const auto course = greatest_river_course (lake_census (), rivers ());
      std::vector<Vec3> line;
      for (auto reach = course.rbegin (); reach != course.rend (); ++reach)
        for (const terrain::RiverAlignmentPoint& point :
             (*reach)->alignment.points) {
          Vec3 at (point.x_m, 0.0f, point.z_m);
          if (!line.empty ())
            for (int axis : { 0, 2 }) {
              const float p = period[axis / 2];
              at[axis] -= p * std::round ((at[axis] - line.back ()[axis]) / p);
            }
          if (line.empty () || length (at - line.back ()) > 4.0f)
            line.push_back (at);
        }
      if (line.size () < 3)
        return false;
      // So far up from the mouth, and on to where it runs deep.
      static const float most = [] {
        const char* reach = moppe::environment ("MOPPE_BOAT_RIVER_M");
        return reach ? std::strtof (reach, nullptr) : 1200.0f;
      }();
      std::size_t start = line.size () - 1;
      float up = 0.0f;
      while (start > 0 && (up < most || boat_depth_at (line[start]) < 0.6f)) {
        up += length (line[start] - line[start - 1]);
        --start;
      }
      if (start + 2 >= line.size ())
        return false;
      const Vec3 at = line[start];
      const Vec3 ahead = normalized (line[start + 1] - at);
      mov::Boat& boat = session ().boat ();
      boat.set_trunks (&m_trunk_field);
      boat.place (at, ahead, boat_depth_at (at) + ground_height (at));
      m_mooring = at;
      m_mooring_out = ahead;
      m_boat_route.assign (line.begin () + start + 1, line.end ());
      std::cerr << "moppe: boat moored on the river " << up
                << " m above its mouth, at " << at[0] << ' ' << at[2]
                << std::endl;
      return true;
    }

    InputFrame MoppeGame::boat_autopilot (float t) {
      const mov::Boat& boat = session ().boat ();
      return water_autopilot (
        t, boat.position (), boat.forward (), { .follow_route = true });
    }

    InputFrame MoppeGame::jet_ski_autopilot (float t) {
      const mov::JetSki& ski = session ().jet_ski ();
      return water_autopilot (
        t,
        ski.position (),
        ski.forward (),
        { .speed = length (ski.velocity ()), .sight = 240.0f, .room = 200.0f });
    }

    InputFrame MoppeGame::water_autopilot (float t,
                                           const Vec3& at,
                                           Vec3 heading,
                                           const WaterPilot& craft) {
      const float speed = craft.speed;
      heading[1] = 0.0f;
      heading = normalized (heading);
      const auto turned = [] (const Vec3& v, float a) {
        return Vec3 (v[0] * std::cos (a) - v[2] * std::sin (a),
                     0.0f,
                     v[0] * std::sin (a) + v[2] * std::cos (a));
      };
      // How far open water runs along `way`, as far as it looks.
      const bool river =
        craft.follow_route && m_boat_route_at < m_boat_route.size ();
      const float shallow = river ? 0.12f : 0.8f;
      const auto open_reach = [&] (const Vec3& way) {
        for (float reach = 6.0f; reach <= craft.sight; reach += 6.0f)
          if (boat_depth_at (at + way * reach) < shallow)
            return reach;
        return craft.sight + 10.0f;
      };
      Vec3 want;
      float drive = 1.0f;
      while (river && m_boat_route_at < m_boat_route.size ()) {
        const Vec3 next = m_boat_route[m_boat_route_at];
        const Vec3 to (next[0] - at[0], 0.0f, next[2] - at[2]);
        if (length (to) > 22.0f && dot (to, heading) > -5.0f)
          break;
        ++m_boat_route_at;
      }
      if (river && m_boat_route_at < m_boat_route.size ()) {
        const Vec3 next = m_boat_route[m_boat_route_at];
        want = normalized (Vec3 (next[0] - at[0], 0.0f, next[2] - at[2]));
        drive = 0.85f;
      } else {
        // Over open water: the way that runs open furthest near a
        // slowly wandering course.
        const float wander = 0.5f * std::sin (t * 0.21f);
        float best = -1e9f;
        for (int k = -8; k <= 8; ++k) {
          const float a = k * (PI / 16.0f);
          const float score =
            std::min (open_reach (turned (heading, a)), craft.room) -
            40.0f * std::abs (a - wander * 0.6f);
          if (score > best) {
            best = score;
            want = turned (heading, a);
          }
        }
      }
      // Shallows close ahead win over the course; what goes fast sees
      // them from further off and slows to come round.
      const float near =
        std::max (river ? 13.0f : 20.0f, std::min (2.6f * speed, 60.0f));
      const float ahead = open_reach (heading);
      if (ahead < near) {
        const float left = open_reach (turned (heading, -0.6f));
        const float right = open_reach (turned (heading, 0.6f));
        want = turned (heading, right >= left ? 0.9f : -0.9f);
        if (speed > 8.0f)
          drive = ahead < 0.5f * near ? 0.3f : 0.6f;
      }
      const Vec3 right = normalized (cross (heading, Vec3 (0, 1, 0)));
      const float bearing = std::atan2 (dot (want, right), dot (want, heading));
      return {
        .turn = std::clamp (2.2f * bearing, -1.0f, 1.0f),
        .drive = std::abs (bearing) > 1.2f ? 0.5f : drive,
      };
    }

    bool MoppeGame::moor_boat (bool sea_only, bool outward) {
      const terrain::WaterSheets& sheets = generated_world ().water_surface ();
      const float sea = world ().water_level.numerical_value_in (u::m);
      const auto level_at = [&] (const Vec3& at) {
        return still_water_at (world (), surface (), &sheets, at)
          .numerical_value_in (u::m);
      };
      const auto depth_at = [&] (const Vec3& at) {
        return level_at (at) - ground_height (at);
      };
      // Still water: the sea, or (unless `sea_only`) a lake's sheet
      // that does not run.
      const auto still = [&] (const Vec3& at) {
        const position_t where = moppe::position (Vec3 (at[0], 0.0f, at[2]));
        if (level_at (at) <= sea + 0.05f)
          return true;
        if (sea_only)
          return false;
        Vec3 current = spatial::sample<terrain::water_velocity> (sheets, where)
                         .numerical_value_in (u::m / u::s);
        current[1] = 0.0f;
        return length (current) < 0.1f;
      };
      // Open water ahead: deep enough to drive in over a good stretch.
      const auto open = [&] (const Vec3& from, const Vec3& out) {
        int wet = 0;
        for (int k = 0; k < 9; ++k) {
          const float b = 0.6f * (k - 4) / 4.0f;
          const Vec3 dir (out[0] * std::cos (b) - out[2] * std::sin (b),
                          0.0f,
                          out[0] * std::sin (b) + out[2] * std::cos (b));
          bool through = true;
          for (const float reach : { 15.0f, 35.0f, 60.0f })
            through = through && depth_at (from + dir * reach) > 0.8f;
          wet += through;
        }
        return wet >= 7;
      };
      constexpr float step = 5.0f;
      for (int ring = 0; ring < 800; ++ring) {
        const float radius = step * ring;
        const int count =
          std::max (1, static_cast<int> (2.0f * PI * radius / step));
        for (int k = 0; k < count; ++k) {
          const float a = 2.0f * PI * k / count;
          const Vec3 shore =
            m_spawn_position + Vec3 (std::cos (a), 0.0f, std::sin (a)) * radius;
          // Dry ground just above the water, not a cliff.
          const float d = depth_at (shore);
          if (d > -0.05f || d < -0.6f)
            continue;
          for (int e = 0; e < 16; ++e) {
            const float b = 2.0f * PI * e / 16;
            const Vec3 out (std::cos (b), 0.0f, std::sin (b));
            const Vec3 at = shore + out * 5.0f;
            if (depth_at (at) < 0.45f || depth_at (shore + out * 2.0f) > 0.4f ||
                !still (at) || !open (at, out))
              continue;
            // Facing out, it lies further off, so that its long tail
            // reaches water deep enough to drive it from the start.
            const Vec3 moor = outward && depth_at (shore + out * 9.0f) > 0.7f
                                ? shore + out * 9.0f
                                : at;
            mov::Boat& boat = session ().boat ();
            boat.set_trunks (&m_trunk_field);
            boat.place (moor, outward ? out : -out, level_at (moor));
            m_mooring = shore;
            m_mooring_out = out;
            std::cerr << "moppe: boat moored "
                      << length (Vec3 (at[0] - m_spawn_position[0],
                                       0.0f,
                                       at[2] - m_spawn_position[2]))
                      << " m from the spawn, on "
                      << (level_at (at) <= sea + 0.05f ? "the sea" : "a lake")
                      << std::endl;
            return true;
          }
        }
      }
      return false;
    }

    void MoppeGame::stage_rescue () {
      const char* walk = moppe::environment ("MOPPE_WALK");
      if (!walk || logic ().m_mode != M_FOOT)
        return;
      const std::string_view script (walk);
      const mov::Tractor& tractor = session ().tractor ();
      if (script == "haul") {
        logic ().m_trouble[static_cast<std::size_t> (Machine::tractor)] = {
          .anchor = tractor.position (), .strain = 4.0f, .stuck = true
        };
        Vec3 ahead = tractor.forward ();
        ahead[1] = 0.0f;
        ahead = length2 (ahead) > 1e-6f ? normalized (ahead) : Vec3 (0, 0, 1);
        Vec3 stand = tractor.position () + ahead * 3.0f;
        stand[1] = ground_height (stand) + 0.1f;
        session ().walker ().spawn (moppe::position (stand), ahead);
      } else if (script == "bump") {
        Vec3 across = tractor.left ();
        across[1] = 0.0f;
        across =
          length2 (across) > 1e-6f ? normalized (across) : Vec3 (1, 0, 0);
        session ().jeep ().place (tractor.position () + across * 18.0f,
                                  -across);
        Vec3 stand = session ().jeep ().point (Vec3 (1.75f, 0.0f, -0.1f));
        stand[1] = ground_height (stand) + 0.1f;
        session ().walker ().spawn (moppe::position (stand), -across);
      } else {
        return;
      }
      logic ().m_fp_eye = session ().walker ().eye_position ();
      session ().settle_camera (surface ());
      std::cerr << "moppe: walk: " << script << " beside the tractor at "
                << tractor.position ()[0] << ", " << tractor.position ()[2]
                << std::endl;
    }

    bool
    MoppeGame::open_ground (const Vec3& at, float room, float relief) const {
      const terrain::WaterSheets& sheets = generated_world ().water_surface ();
      const float h = ground_height (at);
      float low = h, high = h;
      for (int i = 0; i < 12; ++i) {
        const float b = 6.2831853f * i / 12.0f;
        const Vec3 p = at + Vec3 (std::cos (b), 0.0f, std::sin (b)) * room;
        const float g = ground_height (p);
        if (still_water_at (world (), surface (), &sheets, p)
              .numerical_value_in (u::m) > g - 0.6f)
          return false;
        low = std::min (low, g);
        high = std::max (high, g);
      }
      if (high - low > relief)
        return false;
      for (const mov::Trunk& trunk : m_trunk_field.gather (at, room + 2.0f)) {
        const float dx = trunk.root[0] - at[0];
        const float dz = trunk.root[2] - at[2];
        const float clear = room + trunk.radius;
        if (dx * dx + dz * dz < clear * clear)
          return false;
      }
      return true;
    }

    Vec3 MoppeGame::open_ground_near (const Vec3& want,
                                      float room,
                                      float relief,
                                      float search) const {
      for (float r = 0.0f; r <= search; r += 1.5f) {
        const int around =
          std::max (1, static_cast<int> (6.2831853f * r / 1.5f));
        for (int k = 0; k < around; ++k) {
          const float a = 6.2831853f * k / around;
          const Vec3 at = want + Vec3 (std::cos (a), 0.0f, std::sin (a)) * r;
          if (open_ground (at, room, relief))
            return at;
        }
      }
      return want;
    }

    std::pair<Vec3, Vec3> MoppeGame::bike_frame () const {
      Vec3 heading = session ().bike ().orientation ();
      heading[1] = 0.0f;
      heading =
        length2 (heading) > 1e-6f ? normalized (heading) : Vec3 (0, 0, 1);
      return { heading, Vec3 (heading[2], 0, -heading[0]) };
    }

    Vec3 MoppeGame::near_spawn (Vec3 at) const {
      const float px =
        surface ().domain ().period_x ().numerical_value_in (u::m);
      const float pz =
        surface ().domain ().period_z ().numerical_value_in (u::m);
      at[0] -= px * std::round ((at[0] - m_spawn_position[0]) / px);
      at[2] -= pz * std::round ((at[2] - m_spawn_position[2]) / pz);
      at[1] = ground_height (at);
      return at;
    }

    bool MoppeGame::clear_of_machines (const Vec3& at, float room) const {
      for (const Vec3& other : { session ().jeep ().position (),
                                 session ().long_john ().position (),
                                 session ().mountain_bike ().position () })
        if (length2 (Vec3 (other[0] - at[0], 0.0f, other[2] - at[2])) <
            (room + 3.0f) * (room + 3.0f))
          return false;
      return true;
    }

    float MoppeGame::from_spawn (const Vec3& at) const {
      return length (
        Vec3 (at[0] - m_spawn_position[0], 0.0f, at[2] - m_spawn_position[2]));
    }

    void MoppeGame::report_parked (const char* name,
                                   const Vec3& at,
                                   const char* where) {
      std::cerr << "moppe: " << name << " parked "
                << std::lround (from_spawn (at)) << " m from the spawn, "
                << where << ", at " << std::lround (at[0]) << ", "
                << std::lround (ground_height (at)) << ", "
                << std::lround (at[2]) << std::endl;
    }

    void MoppeGame::park_jeep () {
      const auto [heading, side] = bike_frame ();
      std::optional<std::pair<Vec3, Vec3>> best;
      float best_score = 1e30f;
      for (const terrain::Road& road : generated_world ().roads ().roads) {
        if (road.kind != terrain::Road::Kind::Road)
          continue;
        for (std::size_t p = 1; p + 1 < road.points.size (); p += 2) {
          const Vec3 a = near_spawn (
            Vec3 (road.points[p - 1].x_m, 0.0f, road.points[p - 1].z_m));
          const Vec3 b = near_spawn (
            Vec3 (road.points[p + 1].x_m, 0.0f, road.points[p + 1].z_m));
          Vec3 along (b[0] - a[0], 0.0f, b[2] - a[2]);
          if (length2 (along) < 1e-4f)
            continue;
          along = normalized (along);
          const Vec3 mid =
            near_spawn (Vec3 (road.points[p].x_m, 0.0f, road.points[p].z_m));
          const float r = from_spawn (mid);
          if (r < 150.0f || r > 1200.0f)
            continue;
          // Nearest the trailhead wins, a road still in use before one
          // gone over.
          const float score = std::abs (r - 300.0f) +
                              (road.abandoned_years_ago > 0.0f ? 600.0f : 0.0f);
          if (score >= best_score)
            continue;
          const Vec3 across (along[2], 0.0f, -along[0]);
          for (const float offset : { 9.0f, -9.0f, 12.0f, -12.0f }) {
            const Vec3 at = mid + across * offset;
            if (!open_ground (at, 2.6f, 0.9f))
              continue;
            best_score = score;
            best = {
              at, normalized (along + across * (offset > 0 ? 0.25f : -0.25f))
            };
            break;
          }
        }
      }
      if (best) {
        const Vec3 at =
          open_ground_near (best->first + m_park_aside, 2.6f, 0.9f, 12.0f);
        session ().jeep ().place (at, best->second);
        report_parked ("jeep", at, "beside a road");
        return;
      }
      const Vec3 at = open_ground_near (session ().bike ().position () +
                                          side * 11.0f - heading * 4.0f,
                                        2.6f,
                                        0.7f);
      session ().jeep ().place (at, normalized (heading + side * 0.35f));
      report_parked ("jeep", at, "beside the bike (no road near)");
    }

    void MoppeGame::park_tractor () {
      const auto [heading, side] = bike_frame ();
      session ().tractor ().set_trunks (&m_trunk_field);
      const Vec3 facing = normalized (-side - heading * 0.6f);
      const Vec3 room = open_ground_near (session ().bike ().position () -
                                            side * 17.0f + heading * 15.0f,
                                          3.4f,
                                          0.8f,
                                          40.0f);
      const Vec3 at = room + facing * 1.7f;
      session ().tractor ().place (at, facing);
      report_parked ("tractor", at, "at the trailhead with its trailer");
    }

    void MoppeGame::park_caddy () {
      // Off the helicopter's line.
      logic ().m_sling = {};
      session ().caddy ().set_trunks (&m_trunk_field);
      const float water = world ().water_level.numerical_value_in (u::m);
      if (m_mooring) {
        // Level ground near the shore first; failing that, out to a few
        // hundred metres along the coast, on ground a little less level.
        const Vec3 shore = *m_mooring + m_park_aside;
        for (const auto [reach, relief] :
             { std::pair (90.0f, 0.45f), std::pair (320.0f, 0.8f) })
          for (float r = 12.0f; r <= reach; r += 2.0f) {
            const int around = static_cast<int> (6.2831853f * r / 2.0f);
            for (int k = 0; k < around; ++k) {
              const float a = 6.2831853f * k / around;
              const Vec3 at =
                shore + Vec3 (std::cos (a), 0.0f, std::sin (a)) * r;
              if (ground_height (at) < water + 1.0f ||
                  !open_ground (at, 3.2f, relief) ||
                  !clear_of_machines (at, 3.2f))
                continue;
              const Vec3 to_sea =
                normalized (Vec3 (shore[0] - at[0], 0.0f, shore[2] - at[2]));
              session ().caddy ().place (at, to_sea);
              report_parked ("Caddy", at, "up from the boat's shore");
              return;
            }
          }
      }
      const auto [heading, side] = bike_frame ();
      const Vec3 at = open_ground_near (session ().bike ().position () +
                                          side * 11.0f - heading * 11.0f,
                                        3.2f,
                                        0.6f);
      session ().caddy ().place (at, normalized (heading - side * 0.12f));
      report_parked ("Caddy", at, "at the trailhead (no shore found)");
    }

    void MoppeGame::park_mountain_bike () {
      const auto [heading, side] = bike_frame ();
      const auto& points = trail_network ().alignment.points;
      const float start = ground_height (m_spawn_position);
      std::optional<std::pair<Vec3, Vec3>> top;
      float best = start + 15.0f;
      for (std::size_t p = 0; p + 1 < points.size (); ++p) {
        const Vec3 at = near_spawn (Vec3 (points[p].x_m, 0.0f, points[p].z_m));
        const float r = from_spawn (at);
        if (r < 120.0f || r > 1000.0f)
          continue;
        // Higher is better, but not at the price of a long walk.
        const float score = at[1] - 0.03f * r;
        if (score <= best)
          continue;
        const Vec3 next =
          near_spawn (Vec3 (points[p + 1].x_m, 0.0f, points[p + 1].z_m));
        Vec3 along (next[0] - at[0], 0.0f, next[2] - at[2]);
        if (length2 (along) < 1e-4f)
          continue;
        along = normalized (along);
        const Vec3 across (along[2], 0.0f, -along[0]);
        // Down the way the trail falls from here.
        const Vec3 down = next[1] < at[1] ? along : -along;
        for (const float offset : { 3.0f, -3.0f, 4.5f, -4.5f }) {
          const Vec3 stand = at + across * offset;
          if (!open_ground (stand, 1.0f, 0.5f))
            continue;
          best = score;
          top = { stand, normalized (down + across * 0.3f) };
          break;
        }
      }
      // Without a climb on the trail, a lookout: the highest open ground
      // within a walk, facing back down toward the trailhead.
      const char* where = "at the top of the trail's climb";
      if (!top) {
        float highest = start + 8.0f;
        for (float r = 150.0f; r <= 700.0f; r += 25.0f)
          for (int k = 0; k < 32; ++k) {
            const float a = 6.2831853f * k / 32.0f;
            Vec3 at =
              m_spawn_position + Vec3 (std::cos (a), 0.0f, std::sin (a)) * r;
            at[1] = ground_height (at);
            if (at[1] - 0.03f * r <= highest || !open_ground (at, 1.0f, 0.5f))
              continue;
            highest = at[1] - 0.03f * r;
            const Vec3 home = normalized (Vec3 (
              m_spawn_position[0] - at[0], 0.0f, m_spawn_position[2] - at[2]));
            top = { at, home };
            where = "at a lookout above the trailhead";
          }
      }
      mov::Vehicle& mtb = session ().mountain_bike ();
      mtb.set_trunks (&m_trunk_field);
      if (top) {
        Vec3 at =
          open_ground_near (top->first + m_park_aside, 1.0f, 0.5f, 8.0f);
        report_parked ("mountainbike", at, where);
        at[1] = ground_height (at) +
                meters_value (mov::mountain_bike_rig.ride_height ()) + 0.02f;
        mtb.set_heading (top->second);
        mtb.reset (at);
        return;
      }
      Vec3 at = open_ground_near (session ().bike ().position () - side * 5.5f -
                                    heading * 3.5f,
                                  1.0f,
                                  0.4f,
                                  12.0f);
      at[1] = ground_height (at) +
              meters_value (mov::mountain_bike_rig.ride_height ()) + 0.02f;
      mtb.set_heading (normalized (heading + side * 0.2f));
      report_parked (
        "mountainbike", at, "beside the bike (the trail does not climb)");
      mtb.reset (at);
    }

    void MoppeGame::park_long_john () {
      const auto [heading, side] = bike_frame ();
      mov::Vehicle& lj = session ().long_john ();
      lj.set_trunks (&m_trunk_field);
      std::optional<Vec3> hamlet;
      const char* where = "in the nearest hamlet";
      // Best of all outside the door of a farmhouse still lived in.
      for (const Farm& farm : m_places.farms) {
        if (!farm.kept || farm.ruined)
          continue;
        for (const Building& building : farm.buildings) {
          if (building.kind != Building::Kind::house)
            continue;
          const Vec3 door = near_spawn (building.at + building.forward * 6.0f);
          const float r = from_spawn (door);
          if (r > 3000.0f || (hamlet && r >= from_spawn (*hamlet)))
            continue;
          hamlet = door;
          where = "outside a farmhouse";
        }
      }
      for (const terrain::RoadPlace& place :
           generated_world ().roads ().places) {
        if (hamlet && std::string_view (where) == "outside a farmhouse")
          break;
        using Kind = terrain::RoadPlace::Kind;
        if (place.kind != Kind::LakeOutlet && place.kind != Kind::Landing)
          continue;
        const Vec3 at = near_spawn (Vec3 (place.x_m, 0.0f, place.z_m));
        const float r = from_spawn (at);
        if (r > 3000.0f || (hamlet && r >= from_spawn (*hamlet)))
          continue;
        hamlet = at;
      }
      if (hamlet) {
        Vec3 at = open_ground_near (*hamlet + m_park_aside, 1.8f, 0.4f, 60.0f);
        if (open_ground (at, 1.8f, 0.4f)) {
          report_parked ("Long John", at, where);
          const Vec3 home = Vec3 (
            m_spawn_position[0] - at[0], 0.0f, m_spawn_position[2] - at[2]);
          at[1] = ground_height (at) +
                  meters_value (mov::long_john_rig.ride_height ()) + 0.02f;
          lj.set_heading (length2 (home) > 1e-4f ? normalized (home) : heading);
          lj.reset (at);
          return;
        }
      }
      Vec3 at = open_ground_near (session ().bike ().position () -
                                    heading * 24.0f + side * 4.0f,
                                  1.8f,
                                  0.3f,
                                  20.0f);
      at[1] = ground_height (at) +
              meters_value (mov::long_john_rig.ride_height ()) + 0.02f;
      lj.set_heading (normalized (heading - side * 0.15f));
      report_parked ("Long John", at, "down the trail (no hamlet near)");
      lj.reset (at);
    }

    void MoppeGame::park_helicopter (std::optional<Vec3> avoid) {
      const Vec3 centre = m_spawn_position;
      const float water = world ().water_level.numerical_value_in (u::m);
      const float reach = mov::Helicopter::rotor_radius + 2.5f;
      std::optional<Vec3> best;
      float best_score = 1e30f;
      for (float r = 220.0f; r <= 600.0f; r += 6.0f) {
        const int around = static_cast<int> (6.2831853f * r / 6.0f);
        for (int k = 0; k < around; ++k) {
          const float a = 6.2831853f * (k + 0.5f * (int (r) % 2)) / around;
          Vec3 at = centre + Vec3 (std::cos (a), 0.0f, std::sin (a)) * r;
          at[1] = ground_height (at);
          if (at[1] < water + 1.0f)
            continue;
          if (avoid &&
              length2 (Vec3 (at[0] - (*avoid)[0], 0.0f, at[2] - (*avoid)[2])) <
                15.0f * 15.0f)
            continue;
          // Level under the skids and the tail, and not much less so
          // under the rotor's reach.
          float low = at[1], high = at[1], wide = 0.0f;
          for (int i = 0; i < 12; ++i) {
            const float b = 6.2831853f * i / 12.0f;
            const Vec3 out (std::cos (b), 0.0f, std::sin (b));
            const float inner = ground_height (at + out * 3.0f);
            const float outer = ground_height (at + out * 6.5f);
            low = std::min (low, inner);
            high = std::max (high, inner);
            wide = std::max (wide, std::abs (outer - at[1]));
          }
          if (high - low > 0.6f || wide > 1.6f)
            continue;
          // How far it stands over the ground some way round.
          float around_it = 0.0f;
          for (int i = 0; i < 8; ++i) {
            const float b = 6.2831853f * (i + 0.5f) / 8.0f;
            around_it += ground_height (
              at + Vec3 (std::cos (b), 0.0f, std::sin (b)) * 30.0f);
          }
          const float rise = at[1] - around_it / 8.0f;
          const float score = (r - 220.0f) * 0.004f + (high - low) * 4.0f +
                              wide - std::clamp (rise, -2.0f, 6.0f) * 0.25f;
          if (score >= best_score)
            continue;
          bool clear = true;
          for (const mov::Trunk& trunk :
               m_trunk_field.gather (at, reach + 1.0f)) {
            const float dx = trunk.root[0] - at[0];
            const float dz = trunk.root[2] - at[2];
            const float room = reach + trunk.radius;
            if (dx * dx + dz * dz < room * room) {
              clear = false;
              break;
            }
          }
          if (!clear)
            continue;
          best_score = score;
          best = at;
        }
        // Somewhere in the nearer half of the band wins over a far
        // better one further off.
        if (best && r > 380.0f)
          break;
      }
      Vec3 heading = trail_direction_from_home ();
      Vec3 at;
      if (best) {
        at = *best;
        // Side on to the spawn, so it shows its whole length from there.
        const Vec3 to =
          normalized (Vec3 (at[0] - centre[0], 0.0f, at[2] - centre[2]));
        heading = Vec3 (to[2], 0.0f, -to[0]);
      } else {
        heading[1] = 0.0f;
        heading =
          length2 (heading) > 1e-6f ? normalized (heading) : Vec3 (0, 0, 1);
        at = centre - Vec3 (heading[2], 0.0f, -heading[0]) * 14.0f;
      }
      session ().helicopter ().set_trunks (&m_trunk_field);
      session ().helicopter ().place (at, heading);
      std::cerr << "moppe: helicopter parked "
                << length (Vec3 (at[0] - centre[0], 0.0f, at[2] - centre[2]))
                << " m from the spawn" << (best ? "" : " (no clear site found)")
                << std::endl;
    }
  }
}
