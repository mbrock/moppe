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
      const position_t moored = landing.berth.point_for (
        terrain::world_origin, lot.spawn, lot.ground.period ());
      const Vec3& berth = position_value (moored);
      boat.place (moored,
                  outward ? -landing.berth_forward : landing.berth_forward,
                  berth[1] * u::m);
      m_mooring = terrain::nearest_image (
        landing.jetty.root, moored, lot.ground.period ());
      m_mooring_out = landing.jetty.out;
      const Vec3& spawn = position_value (lot.spawn);
      std::cerr << "moppe: boat moored at the landing's jetty, "
                << std::hypot (berth[0] - spawn[0], berth[2] - spawn[2])
                << " m from the spawn, at " << berth[0] << ' ' << berth[2]
                << std::endl;
      return;
    }
    if (!lakes_first && moor_boat (lot, true, outward))
      return;
    if (!moor_boat (lot, false, outward))
      std::cerr << "moppe: boat: no open water near the spawn\n";
  }

  void Parking::park_snowmobile (const ParkingLot& lot) const {
    // Park in the heart of the largest contiguous high snowfield (wide 2D
    // snowpack, gentle cross-slope, obstacle-free) facing along its longest
    // open powder run rather than on a narrow ridge tip or bare scree.
    const auto& domain = lot.ground.surface ().domain ();
    constexpr int samples = 128;
    constexpr proportion_t min_spawn_snow = 0.65f * proportion[one];
    constexpr proportion_t min_run_snow = 0.50f * proportion[one];
    const auto heading_score = [&] (plan_position_t p,
                                    meters_t h,
                                    plan_direction_t way) -> scale_factor_t {
      meters_t last = h;
      scale_factor_t run = 0.0f * scale_factor[one];
      for (int j = 1; j <= 10; ++j) {
        const plan_position_t q = p + along (way, j * 24.0f * u::m);
        if (lot.ground.water_depth (q) > terrain::ElevationChange {})
          break;
        const proportion_t snow = lot.ground.snow_cover (q);
        if (snow < min_run_snow)
          break;
        const meters_t next (lot.ground.elevation (q).delta_from_zero ());
        const meters_t step_drop = abs (next - last);
        if (step_drop > 7.5f * u::m)
          break;
        if (j <= 4 && !lot.ground.open (q, 2.6f * u::m, 1.5f * u::m))
          break;
        // Favor wide snowy runs with smooth grades (slightly preferring
        // gentle downhill launches across the plateau or into a snow valley).
        const proportion_t smooth =
          1.0f * proportion[one] -
          std::clamp (proportion_t (step_drop / (7.5f * u::m)),
                      0.0f * proportion[one],
                      1.0f * proportion[one]);
        const scale_factor_t downhill =
          (next <= last ? 0.12f : 0.0f) * scale_factor[one];
        run += scale_factor_t (
          snow * (0.65f * scale_factor[one] +
                  0.35f * smooth.numerical_value_in (one) * scale_factor[one] +
                  downhill));
        last = next;
      }
      return run;
    };

    bool found_snow = false;
    scale_factor_t best_score = -1e9f * scale_factor[one];
    proportion_t best_snow {};
    auto pick = plan (lot.spawn);
    auto heading = unit_direction (Vec3 (0, 0, 1));
    for (int z = 0; z < samples; ++z)
      for (int x = 0; x < samples; ++x) {
        const auto p = plan_position (domain.period_x () * x / samples,
                                      domain.period_z () * z / samples);
        const proportion_t snow_here = lot.ground.snow_cover (p);
        if (snow_here < min_spawn_snow)
          continue;
        if (!lot.ground.open (p, 4.0f * u::m, 1.3f * u::m))
          continue;
        const auto h = meters_t (lot.ground.elevation (p).delta_from_zero ());
        // Measure 2D contiguous riding area across three rings (45/100/180 m)
        // so broad kalfjäll plateaus win over isolated summit spurs.
        scale_factor_t area_score =
          snow_here.numerical_value_in (one) * scale_factor[one];
        for (const meters_t ring :
             { 45.0f * u::m, 100.0f * u::m, 180.0f * u::m }) {
          for (int k = 0; k < 8; ++k) {
            const radians_t a = k * (PI / 4.0f) * u::rad;
            const plan_position_t q =
              p +
              along (plan_unit_direction (Vec2 (std::cos (radians_value (a)),
                                                std::sin (radians_value (a)))),
                     ring);
            if (lot.ground.water_depth (q) > terrain::ElevationChange {})
              continue;
            const proportion_t sq = lot.ground.snow_cover (q);
            if (sq < min_run_snow)
              continue;
            const meters_t hq (lot.ground.elevation (q).delta_from_zero ());
            if (abs (hq - h) > 0.28f * ring)
              continue;
            area_score += sq.numerical_value_in (one) * scale_factor[one];
          }
        }
        // Quick upper-bound prune before casting 16 launch rays.
        if (area_score + 18.0f * scale_factor[one] <= best_score)
          continue;
        scale_factor_t best_ray = -1.0f * scale_factor[one];
        direction_t ray_heading = heading;
        for (int bearing = 0; bearing < 16; ++bearing) {
          const radians_t a = bearing * (PI / 8.0f) * u::rad;
          const auto way = plan_unit_direction (
            Vec2 (std::cos (radians_value (a)), std::sin (radians_value (a))));
          const scale_factor_t ray = heading_score (p, h, way);
          if (ray > best_ray) {
            best_ray = ray;
            ray_heading = heading_along (along (way, 1.0f * u::m));
          }
        }
        const scale_factor_t total = area_score + 1.5f * best_ray;
        if (total > best_score) {
          best_score = total;
          best_snow = snow_here;
          pick = p;
          heading = ray_heading;
          found_snow = true;
        }
      }

    // Fallback for low/synthetic worlds without a snow line: highest gentle
    // shelf with open room ahead.
    if (!found_snow) {
      meters_t best = -1e6f * u::m;
      for (int z = 0; z < 64; ++z)
        for (int x = 0; x < 64; ++x) {
          const auto p = plan_position (domain.period_x () * x / 64,
                                        domain.period_z () * z / 64);
          const auto h = meters_t (lot.ground.elevation (p).delta_from_zero ());
          if (h > best && lot.ground.open (p, 2.5f * u::m, 1.0f * u::m)) {
            best = h;
            pick = p;
            best_snow = lot.ground.snow_cover (p);
          }
        }
    }

    const auto at = moved (
      near_spawn (lot.ground,
                  lot.spawn,
                  terrain::at_elevation (pick, lot.ground.elevation (pick))),
      m_aside);
    lot.session.fleet ().snowmobile.place (at, heading);
    std::cerr << "moppe: snowmobile parked "
              << std::lround (meters_value (from_spawn (lot.spawn, at)))
              << " m from the spawn, on the "
              << (found_snow ? "snowfield" : "fell") << " ("
              << std::lround (best_snow.numerical_value_in (one) * 100.0f)
              << "% snow), at " << std::lround (position_value (at)[0]) << ", "
              << std::lround (position_value (at)[1]) << ", "
              << std::lround (position_value (at)[2]) << std::endl;
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
        length (terrain::torus_offset (plan (*m_mooring),
                                       plan (lot.places.landing->jetty.root),
                                       lot.ground.period ())) < 1.0f * u::m) {
      // Across the jetty from the boat, a little nearer its head.
      const Jetty& jetty = lot.places.landing->jetty;
      const position_t root =
        terrain::nearest_image (jetty.root, berth, lot.ground.period ());
      const direction_t out = jetty.out;
      const direction_t side = left_of (out);
      const meters_t across =
        dot (position_value (berth) - position_value (root),
             direction_value (side)) *
        u::m;
      const meters_t beyond =
        across + std::copysign (meters_value (jetty.half_width) + 0.8f,
                                meters_value (across)) *
                   u::m;
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
      lot.ground.generated.water_bodies (), lot.ground.generated.rivers ());
    std::vector<plan_position_t> line;
    std::vector<bool> waterfall_point;
    for (auto reach = course.rbegin (); reach != course.rend (); ++reach)
      for (const terrain::RiverAlignmentPoint& point :
           (*reach)->alignment.points) {
        plan_position_t at = point.place;
        if (!line.empty ()) {
          Vec2& p = plan_value (at);
          const Vec2& last = plan_value (line.back ());
          for (std::size_t axis : { 0u, 1u })
            p[axis] -=
              period[axis] * std::round ((p[axis] - last[axis]) / period[axis]);
        }
        const bool is_fall = point.waterfall >= 0.45f * proportion[one];
        if (line.empty () || length (at - line.back ()) > 4.0f * u::m) {
          line.push_back (at);
          waterfall_point.push_back (is_fall);
        } else if (is_fall) {
          waterfall_point.back () = true;
        }
      }
    if (line.size () < 3)
      return false;
    // Walk upstream from the sea mouth along the continuous navigable river
    // stretch, stopping safely downstream of any far-headwater waterfall so a
    // river boat run never launches straight over a cliff.
    std::size_t safe_head = 0;
    for (std::size_t i = line.size (); i-- > 0;)
      if (waterfall_point[i]) {
        meters_t buffer = 0.0f * u::m;
        std::size_t clear = i + 1;
        while (clear + 1 < line.size () && buffer < 120.0f * u::m) {
          buffer += length (line[clear + 1] - line[clear]);
          ++clear;
        }
        safe_head = std::min (clear, line.size () - 1);
        break;
      }
    const meters_t most = 1800.0f * u::m;
    std::size_t start = line.size () - 1;
    meters_t up = 0.0f * u::m;
    while (start > safe_head &&
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
    const auto sheets = lot.ground.generated.water_view ();
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
        sheets.sample<terrain::water_velocity> (where).numerical_value_in (
          u::m / u::s);
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
        ahead);
    } else if (script == "bump") {
      const direction_t across =
        on_the_level (direction_value (tractor.left ()), Vec3 (1, 0, 0));
      session.jeep ().place (
        moved (tractor.position (), along (across, 18.0f * u::m)), -across);
      const plan_position_t stand =
        plan (session.jeep ().point (displacement (Vec3 (1.75f, 0.0f, -0.1f))));
      session.walker ().spawn (
        terrain::at_elevation (stand, ground.elevation (stand) + 0.1f * u::m),
        -across);
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
    const direction_t heading = on_the_level (
      direction_value (lot.session.bike ().forward ()), Vec3 (0, 0, 1));
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
        return near_spawn (
          lot.ground, lot.spawn, at_height (road.points[p], 0.0f * u::m));
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
          (road.abandoned > age_t {} ? 600.0f : 0.0f) * u::m;
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
        lot.ground, lot.spawn, at_height (points[p], 0.0f * u::m));
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
        const position_t door = near_spawn (
          lot.ground,
          lot.spawn,
          moppe::position (position_value (building.at) +
                           direction_value (building.forward) * 6.0f));
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
        near_spawn (lot.ground, lot.spawn, at_height (place.at, 0.0f * u::m));
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
    const meters_t reach = mov::Helicopter::rotor_radius + 2.5f * u::m;
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
        for (const mov::Trunk& trunk :
             lot.trunks.gather (at, reach + 1.0f * u::m)) {
          const plan_displacement_t apart = plan (trunk.root) - on;
          const meters_t room = reach + trunk.radius;
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
    direction_t heading = on_the_level (
      direction_value (lot.ground.trail_heading ()), Vec3 (0, 0, 1));
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
