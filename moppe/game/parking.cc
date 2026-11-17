// Where the machines stand when a world begins, the boats' moorings, the
// water autopilot, and the stuck tractor's rescue scene (parking.hh).

#include <moppe/environment.hh>
#include <moppe/game/cinematic_flight.hh>
#include <moppe/game/parking.hh>
#include <moppe/mov/helicopter.hh>
#include <moppe/terrain/river.hh>

#include <algorithm>
#include <cmath>
#include <iostream>
#include <string_view>

namespace moppe::game {
  void Parking::park_boat (const ParkingLot& lot, bool outward) {
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
    if (river && moor_on_river (lot))
      return;
    if (!lakes_first && lot.places.landing) {
      // At the end of the road, alongside the landing's jetty.
      const Landing& landing = *lot.places.landing;
      mov::Boat& boat = lot.session.boat ();
      boat.set_trunks (&lot.trunks);
      const position_t& berth_at = landing.berth;
      const direction_t forward =
        outward ? -landing.berth_forward : landing.berth_forward;
      const Vec3& berth = position_value (berth_at);
      boat.place (berth, direction_value (forward), berth[1] * u::m);
      m_mooring = position_value (landing.jetty.root);
      m_mooring_out = direction_value (landing.jetty.out);
      std::cerr << "moppe: boat moored at the landing's jetty, "
                << std::hypot (berth[0] - lot.spawn[0], berth[2] - lot.spawn[2])
                << " m from the spawn, at " << berth[0] << ' ' << berth[2]
                << std::endl;
      return;
    }
    if (!lakes_first && moor_boat (lot, true, outward))
      return;
    if (!moor_boat (lot, false, outward))
      std::cerr << "moppe: boat: no open water near the spawn\n";
  }

  void Parking::park_jet_ski (const ParkingLot& lot, bool outward) {
    mov::JetSki& ski = lot.session.jet_ski ();
    ski.set_trunks (&lot.trunks);
    // Where the boat was left, not where someone has since taken it.
    const mov::Boat& boat = lot.session.boat ();
    if (lot.session.logic ().m_doing != Doing::driving (Machine::boat) ||
        !m_boat_berth)
      m_boat_berth = { boat.position (), boat.forward () };
    const Vec3 berth = m_boat_berth->first;
    Vec3 bow = m_boat_berth->second;
    bow[1] = 0.0f;
    bow = length2 (bow) > 1e-6f ? normalized (bow) : Vec3 (0, 0, 1);
    const Vec3 beam (bow[2], 0.0f, -bow[0]);
    std::vector<Vec3> berths;
    if (lot.places.landing && m_mooring &&
        length2 (*m_mooring - position_value (lot.places.landing->jetty.root)) <
          1.0f) {
      // Across the jetty from the boat, a little nearer its head.
      const Jetty& jetty = lot.places.landing->jetty;
      const Vec3 side (
        direction_value (jetty.out)[2], 0.0f, -direction_value (jetty.out)[0]);
      const float across = dot (berth - position_value (jetty.root), side);
      berths.push_back (
        berth -
        side * (across + std::copysign (meters_value (jetty.half_width) + 0.8f,
                                        across)) +
        direction_value (jetty.out) * 0.9f);
    }
    for (const float aside : { 3.2f, -3.2f, 5.5f, -5.5f })
      berths.push_back (berth + beam * aside);
    for (const float along : { -5.0f, 5.5f })
      berths.push_back (berth + bow * along);
    const Vec3 facing = outward && m_mooring_out ? *m_mooring_out : bow;
    for (const Vec3& at : berths)
      if (lot.ground.depth (at) > 0.5f) {
        ski.place (
          at, facing, (lot.ground.depth (at) + lot.ground.height (at)) * u::m);
        std::cerr << "moppe: jet ski afloat "
                  << std::lround (from_spawn (lot.spawn, at))
                  << " m from the spawn, at " << at[0] << ' ' << at[2]
                  << std::endl;
        return;
      }
    if (!m_mooring) {
      std::cerr << "moppe: jet ski: no water near the spawn\n";
      return;
    }
    // No water round the boat deep enough: drawn up on its shore.
    ski.place (*m_mooring, facing, -1e6f * u::m);
    std::cerr << "moppe: jet ski drawn up on the boat's shore, at "
              << (*m_mooring)[0] << ' ' << (*m_mooring)[2] << std::endl;
  }

  bool Parking::moor_on_river (const ParkingLot& lot) {
    // The course from its head down to the mouth, unwrapped about the
    // periodic world.
    const float period[2] = {
      lot.ground.surface ().domain ().period_x ().numerical_value_in (u::m),
      lot.ground.surface ().domain ().period_z ().numerical_value_in (u::m),
    };
    const auto course = greatest_river_course (
      std::get<terrain::LakeCensus> (lot.ground.generated.hydrology ()),
      std::get<terrain::RiverNetwork> (lot.ground.generated.hydrology ()));
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
    constexpr float most = 1200.0f;
    std::size_t start = line.size () - 1;
    float up = 0.0f;
    while (start > 0 && (up < most || lot.ground.depth (line[start]) < 0.6f)) {
      up += length (line[start] - line[start - 1]);
      --start;
    }
    if (start + 2 >= line.size ())
      return false;
    const Vec3 at = line[start];
    const Vec3 ahead = normalized (line[start + 1] - at);
    mov::Boat& boat = lot.session.boat ();
    boat.set_trunks (&lot.trunks);
    boat.place (
      at, ahead, (lot.ground.depth (at) + lot.ground.height (at)) * u::m);
    m_mooring = at;
    m_mooring_out = ahead;
    m_boat_route.assign (line.begin () + start + 1, line.end ());
    std::cerr << "moppe: boat moored on the river " << up
              << " m above its mouth, at " << at[0] << ' ' << at[2]
              << std::endl;
    return true;
  }

  InputFrame Parking::water_autopilot (const WorldGround& ground,
                                       float t,
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
        if (ground.depth (at + way * reach) < shallow)
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

  bool Parking::moor_boat (const ParkingLot& lot, bool sea_only, bool outward) {
    const terrain::WaterSheets& sheets = lot.ground.generated.water_surface ();
    const float sea = lot.ground.world.water_level.numerical_value_in (u::m);
    const auto level_at = [&] (const Vec3& at) {
      return still_water_at (
               lot.ground.world, lot.ground.surface (), &sheets, at)
        .numerical_value_in (u::m);
    };
    const auto depth_at = [&] (const Vec3& at) {
      return level_at (at) - lot.ground.height (at);
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
          lot.spawn + Vec3 (std::cos (a), 0.0f, std::sin (a)) * radius;
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
          mov::Boat& boat = lot.session.boat ();
          boat.set_trunks (&lot.trunks);
          boat.place (moor, outward ? out : -out, level_at (moor) * u::m);
          m_mooring = shore;
          m_mooring_out = out;
          std::cerr << "moppe: boat moored "
                    << length (Vec3 (
                         at[0] - lot.spawn[0], 0.0f, at[2] - lot.spawn[2]))
                    << " m from the spawn, on "
                    << (level_at (at) <= sea + 0.05f ? "the sea" : "a lake")
                    << std::endl;
          return true;
        }
      }
    }
    return false;
  }

  void stage_rescue (GameSession& session, const WorldGround& ground) {
    const char* walk = moppe::environment ("MOPPE_WALK");
    if (!walk || session.logic ().m_doing != Doing::on_foot ())
      return;
    const std::string_view script (walk);
    const mov::Tractor& tractor = session.tractor ();
    if (script == "haul") {
      session.logic ()
        .m_trouble[static_cast<std::size_t> (Machine::tractor)] = {
        .anchor = tractor.position (), .strain = 4.0f * u::s, .stuck = true
      };
      Vec3 ahead = tractor.forward ();
      ahead[1] = 0.0f;
      ahead = length2 (ahead) > 1e-6f ? normalized (ahead) : Vec3 (0, 0, 1);
      Vec3 stand = tractor.position () + ahead * 3.0f;
      stand[1] = ground.height (stand) + 0.1f;
      session.walker ().spawn (moppe::position (stand), ahead);
    } else if (script == "bump") {
      Vec3 across = tractor.left ();
      across[1] = 0.0f;
      across = length2 (across) > 1e-6f ? normalized (across) : Vec3 (1, 0, 0);
      session.jeep ().place (tractor.position () + across * 18.0f, -across);
      Vec3 stand = session.jeep ().point (Vec3 (1.75f, 0.0f, -0.1f));
      stand[1] = ground.height (stand) + 0.1f;
      session.walker ().spawn (moppe::position (stand), -across);
    } else {
      return;
    }
    session.logic ().m_fp_eye = position (session.walker ().eye_position ());
    session.settle_camera (ground.surface ());
    std::cerr << "moppe: walk: " << script << " beside the tractor at "
              << tractor.position ()[0] << ", " << tractor.position ()[2]
              << std::endl;
  }

  std::pair<Vec3, Vec3> Parking::bike_frame (const ParkingLot& lot) {
    Vec3 heading = lot.session.bike ().orientation ();
    heading[1] = 0.0f;
    heading = length2 (heading) > 1e-6f ? normalized (heading) : Vec3 (0, 0, 1);
    return { heading, Vec3 (heading[2], 0, -heading[0]) };
  }

  Vec3 near_spawn (const WorldGround& ground, const Vec3& spawn, Vec3 at) {
    const float px =
      ground.surface ().domain ().period_x ().numerical_value_in (u::m);
    const float pz =
      ground.surface ().domain ().period_z ().numerical_value_in (u::m);
    at[0] -= px * std::round ((at[0] - spawn[0]) / px);
    at[2] -= pz * std::round ((at[2] - spawn[2]) / pz);
    at[1] = ground.height (at);
    return at;
  }

  bool Parking::clear_of_machines (const ParkingLot& lot,
                                   const Vec3& at,
                                   float room) {
    for (const Vec3& other : { lot.session.jeep ().position (),
                               lot.session.long_john ().position (),
                               lot.session.mountain_bike ().position () })
      if (length2 (Vec3 (other[0] - at[0], 0.0f, other[2] - at[2])) <
          (room + 3.0f) * (room + 3.0f))
        return false;
    return true;
  }

  float from_spawn (const Vec3& spawn, const Vec3& at) {
    return length (Vec3 (at[0] - spawn[0], 0.0f, at[2] - spawn[2]));
  }

  void Parking::report_parked (const ParkingLot& lot,
                               const char* name,
                               const Vec3& at,
                               const char* where) {
    std::cerr << "moppe: " << name << " parked "
              << std::lround (from_spawn (lot.spawn, at))
              << " m from the spawn, " << where << ", at "
              << std::lround (at[0]) << ", "
              << std::lround (lot.ground.height (at)) << ", "
              << std::lround (at[2]) << std::endl;
  }

  void Parking::park_jeep (const ParkingLot& lot) const {
    const auto [heading, side] = bike_frame (lot);
    std::optional<std::pair<Vec3, Vec3>> best;
    float best_score = 1e30f;
    for (const terrain::Road& road : lot.ground.generated.roads ().roads) {
      if (road.kind != terrain::Road::Kind::Road)
        continue;
      for (std::size_t p = 1; p + 1 < road.points.size (); p += 2) {
        const Vec3 a = near_spawn (
          lot.ground, lot.spawn, lift (plan_value (road.points[p - 1])));
        const Vec3 b = near_spawn (
          lot.ground, lot.spawn, lift (plan_value (road.points[p + 1])));
        Vec3 along (b[0] - a[0], 0.0f, b[2] - a[2]);
        if (length2 (along) < 1e-4f)
          continue;
        along = normalized (along);
        const Vec3 mid = near_spawn (
          lot.ground, lot.spawn, lift (plan_value (road.points[p])));
        const float r = from_spawn (lot.spawn, mid);
        if (r < 150.0f || r > 1200.0f)
          continue;
        // Nearest the trailhead wins, a road still in use before one
        // gone over.
        const float score =
          std::abs (r - 300.0f) + (road.abandoned > age_t {} ? 600.0f : 0.0f);
        if (score >= best_score)
          continue;
        const Vec3 across (along[2], 0.0f, -along[0]);
        for (const float offset : { 9.0f, -9.0f, 12.0f, -12.0f }) {
          const Vec3 at = mid + across * offset;
          if (!lot.ground.open (at, 2.6f, 0.9f))
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
        lot.ground.open_near (best->first + m_aside, 2.6f, 0.9f, 12.0f);
      lot.session.jeep ().place (at, best->second);
      report_parked (lot, "jeep", at, "beside a road");
      return;
    }
    const Vec3 at = lot.ground.open_near (lot.session.bike ().position () +
                                            side * 11.0f - heading * 4.0f,
                                          2.6f,
                                          0.7f);
    lot.session.jeep ().place (at, normalized (heading + side * 0.35f));
    report_parked (lot, "jeep", at, "beside the bike (no road near)");
  }

  void Parking::park_tractor (const ParkingLot& lot) const {
    const auto [heading, side] = bike_frame (lot);
    lot.session.tractor ().set_trunks (&lot.trunks);
    const Vec3 facing = normalized (-side - heading * 0.6f);
    const Vec3 room = lot.ground.open_near (lot.session.bike ().position () -
                                              side * 17.0f + heading * 15.0f,
                                            3.4f,
                                            0.8f,
                                            40.0f);
    const Vec3 at = room + facing * 1.7f;
    lot.session.tractor ().place (at, facing);
    report_parked (lot, "tractor", at, "at the trailhead with its trailer");
  }

  void Parking::park_caddy (const ParkingLot& lot) const {
    // Off the helicopter's line.
    lot.session.logic ().m_sling = {};
    lot.session.caddy ().set_trunks (&lot.trunks);
    const float water = lot.ground.world.water_level.numerical_value_in (u::m);
    if (m_mooring) {
      // Level ground near the shore first; failing that, out to a few
      // hundred metres along the coast, on ground a little less level.
      const Vec3 shore = *m_mooring + m_aside;
      for (const auto [reach, relief] :
           { std::pair (90.0f, 0.45f), std::pair (320.0f, 0.8f) })
        for (float r = 12.0f; r <= reach; r += 2.0f) {
          const int around = static_cast<int> (6.2831853f * r / 2.0f);
          for (int k = 0; k < around; ++k) {
            const float a = 6.2831853f * k / around;
            const Vec3 at = shore + Vec3 (std::cos (a), 0.0f, std::sin (a)) * r;
            if (lot.ground.height (at) < water + 1.0f ||
                !lot.ground.open (at, 3.2f, relief) ||
                !clear_of_machines (lot, at, 3.2f))
              continue;
            const Vec3 to_sea =
              normalized (Vec3 (shore[0] - at[0], 0.0f, shore[2] - at[2]));
            lot.session.caddy ().place (at, to_sea);
            report_parked (lot, "Caddy", at, "up from the boat's shore");
            return;
          }
        }
    }
    const auto [heading, side] = bike_frame (lot);
    const Vec3 at = lot.ground.open_near (lot.session.bike ().position () +
                                            side * 11.0f - heading * 11.0f,
                                          3.2f,
                                          0.6f);
    lot.session.caddy ().place (at, normalized (heading - side * 0.12f));
    report_parked (lot, "Caddy", at, "at the trailhead (no shore found)");
  }

  void Parking::park_mountain_bike (const ParkingLot& lot) const {
    const auto [heading, side] = bike_frame (lot);
    const auto& points = lot.ground.generated.trails ().alignment.points;
    const float start = lot.ground.height (lot.spawn);
    std::optional<std::pair<Vec3, Vec3>> top;
    float best = start + 15.0f;
    for (std::size_t p = 0; p + 1 < points.size (); ++p) {
      const Vec3 at =
        near_spawn (lot.ground, lot.spawn, lift (plan_value (points[p]), 0.0f));
      const float r = from_spawn (lot.spawn, at);
      if (r < 120.0f || r > 1000.0f)
        continue;
      // Higher is better, but not at the price of a long walk.
      const float score = at[1] - 0.03f * r;
      if (score <= best)
        continue;
      const Vec3 next = near_spawn (
        lot.ground, lot.spawn, lift (plan_value (points[p + 1]), 0.0f));
      Vec3 along (next[0] - at[0], 0.0f, next[2] - at[2]);
      if (length2 (along) < 1e-4f)
        continue;
      along = normalized (along);
      const Vec3 across (along[2], 0.0f, -along[0]);
      // Down the way the trail falls from here.
      const Vec3 down = next[1] < at[1] ? along : -along;
      for (const float offset : { 3.0f, -3.0f, 4.5f, -4.5f }) {
        const Vec3 stand = at + across * offset;
        if (!lot.ground.open (stand, 1.0f, 0.5f))
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
          Vec3 at = lot.spawn + Vec3 (std::cos (a), 0.0f, std::sin (a)) * r;
          at[1] = lot.ground.height (at);
          if (at[1] - 0.03f * r <= highest || !lot.ground.open (at, 1.0f, 0.5f))
            continue;
          highest = at[1] - 0.03f * r;
          const Vec3 home = normalized (
            Vec3 (lot.spawn[0] - at[0], 0.0f, lot.spawn[2] - at[2]));
          top = { at, home };
          where = "at a lookout above the trailhead";
        }
    }
    mov::Vehicle& mtb = lot.session.mountain_bike ();
    mtb.set_trunks (&lot.trunks);
    if (top) {
      Vec3 at = lot.ground.open_near (top->first + m_aside, 1.0f, 0.5f, 8.0f);
      report_parked (lot, "mountainbike", at, where);
      at[1] = lot.ground.height (at) +
              meters_value (mov::mountain_bike_rig.ride_height ()) + 0.02f;
      mtb.set_heading (top->second);
      mtb.reset (at);
      return;
    }
    Vec3 at = lot.ground.open_near (lot.session.bike ().position () -
                                      side * 5.5f - heading * 3.5f,
                                    1.0f,
                                    0.4f,
                                    12.0f);
    at[1] = lot.ground.height (at) +
            meters_value (mov::mountain_bike_rig.ride_height ()) + 0.02f;
    mtb.set_heading (normalized (heading + side * 0.2f));
    report_parked (
      lot, "mountainbike", at, "beside the bike (the trail does not climb)");
    mtb.reset (at);
  }

  void Parking::park_long_john (const ParkingLot& lot) const {
    const auto [heading, side] = bike_frame (lot);
    mov::Vehicle& lj = lot.session.long_john ();
    lj.set_trunks (&lot.trunks);
    std::optional<Vec3> hamlet;
    const char* where = "in the nearest hamlet";
    // Best of all outside the door of a farmhouse still lived in.
    for (const Farm& farm : lot.places.farms) {
      if (!farm.kept || farm.ruined)
        continue;
      for (const Building& building : farm.buildings) {
        if (building.kind != Building::Kind::house)
          continue;
        const Vec3 door =
          near_spawn (lot.ground,
                      lot.spawn,
                      position_value (building.at) +
                        direction_value (building.forward) * 6.0f);
        const float r = from_spawn (lot.spawn, door);
        if (r > 3000.0f || (hamlet && r >= from_spawn (lot.spawn, *hamlet)))
          continue;
        hamlet = door;
        where = "outside a farmhouse";
      }
    }
    for (const terrain::RoadPlace& place :
         lot.ground.generated.roads ().places) {
      if (hamlet && std::string_view (where) == "outside a farmhouse")
        break;
      using Kind = terrain::RoadPlace::Kind;
      if (place.kind != Kind::LakeOutlet && place.kind != Kind::Landing)
        continue;
      const Vec3 at =
        near_spawn (lot.ground, lot.spawn, lift (plan_value (place.at)));
      const float r = from_spawn (lot.spawn, at);
      if (r > 3000.0f || (hamlet && r >= from_spawn (lot.spawn, *hamlet)))
        continue;
      hamlet = at;
    }
    if (hamlet) {
      Vec3 at = lot.ground.open_near (*hamlet + m_aside, 1.8f, 0.4f, 60.0f);
      if (lot.ground.open (at, 1.8f, 0.4f)) {
        report_parked (lot, "Long John", at, where);
        const Vec3 home =
          Vec3 (lot.spawn[0] - at[0], 0.0f, lot.spawn[2] - at[2]);
        at[1] = lot.ground.height (at) +
                meters_value (mov::long_john_rig.ride_height ()) + 0.02f;
        lj.set_heading (length2 (home) > 1e-4f ? normalized (home) : heading);
        lj.reset (at);
        return;
      }
    }
    Vec3 at = lot.ground.open_near (lot.session.bike ().position () -
                                      heading * 24.0f + side * 4.0f,
                                    1.8f,
                                    0.3f,
                                    20.0f);
    at[1] = lot.ground.height (at) +
            meters_value (mov::long_john_rig.ride_height ()) + 0.02f;
    lj.set_heading (normalized (heading - side * 0.15f));
    report_parked (lot, "Long John", at, "down the trail (no hamlet near)");
    lj.reset (at);
  }

  void Parking::park_helicopter (const ParkingLot& lot,
                                 std::optional<Vec3> avoid) const {
    const Vec3 centre = lot.spawn;
    const float water = lot.ground.world.water_level.numerical_value_in (u::m);
    const float reach = mov::Helicopter::rotor_radius + 2.5f;
    std::optional<Vec3> best;
    float best_score = 1e30f;
    for (float r = 220.0f; r <= 600.0f; r += 6.0f) {
      const int around = static_cast<int> (6.2831853f * r / 6.0f);
      for (int k = 0; k < around; ++k) {
        const float a = 6.2831853f * (k + 0.5f * (int (r) % 2)) / around;
        Vec3 at = centre + Vec3 (std::cos (a), 0.0f, std::sin (a)) * r;
        at[1] = lot.ground.height (at);
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
          const float inner = lot.ground.height (at + out * 3.0f);
          const float outer = lot.ground.height (at + out * 6.5f);
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
          around_it += lot.ground.height (
            at + Vec3 (std::cos (b), 0.0f, std::sin (b)) * 30.0f);
        }
        const float rise = at[1] - around_it / 8.0f;
        const float score = (r - 220.0f) * 0.004f + (high - low) * 4.0f + wide -
                            std::clamp (rise, -2.0f, 6.0f) * 0.25f;
        if (score >= best_score)
          continue;
        bool clear = true;
        for (const mov::Trunk& trunk : lot.trunks.gather (at, reach + 1.0f)) {
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
    Vec3 heading = direction_value (lot.ground.trail_heading ());
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
    lot.session.helicopter ().set_trunks (&lot.trunks);
    lot.session.helicopter ().place (at, heading);
    std::cerr << "moppe: helicopter parked "
              << length (Vec3 (at[0] - centre[0], 0.0f, at[2] - centre[2]))
              << " m from the spawn" << (best ? "" : " (no clear site found)")
              << std::endl;
  }
}
