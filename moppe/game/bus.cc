#include <moppe/game/bus.hh>

#include <moppe/game/bus_model.hh>
#include <moppe/game/game_session.hh>
#include <moppe/game/model_mesh.hh>
#include <moppe/game/place_names.hh>
#include <moppe/game/places.hh>
#include <moppe/gfx/mat4.hh>
#include <moppe/terrain/torus.hh>

#include <algorithm>
#include <cmath>
#include <iostream>
#include <limits>

namespace moppe::game {
  namespace {
    namespace bus_parts {
      // The way to the left of a level direction, and the level way.
      Vec3 left_of (const Vec3& forward) {
        return Vec3 (forward[2], 0.0f, -forward[0]);
      }

      // How far a plan step reaches, as std::hypot measures it (which
      // rounds otherwise than cr::hypot).
      meters_t reach_of (const plan_displacement_t& step) {
        const Vec2& d = plan_value (step);
        return std::hypot (d.x (), d.z ()) * u::m;
      }

      // How long a run of `d` from standing to standing takes, and how far
      // along it a bus is, and how fast it goes, `t` in: gathering pace at
      // the bus's acceleration up to its cruise (or as far as half the run
      // allows), keeping it, and braking as hard.
      struct Run {
        static constexpr metres_per_second_squared_t a = BusLine::acceleration;

        meters_t d;
        speed_t top;
        seconds_t gather;

        explicit Run (meters_t metres) : d (metres) {
          top = std::min (BusLine::cruise, speed_t (sqrt (a * d)));
          gather =
            top > speed_t::zero () ? seconds_t (top / a) : seconds_t::zero ();
        }

        seconds_t time () const {
          return top > speed_t::zero ()
                   ? seconds_t (2.0f * gather + (d - top * gather) / top)
                   : seconds_t::zero ();
        }

        meters_t distance (seconds_t t) const {
          const seconds_t total = time ();
          if (t <= seconds_t::zero ())
            return meters_t::zero ();
          if (t >= total)
            return d;
          if (t < gather)
            return meters_t (0.5f * a * t * t);
          if (t > total - gather)
            return meters_t (d - 0.5f * a * (total - t) * (total - t));
          return meters_t (0.5f * top * gather + top * (t - gather));
        }

        speed_t speed (seconds_t t) const {
          const seconds_t total = time ();
          if (t <= seconds_t::zero () || t >= total)
            return speed_t::zero ();
          return std::min ({ top, speed_t (a * t), speed_t (a * (total - t)) });
        }
      };

      // A stop wanted along the loop, the more wanted the lower its rank:
      // the landing's, a branch's, a farm's, and the ones between.
      struct Want {
        meters_t along {};
        int rank = 0;
        std::string name;
        bool farm = false;
      };

      meters_t wrap_along (meters_t along, meters_t length) {
        return terrain::wrap_coordinate (along, length);
      }

      // How far apart two points along the loop are, either way round.
      meters_t apart_along (meters_t a, meters_t b, meters_t length) {
        return abs (terrain::minimum_image_delta (a - b, length));
      }
    }
  }

  BusLine::BusLine (const terrain::Road& loop, std::vector<BusStop> stops)
      : m_stops (std::move (stops)) {
    using namespace bus_parts;
    const std::size_t n = loop.points.size ();
    if (n < 2 || loop.heights.size () != n)
      return;
    m_along.reserve (n);
    meters_t along = meters_t::zero ();
    for (std::size_t i = 0; i < n; ++i) {
      if (i > 0)
        along += reach_of (loop.points[i] - loop.points[i - 1]);
      m_along.push_back (along);
    }
    m_points = loop.points;
    m_height = loop.heights;
    m_lap = m_points.back () - m_points.front ();
    if (!(along > meters_t::zero ())) {
      m_along.clear ();
      return;
    }
    std::ranges::sort (m_stops, {}, &BusStop::along);
    // The timetable: the first stop reached at the round's start.
    const meters_t length = along;
    seconds_t t = seconds_t::zero ();
    for (std::size_t i = 0; i < m_stops.size (); ++i) {
      m_arrive.push_back (t);
      t += i == 0 ? layover : dwell;
      m_leave.push_back (t);
      const meters_t here = m_stops[i].along;
      const meters_t next = i + 1 < m_stops.size ()
                              ? m_stops[i + 1].along
                              : m_stops.front ().along + length;
      t += Run (next - here).time ();
    }
    // With no stop at all, the buses go round without stopping.
    m_round = m_stops.empty () ? seconds_t (length / cruise) : t;
    m_buses = std::max<std::size_t> (
      1,
      static_cast<std::size_t> (
        std::lround (seconds_value (m_round) / seconds_value (headway))));
  }

  position_t BusLine::point (meters_t along) const {
    if (empty ())
      return position (Vec3 ());
    const meters_t length = m_along.back ();
    const float laps =
      std::floor (meters_value (along) / meters_value (length));
    const meters_t s = along - laps * length;
    const auto upper = std::ranges::upper_bound (m_along, s);
    const std::size_t i = std::clamp<std::size_t> (
      static_cast<std::size_t> (upper - m_along.begin ()),
      1,
      m_along.size () - 1);
    const meters_t span = m_along[i] - m_along[i - 1];
    const float t = span > meters_t::zero ()
                      ? meters_value (s - m_along[i - 1]) / meters_value (span)
                      : 0.0f;
    const Vec2& a = plan_value (m_points[i - 1]);
    const Vec2& b = plan_value (m_points[i]);
    const Vec2& lap = plan_value (m_lap);
    return position (
      Vec3 (std::lerp (a.x (), b.x (), t) + laps * lap.x (),
            std::lerp (terrain::surface_elevation_value (m_height[i - 1]),
                       terrain::surface_elevation_value (m_height[i]),
                       t),
            std::lerp (a.z (), b.z (), t) + laps * lap.z ()));
  }

  direction_t BusLine::direction (meters_t along) const {
    // Over a few metres either way, so the lattice's bends are smoothed.
    Vec3 d = position_value (point (along + 3.0f * u::m)) -
             position_value (point (along - 3.0f * u::m));
    d[1] = 0.0f;
    return unit_direction (length2 (d) > 1e-8f ? normalized (d)
                                               : Vec3 (0, 0, 1));
  }

  seconds_t BusLine::phase (std::size_t k, play_time_t now) const {
    const double round = seconds_value (m_round);
    const double offset =
      round * static_cast<double> (k) / static_cast<double> (m_buses);
    const double into = seconds_into_play (now) + offset;
    return static_cast<float> (into - std::floor (into / round) * round) * u::s;
  }

  std::vector<BusPose> BusLine::where (play_time_t now) const {
    using namespace bus_parts;
    std::vector<BusPose> poses;
    if (empty () || !(m_round > seconds_t::zero ()))
      return poses;
    const meters_t length = m_along.back ();
    for (std::size_t k = 0; k < m_buses; ++k) {
      const seconds_t t = phase (k, now);
      BusPose pose;
      meters_t along = meters_t::zero ();
      speed_t speed = speed_t::zero ();
      if (m_stops.empty ()) {
        along = t * cruise;
        speed = cruise;
      } else {
        // The last stop it reached, and how long since it left.
        const auto reached = std::ranges::upper_bound (m_arrive, t);
        const std::size_t i =
          static_cast<std::size_t> (reached - m_arrive.begin ()) - 1;
        const meters_t here = m_stops[i].along;
        pose.next_stop = i;
        if (t < m_leave[i]) {
          along = here;
          pose.at_stop = i;
          // Opening over a second and a half, and shutting as long.
          const seconds_t open = std::min (t - m_arrive[i], m_leave[i] - t);
          pose.door_open =
            std::clamp (seconds_value (open) / 1.5f, 0.0f, 1.0f) *
            proportion[one];
        } else {
          pose.next_stop = (i + 1) % m_stops.size ();
          const meters_t next = i + 1 < m_stops.size ()
                                  ? m_stops[i + 1].along
                                  : m_stops.front ().along + length;
          const Run run (next - here);
          along = here + run.distance (t - m_leave[i]);
          speed = run.speed (t - m_leave[i]);
        }
      }
      pose.along = along;
      pose.speed = speed;
      const Vec3 level = direction_value (direction (pose.along));
      const Vec3 left = left_of (level);
      // Nose and tail over the line's heights, for its pitch.
      const float rise = position_value (point (pose.along + 3.0f * u::m))[1] -
                         position_value (point (pose.along - 3.0f * u::m))[1];
      pose.forward =
        unit_direction (normalized (level * 6.0f + Vec3 (0.0f, rise, 0.0f)));
      pose.up = unit_direction (
        normalized (cross (direction_value (pose.forward), left)));
      pose.position =
        point (pose.along) - quantity_cast<isq::position_vector> (
                               moppe::along (unit_direction (left), lane));
      // The front wheels follow the bend, over the wheelbase; they roll
      // the way it goes.
      {
        constexpr meters_t wheelbase = 6.45f * u::m;
        constexpr meters_t wheel_radius = 0.49f * u::m;
        const Vec3 behind =
          direction_value (direction (pose.along - 4.0f * u::m));
        const Vec3 ahead =
          direction_value (direction (pose.along + 4.0f * u::m));
        const float to_left = std::atan2 (
          behind[2] * ahead[0] - behind[0] * ahead[2], dot (behind, ahead));
        pose.steer =
          -std::atan (meters_value (wheelbase) * to_left / 8.0f) * u::rad;
        pose.wheel_spin =
          std::fmod (meters_value (along) / meters_value (wheel_radius),
                     6.2831853f) *
          u::rad;
      }
      poses.push_back (pose);
    }
    return poses;
  }

  BusDue BusLine::due (std::size_t stop, play_time_t now) const {
    BusDue best;
    if (empty () || stop >= m_stops.size () || !(m_round > seconds_t::zero ()))
      return best;
    seconds_t soonest = std::numeric_limits<float>::infinity () * u::s;
    for (std::size_t k = 0; k < m_buses; ++k) {
      const seconds_t t = phase (k, now);
      if (t >= m_arrive[stop] && t < m_leave[stop]) {
        best.here = true;
        best.wait = m_leave[stop] - t;
        return best;
      }
      seconds_t until = m_arrive[stop] - t;
      if (until < seconds_t::zero ())
        until += m_round;
      soonest = std::min (soonest, until);
    }
    best.wait = soonest;
    return best;
  }

  BusLine plan_bus_line (const terrain::RoadNetwork& network,
                         std::span<const std::string> names,
                         const PlacesPlan& places,
                         const PlaceGround& ground,
                         std::uint32_t seed) {
    using namespace bus_parts;
    const auto found = std::ranges::find_if (
      network.roads, [] (const terrain::Road& road) { return road.loop; });
    if (found == network.roads.end ())
      return {};
    const terrain::Road& loop = *found;
    const BusLine line (loop, {});
    if (line.empty ())
      return {};
    const meters_t length = line.length ();
    const plan_extent_t period = ground.period;
    // The ground's and the water's heights, as the places read them.
    const auto ground_at = [&] (const plan_position_t& at) {
      return ground.ground (at);
    };
    const auto water_at = [&] (const plan_position_t& at) {
      return ground.water (at);
    };

    // How far along the loop its nearest point to `at` lies, and how far
    // from it `at` is, across the seam.
    const auto nearest_along = [&] (const plan_position_t& at) {
      meters_t best_along = meters_t::zero ();
      meters_t best = std::numeric_limits<float>::infinity () * u::m;
      meters_t along = meters_t::zero ();
      for (std::size_t i = 1; i < loop.points.size (); ++i) {
        const plan_position_t& a = loop.points[i - 1];
        const plan_position_t& b = loop.points[i];
        const Vec2 to = plan_value (terrain::torus_offset (a, at, period));
        const Vec2 step = plan_value (b - a);
        const float span2 = step.x () * step.x () + step.z () * step.z ();
        const float t =
          span2 > 0.0f
            ? std::clamp (
                (to.x () * step.x () + to.z () * step.z ()) / span2, 0.0f, 1.0f)
            : 0.0f;
        const meters_t off =
          std::hypot (to.x () - step.x () * t, to.z () - step.z () * t) * u::m;
        const meters_t span = std::sqrt (span2) * u::m;
        if (off < best) {
          best = off;
          best_along = along + t * span;
        }
        along += span;
      }
      return std::pair (best_along, best);
    };
    const auto distance_to = [&] (const terrain::RoadPlace& place,
                                  const plan_position_t& at) {
      return reach_of (terrain::torus_offset (at, place.at, period));
    };

    std::vector<Want> wants;
    std::vector<std::string> taken;
    const auto claim = [&] (const std::string& name) {
      if (name.empty () || std::ranges::find (taken, name) != taken.end ())
        return false;
      taken.push_back (name);
      return true;
    };
    // The landing's, at the loop's start.
    for (std::size_t p = 0; p < network.places.size (); ++p)
      if (network.places[p].kind == terrain::RoadPlace::Kind::Landing &&
          p < names.size ()) {
        wants.push_back ({ meters_t::zero (), 0, names[p], false });
        break;
      }
    if (wants.empty ())
      wants.push_back ({ meters_t::zero (), 0, "", false });
    // Just past each cart road that leaves it, for the place that road
    // leads toward: the nearest of the places off the loop to where it
    // branches.
    for (const terrain::Road& road : network.roads) {
      if (road.loop || road.kind != terrain::Road::Kind::Road ||
          road.points.size () < 2)
        continue;
      for (const bool back : { false, true }) {
        const plan_position_t& end =
          back ? road.points.back () : road.points.front ();
        const plan_position_t& far =
          back ? road.points.front () : road.points.back ();
        const auto [along, off] = nearest_along (end);
        if (off > 8.0f * u::m)
          continue;
        std::size_t nearest = network.places.size ();
        meters_t nearest_m = std::numeric_limits<float>::infinity () * u::m;
        for (std::size_t p = 0; p < network.places.size (); ++p) {
          const terrain::RoadPlace& place = network.places[p];
          if (place.kind == terrain::RoadPlace::Kind::Landing)
            continue;
          // Toward it along the branch: nearer its far end than the loop.
          const meters_t m = distance_to (place, end);
          if (distance_to (place, far) < m && m < nearest_m) {
            nearest_m = m;
            nearest = p;
          }
        }
        std::string name;
        if (nearest < names.size ())
          name = nearest_m < 600.0f * u::m ? names[nearest]
                                           : names[nearest] + " vägskäl";
        wants.push_back ({ along + 30.0f * u::m, 1, name, false });
      }
    }
    // By each farm near it.
    for (const Farm& farm : places.farms) {
      if (farm.buildings.empty () || farm.ruined)
        continue;
      const auto [along, off] =
        nearest_along (plan (farm.buildings.front ().at));
      if (off < 160.0f * u::m)
        wants.push_back ({ along, 2, "", true });
    }
    // The most wanted first; none within a few hundred metres of
    // another.
    std::ranges::stable_sort (wants, {}, &Want::rank);
    std::vector<Want> kept;
    for (Want& want : wants) {
      want.along = wrap_along (want.along, length);
      if (std::ranges::any_of (kept, [&] (const Want& other) {
            return apart_along (other.along, want.along, length) <
                   350.0f * u::m;
          }))
        continue;
      kept.push_back (std::move (want));
    }
    // And between them wherever they lie far apart.
    std::ranges::sort (kept, {}, &Want::along);
    {
      constexpr meters_t farthest = 1400.0f * u::m, between = 1100.0f * u::m;
      std::vector<Want> filled;
      for (std::size_t i = 0; i < kept.size (); ++i) {
        filled.push_back (kept[i]);
        const meters_t from = kept[i].along;
        const meters_t to = i + 1 < kept.size () ? kept[i + 1].along
                                                 : kept.front ().along + length;
        const meters_t gap = to - from;
        if (gap <= farthest)
          continue;
        const int more = static_cast<int> (std::ceil (meters_value (gap) /
                                                      meters_value (between))) -
                         1;
        for (int k = 1; k <= more; ++k)
          filled.push_back (
            { wrap_along (from + gap * static_cast<float> (k) /
                                   static_cast<float> (more + 1),
                          length),
              3,
              "",
              false });
      }
      kept = std::move (filled);
    }

    // Each where the verge is level and dry beside it, off any bridge and
    // clear of other roads, the nearest such within a few dozen metres;
    // the landing's anyway.
    std::vector<std::pair<meters_t, meters_t>> decks;
    {
      std::vector<meters_t> along (loop.points.size (), meters_t::zero ());
      for (std::size_t i = 1; i < loop.points.size (); ++i)
        along[i] =
          along[i - 1] + reach_of (loop.points[i] - loop.points[i - 1]);
      for (const terrain::RoadBridge& bridge : loop.bridges)
        decks.push_back ({ along[bridge.first] - 15.0f * u::m,
                           along[bridge.last] + 15.0f * u::m });
    }
    const meters_t tread = ground.roads
                             ? ground.roads->formation ().half_width +
                                 ground.roads->formation ().shoulder
                             : 3.25f * u::m;
    const meters_t sign_off = tread + 0.9f * u::m;
    const meters_t shelter_off = tread + 3.0f * u::m;
    // How poorly a stop would stand at `along` (how far its verge lies
    // above or below the tread), or below zero if it cannot.
    const auto badness = [&] (meters_t along,
                              bool& sheltered) -> terrain::ElevationChange {
      const terrain::ElevationChange cannot = -1.0f * u::m;
      for (const auto& [from, to] : decks)
        if (along > from && along < to)
          return cannot;
      const position_t centre = line.point (along);
      const terrain::SurfaceElevation tread_height =
        terrain::surface_elevation_point (position_value (centre)[1] * u::m);
      const direction_t right = unit_direction (
        left_of (direction_value (line.direction (along))) * -1.0f);
      terrain::ElevationChange worst = terrain::ElevationChange::zero ();
      for (const meters_t off : { sign_off,
                                  shelter_off - 0.8f * u::m,
                                  shelter_off,
                                  shelter_off + 0.8f * u::m }) {
        const plan_position_t p =
          plan (centre + quantity_cast<isq::position_vector> (
                           moppe::along (right, off)));
        const terrain::SurfaceElevation height = ground_at (p);
        if (water_at (p) > height - 0.3f * u::m)
          return cannot;
        worst = std::max (worst, abs (height - tread_height));
        if (ground.roads && off > sign_off) {
          const terrain::RoadSample sample = ground.roads->nearest (p);
          if (sample.found && abs (sample.across) < tread + 0.5f * u::m)
            return cannot;
        }
      }
      sheltered = worst < 0.7f * u::m;
      return worst < 1.4f * u::m ? worst : cannot;
    };
    std::vector<BusStop> stops;
    std::size_t rolled = 0;
    for (Want& want : kept) {
      meters_t best_along = want.along;
      // A metre off level is as bad as no shelter, or a hundred metres
      // shifted.
      meters_t best = std::numeric_limits<float>::infinity () * u::m;
      bool best_sheltered = false;
      for (meters_t shift = meters_t::zero (); shift <= 64.0f * u::m;
           shift += 4.0f * u::m)
        for (const float sign : { 1.0f, -1.0f }) {
          if (shift == meters_t::zero () && sign < 0.0f)
            continue;
          const meters_t along = wrap_along (want.along + sign * shift, length);
          bool sheltered = false;
          const terrain::ElevationChange bad = badness (along, sheltered);
          if (bad < terrain::ElevationChange::zero ())
            continue;
          const meters_t score = quantity_cast<isq::length> (bad) +
                                 (sheltered ? 0.0f : 1.0f) * u::m +
                                 0.01f * shift;
          if (score < best) {
            best = score;
            best_along = along;
            best_sheltered = sheltered;
          }
        }
      if (!(best < std::numeric_limits<float>::infinity () * u::m) &&
          want.rank > 0)
        continue;
      if (!claim (want.name)) {
        do
          want.name = name_bus_stop (seed, rolled++, want.farm, taken);
        while (!claim (want.name));
      }
      BusStop stop;
      stop.name = want.name;
      stop.along = best_along;
      stop.forward = line.direction (stop.along);
      const direction_t right =
        unit_direction (left_of (direction_value (stop.forward)) * -1.0f);
      const position_t centre = line.point (stop.along);
      const auto on_ground = [&] (meters_t off) {
        const plan_position_t p =
          plan (centre + quantity_cast<isq::position_vector> (
                           moppe::along (right, off)));
        return terrain::at_elevation (terrain::wrap (p, period), ground_at (p));
      };
      stop.sign = on_ground (sign_off);
      if (best_sheltered)
        stop.shelter = on_ground (shelter_off);
      stops.push_back (std::move (stop));
    }
    BusLine planned (loop, std::move (stops));
    std::cerr << "moppe: bus: a loop of " << std::lround (meters_value (length))
              << " m, " << planned.stops ().size () << " stops, "
              << planned.buses () << " buses, "
              << std::lround (seconds_value (planned.round_trip ()))
              << " s round:";
    for (const BusStop& stop : planned.stops ())
      std::cerr << ' ' << stop.name << " ("
                << std::lround (meters_value (stop.along)) << ")";
    std::cerr << std::endl;
    return planned;
  }

  MachineBody
  bus_body (const BusPose& pose, const position_t& near, plan_extent_t period) {
    static const Bumper bumpers[] = {
      { Vec3 (0, 1.35f, 4.3f), 1.3f * u::m },
      { Vec3 (0, 1.35f, 2.15f), 1.3f * u::m },
      { Vec3 (0, 1.35f, 0.0f), 1.3f * u::m },
      { Vec3 (0, 1.35f, -2.15f), 1.3f * u::m },
      { Vec3 (0, 1.35f, -4.3f), 1.3f * u::m },
    };
    MachineBody body;
    body.position =
      position_value (terrain::nearest_image (pose.position, near, period));
    body.forward = direction_value (pose.forward);
    body.up = direction_value (pose.up);
    body.velocity = direction_value (pose.forward) * speed_value (pose.speed);
    body.mass = 12000.0f * u::kg;
    body.ours = false;
    body.bumpers = bumpers;
    return body;
  }

  Mat4 bus_frame (const BusPose& pose) {
    return model_mesh::body_frame (pose.position, pose.forward, pose.up);
  }

  position_t bus_door (const BusPose& pose) {
    return position (bus_frame (pose).transform_point (bus_model::door));
  }

  displacement_t bus_seat_in_model (std::size_t seat) {
    return displacement (seat == 0 ? bus_model::seat_01 : bus_model::seat_02);
  }

  position_t bus_seat (const BusPose& pose, std::size_t seat) {
    return position (bus_frame (pose).transform_point (
      displacement_value (bus_seat_in_model (seat))));
  }

  play_time_t bus_time (const GameLogicState& logic) {
    return logic.m_total_time + logic.m_bus_lead;
  }

  std::optional<std::size_t> bus_at_hand (const BusLine& line,
                                          const position_t& feet,
                                          play_time_t now,
                                          plan_extent_t period) {
    constexpr meters_t reach = 4.5f * u::m;
    const std::vector<BusPose> buses = line.where (now);
    for (std::size_t k = 0; k < buses.size (); ++k) {
      if (!buses[k].at_stop)
        continue;
      const position_t door = bus_door (buses[k]);
      if (bus_parts::reach_of (
            terrain::torus_offset (plan (feet), plan (door), period)) < reach)
        return k;
    }
    return std::nullopt;
  }

  namespace {
    namespace bus_parts {
      // The ridden bus as it is now, if it runs.
      std::optional<BusPose> ridden (const GameSession& session,
                                     const BusLine& line) {
        const std::vector<BusPose> buses =
          line.where (bus_time (session.logic ()));
        if (session.logic ().m_bus >= buses.size ())
          return std::nullopt;
        return buses[session.logic ().m_bus];
      }

      void take_seat (GameLogicState& logic, const BusPose& pose) {
        logic.m_seat_position = bus_seat (pose, logic.m_bus_seat);
        logic.m_seat_heading = pose.forward;
        logic.m_seat_up = pose.up;
        logic.m_seat_velocity =
          velocity (direction_value (pose.forward) * speed_value (pose.speed));
      }

      // Down by the door, on the verge side, facing out.
      void step_down (GameSession& session, const BusPose& pose) {
        const Vec3 left = normalized (
          cross (direction_value (pose.up), direction_value (pose.forward)));
        Vec3 out = left * -1.0f;
        out[1] = 0.0f;
        session.walker ().spawn (bus_door (pose) +
                                   quantity_cast<isq::position_vector> (
                                     displacement (Vec3 (0, 0.2f, 0))),
                                 normalized (out));
        session.set_mode (Doing::on_foot ());
        session.logic ().m_bus_alight = false;
        session.logic ().m_fp_eye =
          moppe::position (session.walker ().eye_position ());
      }
    }
  }

  void board_bus (GameSession& session,
                  const BusLine& line,
                  std::size_t bus,
                  std::size_t seat) {
    GameLogicState& logic = session.logic ();
    logic.m_bus = static_cast<std::uint8_t> (bus);
    logic.m_bus_seat = static_cast<std::uint8_t> (seat);
    logic.m_bus_alight = false;
    const std::optional<BusPose> pose = bus_parts::ridden (session, line);
    if (!pose)
      return;
    session.clear_controls ();
    session.set_mode (Doing::on_the_bus ());
    bus_parts::take_seat (logic, *pose);
  }

  void leave_bus (GameSession& session, const BusLine& line) {
    const std::optional<BusPose> pose = bus_parts::ridden (session, line);
    if (!pose || pose->at_stop)
      bus_parts::step_down (session,
                            pose.value_or (BusPose {
                              .position = session.logic ().m_seat_position,
                              .forward = session.logic ().m_seat_heading,
                            }));
    else
      session.logic ().m_bus_alight = !session.logic ().m_bus_alight;
  }

  void ride_bus (GameSession& session, const BusLine& line) {
    GameLogicState& logic = session.logic ();
    if (logic.m_doing != Doing::on_the_bus ())
      return;
    const std::optional<BusPose> pose = bus_parts::ridden (session, line);
    if (!pose) {
      leave_bus (session, line);
      return;
    }
    bus_parts::take_seat (logic, *pose);
    if (logic.m_bus_alight && pose->at_stop)
      bus_parts::step_down (session, *pose);
  }

  std::optional<std::size_t> bus_stop_near (const BusLine& line,
                                            const position_t& at,
                                            meters_t reach,
                                            plan_extent_t period) {
    std::optional<std::size_t> nearest;
    meters_t best = reach;
    for (std::size_t i = 0; i < line.stops ().size (); ++i) {
      const meters_t d = bus_parts::reach_of (terrain::torus_offset (
        plan (at), plan (line.stops ()[i].sign), period));
      if (d < best) {
        best = d;
        nearest = i;
      }
    }
    return nearest;
  }
}
