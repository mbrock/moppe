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
  namespace {
    // `at` moved by `by`.
    position_t moved (const position_t& at, const displacement_t& by) {
      return at + quantity_cast<isq::position_vector> (by);
    }

    // How high `at` stands.
    meters_t y_of (const position_t& at) {
      return position_value (at)[1] * u::m;
    }

    // `way` on the level, or `otherwise` where it stands near upright.
    direction_t on_the_level (Vec3 way, const Vec3& otherwise) {
      way[1] = 0.0f;
      return unit_direction (length2 (way) > 1e-6f ? normalized (way)
                                                   : otherwise);
    }

    // The level way along a plan displacement.
    direction_t heading_along (const plan_displacement_t& way) {
      return unit_direction (lift (normalized (plan_value (way))));
    }

    // The level way a quarter turn to the left of `way`.
    direction_t left_of (const direction_t& way) {
      const Vec3& v = direction_value (way);
      return unit_direction (Vec3 (v[2], 0.0f, -v[0]));
    }

    // `way` turned toward `other` by `share` of it.
    direction_t
    toward (const direction_t& way, const direction_t& other, float share) {
      return unit_direction (
        normalized (direction_value (way) + direction_value (other) * share));
    }

    // The open ground nearest `want`, at `want`'s height.
    position_t open_ground_near (const WorldGround& ground,
                                 const position_t& want,
                                 meters_t room,
                                 meters_t relief,
                                 meters_t search = 30.0f * u::m) {
      return at_height (ground.open_near (plan (want), room, relief, search),
                        y_of (want));
    }

    // The still water's level over `at`, as its depth over the ground
    // and the ground's height again.
    meters_t afloat_level (const WorldGround& ground,
                           const plan_position_t& at) {
      return terrain::surface_elevation_value (ground.water_depth (at) +
                                               ground.elevation (at)) *
             u::m;
    }

    // Where a bike's frame stands at `at`: its ride height and a little
    // over the ground.
    position_t on_wheels (const WorldGround& ground,
                          const plan_position_t& at,
                          meters_t ride_height) {
      return terrain::at_elevation (
        at, ground.elevation (at) + ride_height + 0.02f * u::m);
    }
  }

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
      boat.place (moppe::position (landing.berth),
                  unit_direction (outward ? -landing.berth_forward
                                          : landing.berth_forward),
                  landing.berth[1] * u::m);
      m_mooring = moppe::position (landing.jetty.root);
      m_mooring_out = unit_direction (landing.jetty.out);
      const Vec3& spawn = position_value (lot.spawn);
      std::cerr << "moppe: boat moored at the landing's jetty, "
                << std::hypot (landing.berth[0] - spawn[0],
                               landing.berth[2] - spawn[2])
                << " m from the spawn, at " << landing.berth[0] << ' '
                << landing.berth[2] << std::endl;
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
    const position_t berth = m_boat_berth->first;
    const direction_t bow =
      on_the_level (direction_value (m_boat_berth->second), Vec3 (0, 0, 1));
    const direction_t beam = left_of (bow);
    std::vector<position_t> berths;
    if (lot.places.landing && m_mooring &&
        length2 (position_value (*m_mooring) - lot.places.landing->jetty.root) <
          1.0f) {
      // Across the jetty from the boat, a little nearer its head.
      const Jetty& jetty = lot.places.landing->jetty;
      const direction_t out = unit_direction (jetty.out);
      const direction_t side = left_of (out);
      const meters_t across =
        dot (position_value (berth) - jetty.root, direction_value (side)) *
        u::m;
      const meters_t beyond =
        across +
        std::copysign (jetty.half_width + 0.8f, meters_value (across)) * u::m;
      berths.push_back (
        moved (moved (berth, along (side, -beyond)), along (out, 0.9f * u::m)));
    }
    for (const meters_t aside :
         { 3.2f * u::m, -3.2f * u::m, 5.5f * u::m, -5.5f * u::m })
      berths.push_back (moved (berth, along (beam, aside)));
    for (const meters_t ahead : { -5.0f * u::m, 5.5f * u::m })
      berths.push_back (moved (berth, along (bow, ahead)));
    const direction_t facing = outward && m_mooring_out ? *m_mooring_out : bow;
    for (const position_t& at : berths)
      if (lot.ground.water_depth (plan (at)) > 0.5f * u::m) {
        ski.place (at, facing, afloat_level (lot.ground, plan (at)));
        std::cerr << "moppe: jet ski afloat "
                  << std::lround (meters_value (from_spawn (lot.spawn, at)))
                  << " m from the spawn, at " << position_value (at)[0] << ' '
                  << position_value (at)[2] << std::endl;
        return;
      }
    if (!m_mooring) {
      std::cerr << "moppe: jet ski: no water near the spawn\n";
      return;
    }
    // No water round the boat deep enough: drawn up on its shore.
    ski.place (*m_mooring, facing, -1e6f * u::m);
    std::cerr << "moppe: jet ski drawn up on the boat's shore, at "
              << position_value (*m_mooring)[0] << ' '
              << position_value (*m_mooring)[2] << std::endl;
  }

  bool Parking::moor_on_river (const ParkingLot& lot) {
    // The course from its head down to the mouth, unwrapped about the
    // periodic world.
    const Vec2 period = plan_value (lot.ground.period ());
    const auto course = greatest_river_course (
      std::get<terrain::LakeCensus> (lot.ground.generated.hydrology ()),
      std::get<terrain::RiverNetwork> (lot.ground.generated.hydrology ()));
    std::vector<plan_position_t> line;
    for (auto reach = course.rbegin (); reach != course.rend (); ++reach)
      for (const terrain::RiverAlignmentPoint& point :
           (*reach)->alignment.points) {
        plan_position_t at = plan_position (point.x_m, point.z_m);
        if (!line.empty ()) {
          Vec2& p = plan_value (at);
          const Vec2& last = plan_value (line.back ());
          for (std::size_t axis : { 0u, 1u })
            p[axis] -=
              period[axis] * std::round ((p[axis] - last[axis]) / period[axis]);
        }
        if (line.empty () || length (at - line.back ()) > 4.0f * u::m)
          line.push_back (at);
      }
    if (line.size () < 3)
      return false;
    // So far up from the mouth, and on to where it runs deep.
    const meters_t most = 1200.0f * u::m;
    std::size_t start = line.size () - 1;
    meters_t up = 0.0f * u::m;
    while (start > 0 &&
           (up < most || lot.ground.water_depth (line[start]) < 0.6f * u::m)) {
      up += length (line[start] - line[start - 1]);
      --start;
    }
    if (start + 2 >= line.size ())
      return false;
    const plan_position_t on = line[start];
    const position_t at = at_height (on, 0.0f * u::m);
    const direction_t ahead = heading_along (line[start + 1] - on);
    mov::Boat& boat = lot.session.boat ();
    boat.set_trunks (&lot.trunks);
    boat.place (at, ahead, afloat_level (lot.ground, on));
    m_mooring = at;
    m_mooring_out = ahead;
    for (auto next = line.begin () + start + 1; next != line.end (); ++next)
      m_boat_route.push_back (at_height (*next, 0.0f * u::m));
    std::cerr << "moppe: boat moored on the river " << meters_value (up)
              << " m above its mouth, at " << plan_value (on).x () << ' '
              << plan_value (on).z () << std::endl;
    return true;
  }

  InputFrame Parking::water_autopilot (const WorldGround& ground,
                                       seconds_t t,
                                       const position_t& at,
                                       direction_t heading,
                                       const WaterPilot& craft) {
    const speed_t speed = craft.speed;
    {
      Vec3 level = direction_value (heading);
      level[1] = 0.0f;
      heading = unit_direction (normalized (level));
    }
    const auto turned = [] (const direction_t& way, radians_t angle) {
      const Vec3& v = direction_value (way);
      const float a = radians_value (angle);
      return unit_direction (Vec3 (v[0] * std::cos (a) - v[2] * std::sin (a),
                                   0.0f,
                                   v[0] * std::sin (a) + v[2] * std::cos (a)));
    };
    // How far open water runs along `way`, as far as it looks.
    const bool river =
      craft.follow_route && m_boat_route_at < m_boat_route.size ();
    const meters_t shallow = river ? 0.12f * u::m : 0.8f * u::m;
    const auto open_reach = [&] (const direction_t& way) {
      for (meters_t reach = 6.0f * u::m; reach <= craft.sight;
           reach += 6.0f * u::m)
        if (ground.water_depth (plan (moved (at, along (way, reach)))) <
            shallow)
          return reach;
      return craft.sight + 10.0f * u::m;
    };
    direction_t want {};
    control_signal_t drive = 1.0f * control_signal[one];
    while (river && m_boat_route_at < m_boat_route.size ()) {
      const plan_displacement_t to =
        plan (m_boat_route[m_boat_route_at]) - plan (at);
      if (length (to) > 22.0f * u::m &&
          dot (plan_value (to), plan (direction_value (heading))) > -5.0f)
        break;
      ++m_boat_route_at;
    }
    if (river && m_boat_route_at < m_boat_route.size ()) {
      want = heading_along (plan (m_boat_route[m_boat_route_at]) - plan (at));
      drive = 0.85f * control_signal[one];
    } else {
      // Over open water: the way that runs open furthest near a
      // slowly wandering course.
      const radians_t wander =
        0.5f * std::sin (seconds_value (t) * 0.21f) * u::rad;
      meters_t best = -1e9f * u::m;
      for (int k = -8; k <= 8; ++k) {
        const radians_t a = k * (PI / 16.0f) * u::rad;
        const meters_t score =
          std::min (open_reach (turned (heading, a)), craft.room) -
          40.0f * radians_value (abs (a - wander * 0.6f)) * u::m;
        if (score > best) {
          best = score;
          want = turned (heading, a);
        }
      }
    }
    // Shallows close ahead win over the course; what goes fast sees
    // them from further off and slows to come round.
    const meters_t seen = 2.6f * u::s * speed;
    const meters_t near = std::max (river ? 13.0f * u::m : 20.0f * u::m,
                                    std::min (seen, 60.0f * u::m));
    const meters_t ahead = open_reach (heading);
    if (ahead < near) {
      const meters_t left = open_reach (turned (heading, -0.6f * u::rad));
      const meters_t right = open_reach (turned (heading, 0.6f * u::rad));
      want = turned (heading, (right >= left ? 0.9f : -0.9f) * u::rad);
      if (speed > 8.0f * u::m / u::s)
        drive = (ahead < 0.5f * near ? 0.3f : 0.6f) * control_signal[one];
    }
    const Vec3& h = direction_value (heading);
    const Vec3& w = direction_value (want);
    const Vec3 right = normalized (cross (h, Vec3 (0, 1, 0)));
    const radians_t bearing = std::atan2 (dot (w, right), dot (w, h)) * u::rad;
    return {
      .turn = std::clamp (2.2f * radians_value (bearing), -1.0f, 1.0f) *
              control_signal[one],
      .drive = std::abs (radians_value (bearing)) > 1.2f
                 ? 0.5f * control_signal[one]
                 : drive,
    };
  }

  bool Parking::moor_boat (const ParkingLot& lot, bool sea_only, bool outward) {
    const terrain::WaterSheets& sheets = lot.ground.generated.water_surface ();
    const terrain::SurfaceElevation sea =
      terrain::surface_elevation_point (lot.ground.world.water_level);
    const auto level_at = [&] (const position_t& at) {
      return lot.ground.still_water (plan (at));
    };
    const auto depth_at = [&] (const position_t& at) {
      return lot.ground.water_depth (plan (at));
    };
    // Still water: the sea, or (unless `sea_only`) a lake's sheet
    // that does not run.
    const auto still = [&] (const position_t& at) {
      const position_t where = at_height (plan (at), 0.0f * u::m);
      if (level_at (at) <= sea + 0.05f * u::m)
        return true;
      if (sea_only)
        return false;
      const Vec3 current =
        spatial::sample<terrain::water_velocity> (sheets, where)
          .numerical_value_in (u::m / u::s);
      return length (plan (current)) * u::m / u::s < 0.1f * u::m / u::s;
    };
    // Open water ahead: deep enough to drive in over a good stretch.
    const auto open = [&] (const position_t& from, const direction_t& out) {
      const Vec3& o = direction_value (out);
      int wet = 0;
      for (int k = 0; k < 9; ++k) {
        const float b = 0.6f * (k - 4) / 4.0f;
        const direction_t dir =
          unit_direction (Vec3 (o[0] * std::cos (b) - o[2] * std::sin (b),
                                0.0f,
                                o[0] * std::sin (b) + o[2] * std::cos (b)));
        bool through = true;
        for (const meters_t reach :
             { 15.0f * u::m, 35.0f * u::m, 60.0f * u::m })
          through = through &&
                    depth_at (moved (from, along (dir, reach))) > 0.8f * u::m;
        wet += through;
      }
      return wet >= 7;
    };
    const meters_t step = 5.0f * u::m;
    for (int ring = 0; ring < 800; ++ring) {
      const meters_t radius = step * ring;
      const int count =
        std::max (1,
                  static_cast<int> (2.0f * PI * meters_value (radius) /
                                    meters_value (step)));
      for (int k = 0; k < count; ++k) {
        const float a = 2.0f * PI * k / count;
        const position_t shore = moved (
          lot.spawn,
          along (unit_direction (Vec3 (std::cos (a), 0.0f, std::sin (a))),
                 radius));
        // Dry ground just above the water, not a cliff.
        const terrain::ElevationChange d = depth_at (shore);
        if (d > -0.05f * u::m || d < -0.6f * u::m)
          continue;
        for (int e = 0; e < 16; ++e) {
          const float b = 2.0f * PI * e / 16;
          const direction_t out =
            unit_direction (Vec3 (std::cos (b), 0.0f, std::sin (b)));
          const position_t at = moved (shore, along (out, 5.0f * u::m));
          if (depth_at (at) < 0.45f * u::m ||
              depth_at (moved (shore, along (out, 2.0f * u::m))) >
                0.4f * u::m ||
              !still (at) || !open (at, out))
            continue;
          // Facing out, it lies further off, so that its long tail
          // reaches water deep enough to drive it from the start.
          const position_t further = moved (shore, along (out, 9.0f * u::m));
          const position_t moor =
            outward && depth_at (further) > 0.7f * u::m ? further : at;
          mov::Boat& boat = lot.session.boat ();
          boat.set_trunks (&lot.trunks);
          boat.place (moor,
                      outward ? out : -out,
                      terrain::surface_elevation_value (level_at (moor)) *
                        u::m);
          m_mooring = shore;
          m_mooring_out = out;
          std::cerr << "moppe: boat moored "
                    << meters_value (from_spawn (lot.spawn, at))
                    << " m from the spawn, on "
                    << (level_at (at) <= sea + 0.05f * u::m ? "the sea"
                                                            : "a lake")
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
      const direction_t ahead =
        on_the_level (direction_value (tractor.forward ()), Vec3 (0, 0, 1));
      const plan_position_t stand =
        plan (moved (tractor.position (), along (ahead, 3.0f * u::m)));
      session.walker ().spawn (
        terrain::at_elevation (stand, ground.elevation (stand) + 0.1f * u::m),
        direction_value (ahead));
    } else if (script == "bump") {
      const direction_t across =
        on_the_level (direction_value (tractor.left ()), Vec3 (1, 0, 0));
      session.jeep ().place (
        moved (tractor.position (), along (across, 18.0f * u::m)), -across);
      const plan_position_t stand =
        plan (session.jeep ().point (displacement (Vec3 (1.75f, 0.0f, -0.1f))));
      session.walker ().spawn (
        terrain::at_elevation (stand, ground.elevation (stand) + 0.1f * u::m),
        direction_value (-across));
    } else {
      return;
    }
    session.logic ().m_fp_eye = session.walker ().eye_position ();
    session.settle_camera (ground.surface ());
    std::cerr << "moppe: walk: " << script << " beside the tractor at "
              << position_value (tractor.position ())[0] << ", "
              << position_value (tractor.position ())[2] << std::endl;
  }

  std::pair<direction_t, direction_t>
  Parking::bike_frame (const ParkingLot& lot) {
    const direction_t heading =
      on_the_level (lot.session.bike ().orientation (), Vec3 (0, 0, 1));
    return { heading, left_of (heading) };
  }

  position_t near_spawn (const WorldGround& ground,
                         const position_t& spawn,
                         const position_t& at) {
    const Vec2 period = plan_value (ground.period ());
    const Vec3& s = position_value (spawn);
    Vec3 p = position_value (at);
    p[0] -= period.x () * std::round ((p[0] - s[0]) / period.x ());
    p[2] -= period.z () * std::round ((p[2] - s[2]) / period.z ());
    const plan_position_t on = plan_position (p[0], p[2]);
    return terrain::at_elevation (on, ground.elevation (on));
  }

  bool Parking::clear_of_machines (const ParkingLot& lot,
                                   const position_t& at,
                                   meters_t room) {
    for (const position_t& other :
         { lot.session.jeep ().position (),
           lot.session.long_john ().position (),
           lot.session.mountain_bike ().position () }) {
      const plan_displacement_t apart = plan (other) - plan (at);
      if (dot (apart, apart) < (room + 3.0f * u::m) * (room + 3.0f * u::m))
        return false;
    }
    return true;
  }

  meters_t from_spawn (const position_t& spawn, const position_t& at) {
    return length (plan (at) - plan (spawn));
  }

  void Parking::report_parked (const ParkingLot& lot,
                               const char* name,
                               const position_t& at,
                               const char* where) {
    std::cerr << "moppe: " << name << " parked "
              << std::lround (meters_value (from_spawn (lot.spawn, at)))
              << " m from the spawn, " << where << ", at "
              << std::lround (position_value (at)[0]) << ", "
              << std::lround (terrain::surface_elevation_value (
                   lot.ground.elevation (plan (at))))
              << ", " << std::lround (position_value (at)[2]) << std::endl;
  }

  void Parking::park_jeep (const ParkingLot& lot) const {
    const auto [heading, side] = bike_frame (lot);
    std::optional<std::pair<position_t, direction_t>> best;
    meters_t best_score = 1e30f * u::m;
    for (const terrain::Road& road : lot.ground.generated.roads ().roads) {
      if (road.kind != terrain::Road::Kind::Road)
        continue;
      const auto point = [&] (std::size_t p) {
        return near_spawn (lot.ground,
                           lot.spawn,
                           moppe::position (Vec3 (
                             road.points[p].x_m, 0.0f, road.points[p].z_m)));
      };
      for (std::size_t p = 1; p + 1 < road.points.size (); p += 2) {
        const position_t a = point (p - 1);
        const position_t b = point (p + 1);
        const plan_displacement_t run = plan (b) - plan (a);
        if (length2 (plan_value (run)) < 1e-4f)
          continue;
        const direction_t way = heading_along (run);
        const position_t mid = point (p);
        const meters_t r = from_spawn (lot.spawn, mid);
        if (r < 150.0f * u::m || r > 1200.0f * u::m)
          continue;
        // Nearest the trailhead wins, a road still in use before one
        // gone over.
        const meters_t score =
          abs (r - 300.0f * u::m) +
          (road.abandoned_years_ago > 0.0f ? 600.0f : 0.0f) * u::m;
        if (score >= best_score)
          continue;
        const direction_t across = left_of (way);
        for (const meters_t offset :
             { 9.0f * u::m, -9.0f * u::m, 12.0f * u::m, -12.0f * u::m }) {
          const position_t at = moved (mid, along (across, offset));
          if (!lot.ground.open (plan (at), 2.6f * u::m, 0.9f * u::m))
            continue;
          best_score = score;
          best = {
            at, toward (way, across, offset > 0.0f * u::m ? 0.25f : -0.25f)
          };
          break;
        }
      }
    }
    if (best) {
      const position_t at = open_ground_near (lot.ground,
                                              moved (best->first, m_aside),
                                              2.6f * u::m,
                                              0.9f * u::m,
                                              12.0f * u::m);
      lot.session.jeep ().place (at, best->second);
      report_parked (lot, "jeep", at, "beside a road");
      return;
    }
    const position_t at =
      open_ground_near (lot.ground,
                        moved (moved (lot.session.bike ().position (),
                                      along (side, 11.0f * u::m)),
                               along (heading, -4.0f * u::m)),
                        2.6f * u::m,
                        0.7f * u::m);
    lot.session.jeep ().place (at, toward (heading, side, 0.35f));
    report_parked (lot, "jeep", at, "beside the bike (no road near)");
  }

  void Parking::park_tractor (const ParkingLot& lot) const {
    const auto [heading, side] = bike_frame (lot);
    lot.session.tractor ().set_trunks (&lot.trunks);
    const direction_t facing = toward (-side, heading, -0.6f);
    const position_t clearing =
      open_ground_near (lot.ground,
                        moved (moved (lot.session.bike ().position (),
                                      along (side, -17.0f * u::m)),
                               along (heading, 15.0f * u::m)),
                        3.4f * u::m,
                        0.8f * u::m,
                        40.0f * u::m);
    const position_t at = moved (clearing, along (facing, 1.7f * u::m));
    lot.session.tractor ().place (at, facing);
    report_parked (lot, "tractor", at, "at the trailhead with its trailer");
  }

  void Parking::park_caddy (const ParkingLot& lot) const {
    // Off the helicopter's line.
    lot.session.logic ().m_sling = {};
    lot.session.caddy ().set_trunks (&lot.trunks);
    const terrain::SurfaceElevation water =
      terrain::surface_elevation_point (lot.ground.world.water_level);
    if (m_mooring) {
      // Level ground near the shore first; failing that, out to a few
      // hundred metres along the coast, on ground a little less level.
      const position_t shore = moved (*m_mooring, m_aside);
      for (const auto [reach, relief] :
           { std::pair (90.0f * u::m, 0.45f * u::m),
             std::pair (320.0f * u::m, 0.8f * u::m) })
        for (meters_t r = 12.0f * u::m; r <= reach; r += 2.0f * u::m) {
          const int around =
            static_cast<int> (6.2831853f * meters_value (r) / 2.0f);
          for (int k = 0; k < around; ++k) {
            const float a = 6.2831853f * k / around;
            const position_t at = moved (
              shore,
              along (unit_direction (Vec3 (std::cos (a), 0.0f, std::sin (a))),
                     r));
            if (lot.ground.elevation (plan (at)) < water + 1.0f * u::m ||
                !lot.ground.open (plan (at), 3.2f * u::m, relief) ||
                !clear_of_machines (lot, at, 3.2f * u::m))
              continue;
            const direction_t to_sea = heading_along (plan (shore) - plan (at));
            lot.session.caddy ().place (at, to_sea);
            report_parked (lot, "Caddy", at, "up from the boat's shore");
            return;
          }
        }
    }
    const auto [heading, side] = bike_frame (lot);
    const position_t at =
      open_ground_near (lot.ground,
                        moved (moved (lot.session.bike ().position (),
                                      along (side, 11.0f * u::m)),
                               along (heading, -11.0f * u::m)),
                        3.2f * u::m,
                        0.6f * u::m);
    lot.session.caddy ().place (at, toward (heading, side, -0.12f));
    report_parked (lot, "Caddy", at, "at the trailhead (no shore found)");
  }

  void Parking::park_mountain_bike (const ParkingLot& lot) const {
    const auto [heading, side] = bike_frame (lot);
    const auto& points = lot.ground.generated.trails ().alignment.points;
    const terrain::SurfaceElevation start =
      lot.ground.elevation (plan (lot.spawn));
    const auto point = [&] (std::size_t p) {
      return near_spawn (
        lot.ground,
        lot.spawn,
        moppe::position (Vec3 (points[p].x_m, 0.0f, points[p].z_m)));
    };
    std::optional<std::pair<position_t, direction_t>> top;
    terrain::SurfaceElevation best = start + 15.0f * u::m;
    for (std::size_t p = 0; p + 1 < points.size (); ++p) {
      const position_t at = point (p);
      const meters_t r = from_spawn (lot.spawn, at);
      if (r < 120.0f * u::m || r > 1000.0f * u::m)
        continue;
      // Higher is better, but not at the price of a long walk.
      const terrain::SurfaceElevation score =
        terrain::surface_elevation_point (y_of (at)) - 0.03f * r;
      if (score <= best)
        continue;
      const position_t next = point (p + 1);
      const plan_displacement_t run = plan (next) - plan (at);
      if (length2 (plan_value (run)) < 1e-4f)
        continue;
      const direction_t way = heading_along (run);
      const direction_t across = left_of (way);
      // Down the way the trail falls from here.
      const direction_t down = y_of (next) < y_of (at) ? way : -way;
      for (const meters_t offset :
           { 3.0f * u::m, -3.0f * u::m, 4.5f * u::m, -4.5f * u::m }) {
        const position_t stand = moved (at, along (across, offset));
        if (!lot.ground.open (plan (stand), 1.0f * u::m, 0.5f * u::m))
          continue;
        best = score;
        top = { stand, toward (down, across, 0.3f) };
        break;
      }
    }
    // Without a climb on the trail, a lookout: the highest open ground
    // within a walk, facing back down toward the trailhead.
    const char* where = "at the top of the trail's climb";
    if (!top) {
      terrain::SurfaceElevation highest = start + 8.0f * u::m;
      for (meters_t r = 150.0f * u::m; r <= 700.0f * u::m; r += 25.0f * u::m)
        for (int k = 0; k < 32; ++k) {
          const float a = 6.2831853f * k / 32.0f;
          const plan_position_t on = plan (moved (
            lot.spawn,
            along (unit_direction (Vec3 (std::cos (a), 0.0f, std::sin (a))),
                   r)));
          const terrain::SurfaceElevation h = lot.ground.elevation (on);
          if (h - 0.03f * r <= highest ||
              !lot.ground.open (on, 1.0f * u::m, 0.5f * u::m))
            continue;
          highest = h - 0.03f * r;
          top = { terrain::at_elevation (on, h),
                  heading_along (plan (lot.spawn) - on) };
          where = "at a lookout above the trailhead";
        }
    }
    mov::Vehicle& mtb = lot.session.mountain_bike ();
    mtb.set_trunks (&lot.trunks);
    const meters_t ride = mov::mountain_bike_rig.ride_height ();
    if (top) {
      const position_t at = open_ground_near (lot.ground,
                                              moved (top->first, m_aside),
                                              1.0f * u::m,
                                              0.5f * u::m,
                                              8.0f * u::m);
      report_parked (lot, "mountainbike", at, where);
      const position_t frame = on_wheels (lot.ground, plan (at), ride);
      mtb.set_heading (direction_value (top->second));
      mtb.reset (position_value (frame));
      return;
    }
    const position_t at =
      open_ground_near (lot.ground,
                        moved (moved (lot.session.bike ().position (),
                                      along (side, -5.5f * u::m)),
                               along (heading, -3.5f * u::m)),
                        1.0f * u::m,
                        0.4f * u::m,
                        12.0f * u::m);
    const position_t frame = on_wheels (lot.ground, plan (at), ride);
    mtb.set_heading (direction_value (toward (heading, side, 0.2f)));
    report_parked (
      lot, "mountainbike", frame, "beside the bike (the trail does not climb)");
    mtb.reset (position_value (frame));
  }

  void Parking::park_long_john (const ParkingLot& lot) const {
    const auto [heading, side] = bike_frame (lot);
    mov::Vehicle& lj = lot.session.long_john ();
    lj.set_trunks (&lot.trunks);
    std::optional<position_t> hamlet;
    const char* where = "in the nearest hamlet";
    // Best of all outside the door of a farmhouse still lived in.
    for (const Farm& farm : lot.places.farms) {
      if (!farm.kept || farm.ruined)
        continue;
      for (const Building& building : farm.buildings) {
        if (building.kind != Building::Kind::house)
          continue;
        const position_t door =
          near_spawn (lot.ground,
                      lot.spawn,
                      moppe::position (building.at + building.forward * 6.0f));
        const meters_t r = from_spawn (lot.spawn, door);
        if (r > 3000.0f * u::m ||
            (hamlet && r >= from_spawn (lot.spawn, *hamlet)))
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
      const position_t at =
        near_spawn (lot.ground,
                    lot.spawn,
                    moppe::position (Vec3 (place.x_m, 0.0f, place.z_m)));
      const meters_t r = from_spawn (lot.spawn, at);
      if (r > 3000.0f * u::m ||
          (hamlet && r >= from_spawn (lot.spawn, *hamlet)))
        continue;
      hamlet = at;
    }
    const meters_t ride = mov::long_john_rig.ride_height ();
    if (hamlet) {
      const position_t at = open_ground_near (lot.ground,
                                              moved (*hamlet, m_aside),
                                              1.8f * u::m,
                                              0.4f * u::m,
                                              60.0f * u::m);
      if (lot.ground.open (plan (at), 1.8f * u::m, 0.4f * u::m)) {
        report_parked (lot, "Long John", at, where);
        const plan_displacement_t home = plan (lot.spawn) - plan (at);
        lj.set_heading (direction_value (length2 (plan_value (home)) > 1e-4f
                                           ? heading_along (home)
                                           : heading));
        lj.reset (position_value (on_wheels (lot.ground, plan (at), ride)));
        return;
      }
    }
    const position_t at =
      open_ground_near (lot.ground,
                        moved (moved (lot.session.bike ().position (),
                                      along (heading, -24.0f * u::m)),
                               along (side, 4.0f * u::m)),
                        1.8f * u::m,
                        0.3f * u::m,
                        20.0f * u::m);
    const position_t frame = on_wheels (lot.ground, plan (at), ride);
    lj.set_heading (direction_value (toward (heading, side, -0.15f)));
    report_parked (lot, "Long John", frame, "down the trail (no hamlet near)");
    lj.reset (position_value (frame));
  }

  void Parking::park_helicopter (const ParkingLot& lot,
                                 std::optional<position_t> avoid) const {
    const position_t centre = lot.spawn;
    const terrain::SurfaceElevation water =
      terrain::surface_elevation_point (lot.ground.world.water_level);
    const meters_t reach = (mov::Helicopter::rotor_radius + 2.5f) * u::m;
    std::optional<position_t> best;
    meters_t best_score = 1e30f * u::m;
    for (meters_t r = 220.0f * u::m; r <= 600.0f * u::m; r += 6.0f * u::m) {
      const float metres = meters_value (r);
      const int around = static_cast<int> (6.2831853f * metres / 6.0f);
      for (int k = 0; k < around; ++k) {
        const float a = 6.2831853f * (k + 0.5f * (int (metres) % 2)) / around;
        const plan_position_t on = plan (moved (
          centre,
          along (unit_direction (Vec3 (std::cos (a), 0.0f, std::sin (a))), r)));
        const terrain::SurfaceElevation h = lot.ground.elevation (on);
        if (h < water + 1.0f * u::m)
          continue;
        if (avoid) {
          const plan_displacement_t apart = on - plan (*avoid);
          if (dot (apart, apart) < (15.0f * u::m) * (15.0f * u::m))
            continue;
        }
        // Level under the skids and the tail, and not much less so
        // under the rotor's reach.
        terrain::SurfaceElevation low = h, high = h;
        terrain::ElevationChange wide = 0.0f * u::m;
        for (int i = 0; i < 12; ++i) {
          const float b = 6.2831853f * i / 12.0f;
          const plan_direction_t out =
            plan_unit_direction (Vec2 (std::cos (b), std::sin (b)));
          const terrain::SurfaceElevation inner =
            lot.ground.elevation (on + along (out, 3.0f * u::m));
          const terrain::SurfaceElevation outer =
            lot.ground.elevation (on + along (out, 6.5f * u::m));
          low = std::min (low, inner);
          high = std::max (high, inner);
          wide = std::max (wide, abs (outer - h));
        }
        if (high - low > 0.6f * u::m || wide > 1.6f * u::m)
          continue;
        // How far it stands over the ground some way round.
        terrain::ElevationChange around_it = 0.0f * u::m;
        for (int i = 0; i < 8; ++i) {
          const float b = 6.2831853f * (i + 0.5f) / 8.0f;
          around_it += lot.ground
                         .elevation (on + along (plan_unit_direction (Vec2 (
                                                   std::cos (b), std::sin (b))),
                                                 30.0f * u::m))
                         .delta_from_zero ();
        }
        const terrain::ElevationChange rise =
          h.delta_from_zero () - around_it / 8.0f;
        const meters_t score ((r - 220.0f * u::m) * 0.004f +
                              (high - low) * 4.0f + wide -
                              std::clamp<terrain::ElevationChange> (
                                rise, -2.0f * u::m, 6.0f * u::m) *
                                0.25f);
        if (score >= best_score)
          continue;
        const position_t at = terrain::at_elevation (on, h);
        bool clear = true;
        for (const mov::Trunk& trunk : lot.trunks.gather (
               position_value (at), meters_value (reach) + 1.0f)) {
          const plan_displacement_t apart =
            plan_position (trunk.root[0], trunk.root[2]) - on;
          const meters_t room = reach + trunk.radius * u::m;
          if (dot (apart, apart) < room * room) {
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
      if (best && r > 380.0f * u::m)
        break;
    }
    direction_t heading =
      on_the_level (lot.ground.trail_heading (), Vec3 (0, 0, 1));
    position_t at = moved (centre, along (left_of (heading), -14.0f * u::m));
    if (best) {
      at = *best;
      // Side on to the spawn, so it shows its whole length from there.
      heading = left_of (heading_along (plan (at) - plan (centre)));
    }
    lot.session.helicopter ().set_trunks (&lot.trunks);
    lot.session.helicopter ().place (at, heading);
    std::cerr << "moppe: helicopter parked "
              << meters_value (from_spawn (centre, at)) << " m from the spawn"
              << (best ? "" : " (no clear site found)") << std::endl;
  }
}
