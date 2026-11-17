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

      // How long a run of `d` metres from standing to standing takes, and
      // how far along it a bus is, and how fast it goes, `t` seconds in:
      // gathering pace at the bus's acceleration up to its cruise (or
      // as far as half the run allows), keeping it, and braking as hard.
      struct Run {
        float d, top, gather;

        explicit Run (float metres) : d (metres) {
          const float a =
            BusLine::acceleration.numerical_value_in (u::m / (u::s * u::s));
          top = std::min (speed_value (BusLine::cruise), std::sqrt (a * d));
          gather = top > 0.0f ? top / a : 0.0f;
        }

        float a () const {
          return BusLine::acceleration.numerical_value_in (u::m /
                                                           (u::s * u::s));
        }

        float time () const {
          return top > 0.0f ? 2.0f * gather + (d - top * gather) / top : 0.0f;
        }

        float distance (float t) const {
          const float total = time ();
          if (t <= 0.0f)
            return 0.0f;
          if (t >= total)
            return d;
          if (t < gather)
            return 0.5f * a () * t * t;
          if (t > total - gather)
            return d - 0.5f * a () * (total - t) * (total - t);
          return 0.5f * top * gather + top * (t - gather);
        }

        float speed (float t) const {
          const float total = time ();
          if (t <= 0.0f || t >= total)
            return 0.0f;
          return std::min ({ top, a () * t, a () * (total - t) });
        }
      };

      // A stop wanted along the loop, the more wanted the lower its rank:
      // the landing's, a branch's, a farm's, and the ones between.
      struct Want {
        float along = 0.0f;
        int rank = 0;
        std::string name;
        bool farm = false;
      };

      float wrap_along (float along, float length) {
        return terrain::wrap_coordinate (along, length);
      }

      // How far apart two points along the loop are, either way round.
      float apart_along (float a, float b, float length) {
        return std::fabs (terrain::minimum_image_delta (a - b, length));
      }
    }
  }

  BusLine::BusLine (const terrain::Road& loop, std::vector<BusStop> stops)
      : m_stops (std::move (stops)) {
    const std::size_t n = loop.points.size ();
    if (n < 2 || loop.heights.size () != n)
      return;
    m_x.reserve (n);
    m_z.reserve (n);
    m_along.reserve (n);
    float along = 0.0f;
    for (std::size_t i = 0; i < n; ++i) {
      if (i > 0)
        along += std::hypot (loop.points[i].x_m - loop.points[i - 1].x_m,
                             loop.points[i].z_m - loop.points[i - 1].z_m);
      m_x.push_back (loop.points[i].x_m);
      m_z.push_back (loop.points[i].z_m);
      m_along.push_back (along);
    }
    m_height = loop.heights;
    m_lap_x = m_x.back () - m_x.front ();
    m_lap_z = m_z.back () - m_z.front ();
    if (!(along > 0.0f)) {
      m_along.clear ();
      return;
    }
    std::ranges::sort (m_stops, {}, [] (const BusStop& s) {
      return s.along.numerical_value_in (u::m);
    });
    // The timetable: the first stop reached at the round's start.
    const float length = along;
    float t = 0.0f;
    for (std::size_t i = 0; i < m_stops.size (); ++i) {
      m_arrive.push_back (t);
      t += seconds_value (i == 0 ? layover : dwell);
      m_leave.push_back (t);
      const float here = m_stops[i].along.numerical_value_in (u::m);
      const float next =
        i + 1 < m_stops.size ()
          ? m_stops[i + 1].along.numerical_value_in (u::m)
          : m_stops.front ().along.numerical_value_in (u::m) + length;
      t += bus_parts::Run (next - here).time ();
    }
    // With no stop at all, the buses go round without stopping.
    m_round = m_stops.empty () ? length / speed_value (cruise) : t;
    m_buses = std::max<std::size_t> (1,
                                     static_cast<std::size_t> (std::lround (
                                       m_round / seconds_value (headway))));
  }

  Vec3 BusLine::point (meters_t along_q) const {
    if (empty ())
      return Vec3 ();
    const float length = m_along.back ();
    const float along = along_q.numerical_value_in (u::m);
    const float laps = std::floor (along / length);
    const float s = along - laps * length;
    const auto upper = std::ranges::upper_bound (m_along, s);
    const std::size_t i = std::clamp<std::size_t> (
      static_cast<std::size_t> (upper - m_along.begin ()),
      1,
      m_along.size () - 1);
    const float span = m_along[i] - m_along[i - 1];
    const float t = span > 0.0f ? (s - m_along[i - 1]) / span : 0.0f;
    return Vec3 (std::lerp (m_x[i - 1], m_x[i], t) + laps * m_lap_x,
                 std::lerp (m_height[i - 1], m_height[i], t),
                 std::lerp (m_z[i - 1], m_z[i], t) + laps * m_lap_z);
  }

  Vec3 BusLine::direction (meters_t along) const {
    // Over a few metres either way, so the lattice's bends are smoothed.
    Vec3 d = point (along + 3.0f * u::m) - point (along - 3.0f * u::m);
    d[1] = 0.0f;
    return length2 (d) > 1e-8f ? normalized (d) : Vec3 (0, 0, 1);
  }

  float BusLine::phase (std::size_t k, play_time_t now) const {
    const double offset = static_cast<double> (m_round) *
                          static_cast<double> (k) /
                          static_cast<double> (m_buses);
    const double into = seconds_into_play (now) + offset;
    return static_cast<float> (
      into - std::floor (into / static_cast<double> (m_round)) * m_round);
  }

  std::vector<BusPose> BusLine::where (play_time_t now) const {
    using namespace bus_parts;
    std::vector<BusPose> poses;
    if (empty () || !(m_round > 0.0f))
      return poses;
    const float length = m_along.back ();
    for (std::size_t k = 0; k < m_buses; ++k) {
      const float t = phase (k, now);
      BusPose pose;
      float along = 0.0f;
      float speed = 0.0f;
      if (m_stops.empty ()) {
        along = t * speed_value (cruise);
        speed = speed_value (cruise);
      } else {
        // The last stop it reached, and how long since it left.
        const auto reached = std::ranges::upper_bound (m_arrive, t);
        const std::size_t i =
          static_cast<std::size_t> (reached - m_arrive.begin ()) - 1;
        const float here = m_stops[i].along.numerical_value_in (u::m);
        pose.next_stop = i;
        if (t < m_leave[i]) {
          along = here;
          pose.at_stop = i;
          // Opening over a second and a half, and shutting as long.
          pose.door_open = std::clamp (
            std::min (t - m_arrive[i], m_leave[i] - t) / 1.5f, 0.0f, 1.0f);
        } else {
          pose.next_stop = (i + 1) % m_stops.size ();
          const float next =
            i + 1 < m_stops.size ()
              ? m_stops[i + 1].along.numerical_value_in (u::m)
              : m_stops.front ().along.numerical_value_in (u::m) + length;
          const Run run (next - here);
          along = here + run.distance (t - m_leave[i]);
          speed = run.speed (t - m_leave[i]);
        }
      }
      pose.along = along * u::m;
      pose.speed = speed * u::m / u::s;
      const Vec3 level = direction (pose.along);
      const Vec3 left = left_of (level);
      // Nose and tail over the line's heights, for its pitch.
      const float rise = point (pose.along + 3.0f * u::m)[1] -
                         point (pose.along - 3.0f * u::m)[1];
      pose.forward = normalized (level * 6.0f + Vec3 (0.0f, rise, 0.0f));
      pose.up = normalized (cross (pose.forward, left));
      pose.position = point (pose.along) - left * meters_value (lane);
      // The front wheels follow the bend, over the wheelbase; they roll
      // the way it goes.
      {
        constexpr float wheelbase = 6.45f, wheel_radius = 0.49f;
        const Vec3 behind = direction (pose.along - 4.0f * u::m);
        const Vec3 ahead = direction (pose.along + 4.0f * u::m);
        const float to_left = std::atan2 (
          behind[2] * ahead[0] - behind[0] * ahead[2], dot (behind, ahead));
        pose.steer_radians = -std::atan (wheelbase * to_left / 8.0f);
        pose.wheel_spin_radians = std::fmod (along / wheel_radius, 6.2831853f);
      }
      poses.push_back (pose);
    }
    return poses;
  }

  BusDue BusLine::due (std::size_t stop, play_time_t now) const {
    BusDue best;
    if (empty () || stop >= m_stops.size () || !(m_round > 0.0f))
      return best;
    float soonest = std::numeric_limits<float>::infinity ();
    for (std::size_t k = 0; k < m_buses; ++k) {
      const float t = phase (k, now);
      if (t >= m_arrive[stop] && t < m_leave[stop]) {
        best.here = true;
        best.wait = static_cast<double> (m_leave[stop] - t) * si::second;
        return best;
      }
      float until = m_arrive[stop] - t;
      if (until < 0.0f)
        until += m_round;
      soonest = std::min (soonest, until);
    }
    best.wait = static_cast<double> (soonest) * si::second;
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
    const float length = line.length ().numerical_value_in (u::m);
    const float period_x = ground.period_x;
    const float period_z = ground.period_z;

    // How far along the loop its nearest point to `at` lies, and how far
    // from it `at` is, across the seam.
    const auto nearest_along = [&] (float x, float z) {
      float best_along = 0.0f;
      float best = std::numeric_limits<float>::infinity ();
      float along = 0.0f;
      for (std::size_t i = 1; i < loop.points.size (); ++i) {
        const terrain::RoadPoint& a = loop.points[i - 1];
        const terrain::RoadPoint& b = loop.points[i];
        const float ax = terrain::minimum_image_delta (x - a.x_m, period_x);
        const float az = terrain::minimum_image_delta (z - a.z_m, period_z);
        const float dx = b.x_m - a.x_m, dz = b.z_m - a.z_m;
        const float span2 = dx * dx + dz * dz;
        const float t = span2 > 0.0f
                          ? std::clamp ((ax * dx + az * dz) / span2, 0.0f, 1.0f)
                          : 0.0f;
        const float off = std::hypot (ax - dx * t, az - dz * t);
        if (off < best) {
          best = off;
          best_along = along + t * std::sqrt (span2);
        }
        along += std::sqrt (span2);
      }
      return std::pair (best_along, best);
    };
    const auto distance_to =
      [&] (const terrain::RoadPlace& place, float x, float z) {
        return std::hypot (
          terrain::minimum_image_delta (place.x_m - x, period_x),
          terrain::minimum_image_delta (place.z_m - z, period_z));
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
        wants.push_back ({ 0.0f, 0, names[p], false });
        break;
      }
    if (wants.empty ())
      wants.push_back ({ 0.0f, 0, "", false });
    // Just past each cart road that leaves it, for the place that road
    // leads toward: the nearest of the places off the loop to where it
    // branches.
    for (const terrain::Road& road : network.roads) {
      if (road.loop || road.kind != terrain::Road::Kind::Road ||
          road.points.size () < 2)
        continue;
      for (const bool back : { false, true }) {
        const terrain::RoadPoint& end =
          back ? road.points.back () : road.points.front ();
        const terrain::RoadPoint& far =
          back ? road.points.front () : road.points.back ();
        const auto [along, off] = nearest_along (end.x_m, end.z_m);
        if (off > 8.0f)
          continue;
        std::size_t nearest = network.places.size ();
        float nearest_m = std::numeric_limits<float>::infinity ();
        for (std::size_t p = 0; p < network.places.size (); ++p) {
          const terrain::RoadPlace& place = network.places[p];
          if (place.kind == terrain::RoadPlace::Kind::Landing)
            continue;
          // Toward it along the branch: nearer its far end than the loop.
          const float m = distance_to (place, end.x_m, end.z_m);
          if (distance_to (place, far.x_m, far.z_m) < m && m < nearest_m) {
            nearest_m = m;
            nearest = p;
          }
        }
        std::string name;
        if (nearest < names.size ())
          name =
            nearest_m < 600.0f ? names[nearest] : names[nearest] + " vägskäl";
        wants.push_back ({ along + 30.0f, 1, name, false });
      }
    }
    // By each farm near it.
    for (const Farm& farm : places.farms) {
      if (farm.buildings.empty () || farm.ruined)
        continue;
      const Vec3 house = farm.buildings.front ().at;
      const auto [along, off] = nearest_along (house[0], house[2]);
      if (off < 160.0f)
        wants.push_back ({ along, 2, "", true });
    }
    // The most wanted first; none within a few hundred metres of
    // another.
    std::ranges::stable_sort (wants, {}, &Want::rank);
    std::vector<Want> kept;
    for (Want& want : wants) {
      want.along = wrap_along (want.along, length);
      if (std::ranges::any_of (kept, [&] (const Want& other) {
            return apart_along (other.along, want.along, length) < 350.0f;
          }))
        continue;
      kept.push_back (std::move (want));
    }
    // And between them wherever they lie far apart.
    std::ranges::sort (kept, {}, &Want::along);
    {
      constexpr float farthest = 1400.0f, between = 1100.0f;
      std::vector<Want> filled;
      for (std::size_t i = 0; i < kept.size (); ++i) {
        filled.push_back (kept[i]);
        const float from = kept[i].along;
        const float to = i + 1 < kept.size () ? kept[i + 1].along
                                              : kept.front ().along + length;
        const float gap = to - from;
        if (gap <= farthest)
          continue;
        const int more = static_cast<int> (std::ceil (gap / between)) - 1;
        for (int k = 1; k <= more; ++k)
          filled.push_back (
            { wrap_along (from + gap * k / (more + 1), length), 3, "", false });
      }
      kept = std::move (filled);
    }

    // Each where the verge is level and dry beside it, off any bridge and
    // clear of other roads, the nearest such within a few dozen metres;
    // the landing's anyway.
    std::vector<std::pair<float, float>> decks;
    {
      std::vector<float> along (loop.points.size (), 0.0f);
      for (std::size_t i = 1; i < loop.points.size (); ++i)
        along[i] = along[i - 1] +
                   std::hypot (loop.points[i].x_m - loop.points[i - 1].x_m,
                               loop.points[i].z_m - loop.points[i - 1].z_m);
      for (const terrain::RoadBridge& bridge : loop.bridges)
        decks.push_back (
          { along[bridge.first] - 15.0f, along[bridge.last] + 15.0f });
    }
    const float tread = ground.roads ? ground.roads->formation ().half_width +
                                         ground.roads->formation ().shoulder
                                     : 3.25f;
    const float sign_off = tread + 0.9f;
    const float shelter_off = tread + 3.0f;
    // How poorly a stop would stand at `along`, or nothing if it cannot.
    const auto badness = [&] (float along, bool& sheltered) -> float {
      for (const auto& [from, to] : decks)
        if (along > from && along < to)
          return -1.0f;
      const Vec3 centre = line.point (along * u::m);
      const Vec3 right = left_of (line.direction (along * u::m)) * -1.0f;
      float worst = 0.0f;
      for (const float off :
           { sign_off, shelter_off - 0.8f, shelter_off, shelter_off + 0.8f }) {
        const Vec3 p = centre + right * off;
        const float height = ground.ground (p[0], p[2]);
        if (ground.water (p[0], p[2]) > height - 0.3f)
          return -1.0f;
        worst = std::max (worst, std::fabs (height - centre[1]));
        if (ground.roads && off > sign_off) {
          const terrain::RoadSample sample = ground.roads->nearest (p[0], p[2]);
          if (sample.found && std::fabs (sample.across) < tread + 0.5f)
            return -1.0f;
        }
      }
      sheltered = worst < 0.7f;
      return worst < 1.4f ? worst : -1.0f;
    };
    std::vector<BusStop> stops;
    std::size_t rolled = 0;
    for (Want& want : kept) {
      float best_along = want.along;
      float best = std::numeric_limits<float>::infinity ();
      bool best_sheltered = false;
      for (float shift = 0.0f; shift <= 64.0f; shift += 4.0f)
        for (const float sign : { 1.0f, -1.0f }) {
          if (shift == 0.0f && sign < 0.0f)
            continue;
          const float along = wrap_along (want.along + sign * shift, length);
          bool sheltered = false;
          const float bad = badness (along, sheltered);
          if (bad < 0.0f)
            continue;
          const float score = bad + (sheltered ? 0.0f : 1.0f) + 0.01f * shift;
          if (score < best) {
            best = score;
            best_along = along;
            best_sheltered = sheltered;
          }
        }
      if (!(best < std::numeric_limits<float>::infinity ()) && want.rank > 0)
        continue;
      if (!claim (want.name)) {
        do
          want.name = name_bus_stop (seed, rolled++, want.farm, taken);
        while (!claim (want.name));
      }
      BusStop stop;
      stop.name = want.name;
      stop.along = best_along * u::m;
      stop.forward = line.direction (stop.along);
      const Vec3 right = left_of (stop.forward) * -1.0f;
      const Vec3 centre = line.point (stop.along);
      const auto on_ground = [&] (const Vec3& p) {
        return Vec3 (terrain::wrap_coordinate (p[0], period_x),
                     ground.ground (p[0], p[2]),
                     terrain::wrap_coordinate (p[2], period_z));
      };
      stop.sign = on_ground (centre + right * sign_off);
      if (best_sheltered)
        stop.shelter = on_ground (centre + right * shelter_off);
      stops.push_back (std::move (stop));
    }
    BusLine planned (loop, std::move (stops));
    std::cerr << "moppe: bus: a loop of " << std::lround (length) << " m, "
              << planned.stops ().size () << " stops, " << planned.buses ()
              << " buses, "
              << std::lround (seconds_value (planned.round_trip ()))
              << " s round:";
    for (const BusStop& stop : planned.stops ())
      std::cerr << ' ' << stop.name << " ("
                << std::lround (stop.along.numerical_value_in (u::m)) << ")";
    std::cerr << std::endl;
    return planned;
  }

  MachineBody bus_body (const BusPose& pose,
                        const Vec3& near,
                        float period_x,
                        float period_z) {
    static const Bumper bumpers[] = {
      { Vec3 (0, 1.35f, 4.3f), 1.3f * u::m },
      { Vec3 (0, 1.35f, 2.15f), 1.3f * u::m },
      { Vec3 (0, 1.35f, 0.0f), 1.3f * u::m },
      { Vec3 (0, 1.35f, -2.15f), 1.3f * u::m },
      { Vec3 (0, 1.35f, -4.3f), 1.3f * u::m },
    };
    MachineBody body;
    body.position = pose.position;
    body.position[0] =
      terrain::nearest_image (pose.position[0], near[0], period_x);
    body.position[2] =
      terrain::nearest_image (pose.position[2], near[2], period_z);
    body.forward = pose.forward;
    body.up = pose.up;
    body.velocity = pose.forward * speed_value (pose.speed);
    body.mass = 12000.0f * u::kg;
    body.ours = false;
    body.bumpers = bumpers;
    return body;
  }

  Mat4 bus_frame (const BusPose& pose) {
    return model_mesh::body_frame (pose.position, pose.forward, pose.up);
  }

  Vec3 bus_door (const BusPose& pose) {
    return bus_frame (pose).transform_point (bus_model::door);
  }

  Vec3 bus_seat_in_model (std::size_t seat) {
    return seat == 0 ? bus_model::seat_01 : bus_model::seat_02;
  }

  Vec3 bus_seat (const BusPose& pose, std::size_t seat) {
    return bus_frame (pose).transform_point (bus_seat_in_model (seat));
  }

  play_time_t bus_time (const GameLogicState& logic) {
    return play_time (seconds_into_play (logic.m_total_time) +
                      logic.m_bus_lead);
  }

  std::optional<std::size_t> bus_at_hand (const BusLine& line,
                                          const Vec3& feet,
                                          play_time_t now,
                                          float period_x,
                                          float period_z) {
    constexpr float reach = 4.5f;
    const std::vector<BusPose> buses = line.where (now);
    for (std::size_t k = 0; k < buses.size (); ++k) {
      if (!buses[k].at_stop)
        continue;
      const Vec3 door = bus_door (buses[k]);
      if (std::hypot (
            terrain::minimum_image_delta (door[0] - feet[0], period_x),
            terrain::minimum_image_delta (door[2] - feet[2], period_z)) < reach)
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
        logic.m_seat_velocity = pose.forward * speed_value (pose.speed);
      }

      // Down by the door, on the verge side, facing out.
      void step_down (GameSession& session, const BusPose& pose) {
        const Vec3 left = normalized (cross (pose.up, pose.forward));
        Vec3 out = left * -1.0f;
        out[1] = 0.0f;
        session.walker ().spawn (
          moppe::position (bus_door (pose) + Vec3 (0, 0.2f, 0)),
          normalized (out));
        session.set_mode (Doing::on_foot ());
        session.logic ().m_bus_alight = false;
        session.logic ().m_fp_eye =
          position (session.walker ().eye_position ());
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
                                            const Vec3& at,
                                            meters_t reach,
                                            float period_x,
                                            float period_z) {
    std::optional<std::size_t> nearest;
    float best = meters_value (reach);
    for (std::size_t i = 0; i < line.stops ().size (); ++i) {
      const Vec3& sign = line.stops ()[i].sign;
      const float d =
        std::hypot (terrain::minimum_image_delta (sign[0] - at[0], period_x),
                    terrain::minimum_image_delta (sign[2] - at[2], period_z));
      if (d < best) {
        best = d;
        nearest = i;
      }
    }
    return nearest;
  }
}
