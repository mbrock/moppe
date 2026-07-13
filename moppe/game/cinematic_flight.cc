#include <moppe/game/cinematic_flight.hh>

#include <algorithm>
#include <cmath>
#include <limits>

namespace moppe::game {
  namespace {
    constexpr terrain::CellIndex no_cell = terrain::no_cell;

    struct FeatureCandidate {
      terrain::CellIndex cell = no_cell;
      float score = -std::numeric_limits<float>::infinity ();
      Vec3 direction { 0, 0, 1 };
    };

    float clamp_length (Vec3& value, float maximum) {
      const float magnitude = length (value);
      if (magnitude > maximum && magnitude > 1e-5f)
        value *= maximum / magnitude;
      return magnitude;
    }

    int flight_minimum_image_delta (int delta, int period) {
      if (delta > period / 2)
        delta -= period;
      else if (delta < -period / 2)
        delta += period;
      return delta;
    }

    Vec3
    unwrap_near (Vec3 point, const Vec3& reference, const map::HeightMap& map) {
      if (!map.periodic ())
        return point;
      const Vec3 size = map.size ();
      for (int axis : { 0, 2 }) {
        while (point[axis] - reference[axis] > size[axis] * 0.5f)
          point[axis] -= size[axis];
        while (point[axis] - reference[axis] < -size[axis] * 0.5f)
          point[axis] += size[axis];
      }
      return point;
    }

    float flight_height_sample (const map::HeightMap& map, int x, int z) {
      if (map.periodic ()) {
        const int width = map.unique_width ();
        const int height = map.unique_height ();
        x = ((x % width) + width) % width;
        z = ((z % height) + height) % height;
      } else {
        x = std::clamp (x, 0, map.unique_width () - 1);
        z = std::clamp (z, 0, map.unique_height () - 1);
      }
      return map.get (x, z) * map.scale ()[1];
    }

    Vec3 flight_cell_position (terrain::CellIndex cell,
                               const map::HeightMap& map,
                               const terrain::FloodField& flood,
                               const terrain::LakeCensus& census,
                               const terrain::DrainageGraph& drainage) {
      const std::size_t width = drainage.width ();
      const int x = static_cast<int> (cell % width);
      const int z = static_cast<int> (cell / width);
      const Vec3 scale = map.scale ();
      const bool wet =
        census.body[cell] != terrain::LakeCensus::dry || flood.ocean[cell];
      const float y = wet ? flood.water_level.values ()[cell] * scale[1]
                          : map.get (x, z) * scale[1];
      return Vec3 (x * scale[0], y, z * scale[2]);
    }

    Vec3 flight_cell_direction (terrain::CellIndex from,
                                terrain::CellIndex to,
                                const map::HeightMap& map,
                                const terrain::DrainageGraph& drainage) {
      const int width = static_cast<int> (drainage.width ());
      const int height = static_cast<int> (drainage.height ());
      const int from_x = static_cast<int> (from % drainage.width ());
      const int from_z = static_cast<int> (from / drainage.width ());
      const int to_x = static_cast<int> (to % drainage.width ());
      const int to_z = static_cast<int> (to / drainage.width ());
      int dx = to_x - from_x;
      int dz = to_z - from_z;
      if (map.periodic ()) {
        dx = flight_minimum_image_delta (dx, width);
        dz = flight_minimum_image_delta (dz, height);
      }
      Vec3 direction (dx * map.scale ()[0], 0, dz * map.scale ()[2]);
      if (length2 (direction) < 1e-5f)
        direction = Vec3 (0, 0, 1);
      return normalized (direction);
    }

    FeatureCandidate choose_valley (const terrain::DrainageGraph& drainage,
                                    const terrain::RiverNetwork& rivers) {
      FeatureCandidate best;
      for (const terrain::RiverReach& reach : rivers.reaches) {
        if (reach.cells.size () < 4)
          continue;
        const float area =
          std::max (1.0f, square_meters_value (reach.downstream_area));
        const float score = std::sqrt (area) * reach.cells.size ();
        if (score > best.score) {
          const std::size_t middle = reach.cells.size () / 2;
          best.cell = reach.cells[middle];
          best.score = score;
        }
      }
      return best;
    }

    const terrain::RiverReach*
    valley_reach (terrain::CellIndex cell,
                  const terrain::RiverNetwork& rivers) {
      for (const terrain::RiverReach& reach : rivers.reaches)
        if (std::find (reach.cells.begin (), reach.cells.end (), cell) !=
            reach.cells.end ())
          return &reach;
      return nullptr;
    }

    FeatureCandidate
    choose_flight_waterfall (const terrain::RiverNetwork& rivers) {
      FeatureCandidate best;
      for (const terrain::Waterfall& fall : rivers.waterfalls) {
        const float score =
          meters_value (fall.drop) *
          std::sqrt (
            std::max (1.0f, square_meters_value (fall.contributing_area)));
        if (score > best.score) {
          best.cell = fall.lip_cell;
          best.score = score;
        }
      }
      return best;
    }

    FeatureCandidate choose_flight_lake (const terrain::FloodField& flood,
                                         const terrain::LakeCensus& census) {
      const terrain::WaterBody* body = nullptr;
      for (const terrain::WaterBody& candidate : census.bodies) {
        if (candidate.classification == terrain::WaterBodyClass::Sea)
          continue;
        if (!body || candidate.area > body->area)
          body = &candidate;
      }
      FeatureCandidate best;
      if (!body)
        return best;
      best.score = square_meters_value (body->area);
      float deepest = -1.0f;
      for (std::uint32_t cell = 0; cell < census.body.size (); ++cell)
        if (census.body[cell] == body->id &&
            flood.water_depth.values ()[cell] > deepest) {
          deepest = flood.water_depth.values ()[cell];
          best.cell = terrain::CellIndex (cell);
        }
      return best;
    }

    FeatureCandidate choose_peak (const map::HeightMap& map) {
      FeatureCandidate best;
      const int width = map.unique_width ();
      const int height = map.unique_height ();
      const int stride = std::max (2, width / 96);
      const int radius = std::max (3, width / 64);
      const int margin = map.periodic () ? 0 : radius;
      for (int z = margin; z < height - margin; z += stride)
        for (int x = margin; x < width - margin; x += stride) {
          const float center = flight_height_sample (map, x, z);
          float ring_high = -std::numeric_limits<float>::infinity ();
          float ring_low = std::numeric_limits<float>::infinity ();
          for (int dz : { -radius, 0, radius })
            for (int dx : { -radius, 0, radius }) {
              if (dx == 0 && dz == 0)
                continue;
              const float sample = flight_height_sample (map, x + dx, z + dz);
              ring_high = std::max (ring_high, sample);
              ring_low = std::min (ring_low, sample);
            }
          if (center < ring_high)
            continue;
          const float prominence = center - ring_low;
          const float score = center + 1.8f * prominence;
          if (score > best.score) {
            best.cell = static_cast<terrain::CellIndex> (z * width + x);
            best.score = score;
          }
        }
      return best;
    }

    FeatureCandidate choose_saddle (const map::HeightMap& map) {
      FeatureCandidate best;
      const int width = map.unique_width ();
      const int height = map.unique_height ();
      const int stride = std::max (2, width / 96);
      const int radius = std::max (3, width / 80);
      const int margin = map.periodic () ? 0 : radius;
      for (int z = margin; z < height - margin; z += stride)
        for (int x = margin; x < width - margin; x += stride) {
          const float c = flight_height_sample (map, x, z);
          const float n = flight_height_sample (map, x, z - radius);
          const float s = flight_height_sample (map, x, z + radius);
          const float e = flight_height_sample (map, x + radius, z);
          const float w = flight_height_sample (map, x - radius, z);
          const float across_x =
            std::min (n - c, s - c) + std::min (c - e, c - w);
          const float across_z =
            std::min (e - c, w - c) + std::min (c - n, c - s);
          const float shape = std::max (across_x, across_z);
          if (shape <= 0.0f)
            continue;
          const float score = shape + 0.12f * c;
          if (score > best.score) {
            best.cell = static_cast<terrain::CellIndex> (z * width + x);
            best.score = score;
            best.direction =
              across_x > across_z ? Vec3 (1, 0, 0) : Vec3 (0, 0, 1);
          }
        }
      return best;
    }

    void add_waypoint (CinematicFlightPlan& plan,
                       const map::HeightMap& map,
                       Vec3 position,
                       Vec3 subject,
                       float clearance,
                       float speed,
                       float field_of_view) {
      position[1] = std::max (
        position[1],
        map.interpolated_height (position[0], position[2]) + clearance);
      plan.waypoints.push_back ({ .position = position,
                                  .subject = subject,
                                  .cruise_speed = speed,
                                  .field_of_view = field_of_view });
    }

    void add_transit (CinematicFlightPlan& plan,
                      const map::HeightMap& map,
                      Vec3 destination,
                      const Vec3& subject) {
      if (plan.waypoints.empty ())
        return;
      const Vec3 start = plan.waypoints.back ().position;
      destination = unwrap_near (destination, start, map);
      const Vec3 delta = destination - start;
      const float horizontal = std::hypot (delta[0], delta[2]);
      const int steps = std::max (1, static_cast<int> (horizontal / 650.0f));
      for (int i = 1; i < steps; ++i) {
        const float t = static_cast<float> (i) / steps;
        Vec3 point = start + delta * t;
        point[1] = map.interpolated_height (point[0], point[2]) + 150.0f;
        add_waypoint (plan, map, point, subject, 130.0f, 185.0f, 56.0f);
      }
    }

    void record_landmark (CinematicFlightPlan& plan,
                          CinematicLandmarkKind kind,
                          const FeatureCandidate& feature,
                          const Vec3& position) {
      if (feature.cell == no_cell)
        return;
      plan.landmarks.push_back ({ .kind = kind,
                                  .cell = feature.cell,
                                  .score = feature.score,
                                  .position = position });
    }
  }

  std::string_view
  cinematic_landmark_name (CinematicLandmarkKind kind) noexcept {
    switch (kind) {
    case CinematicLandmarkKind::Valley:
      return "valley";
    case CinematicLandmarkKind::Waterfall:
      return "waterfall";
    case CinematicLandmarkKind::Lake:
      return "lake";
    case CinematicLandmarkKind::Saddle:
      return "saddle";
    case CinematicLandmarkKind::Peak:
      return "peak";
    case CinematicLandmarkKind::Arrival:
      return "arrival";
    }
    return "landmark";
  }

  CinematicFlightPlan
  plan_cinematic_flight (const map::HeightMap& map,
                         const terrain::FloodField& flood,
                         const terrain::LakeCensus& census,
                         const terrain::DrainageGraph& drainage,
                         const terrain::RiverNetwork& rivers,
                         const Vec3& arrival) {
    CinematicFlightPlan plan;
    if (drainage.receiver.empty ())
      return plan;

    const FeatureCandidate valley = choose_valley (drainage, rivers);
    const FeatureCandidate waterfall = choose_flight_waterfall (rivers);
    const FeatureCandidate lake = choose_flight_lake (flood, census);
    const FeatureCandidate saddle = choose_saddle (map);
    const FeatureCandidate peak = choose_peak (map);

    if (valley.cell != no_cell) {
      const terrain::RiverReach* reach = valley_reach (valley.cell, rivers);
      if (reach && !reach->cells.empty ()) {
        Vec3 previous = flight_cell_position (
          reach->cells.front (), map, flood, census, drainage);
        const std::size_t samples =
          std::min<std::size_t> (6, reach->cells.size ());
        for (std::size_t i = 0; i < samples; ++i) {
          const std::size_t index = i * (reach->cells.size () - 1) /
                                    std::max<std::size_t> (1, samples - 1);
          const terrain::CellIndex cell = reach->cells[index];
          Vec3 ground =
            flight_cell_position (cell, map, flood, census, drainage);
          ground = unwrap_near (ground, previous, map);
          const terrain::CellIndex next = drainage.receiver[cell];
          const Vec3 flow = flight_cell_direction (cell, next, map, drainage);
          const Vec3 side (-flow[2], 0, flow[0]);
          Vec3 eye = ground - flow * (i == 0 ? 95.0f : 28.0f) +
                     side * (14.0f * std::sin (i * 1.7f));
          eye[1] = ground[1] + (i == 0 ? 52.0f : 22.0f);
          Vec3 subject = ground + flow * 85.0f;
          subject[1] = map.interpolated_height (subject[0], subject[2]) + 3.0f;
          add_waypoint (plan, map, eye, subject, 18.0f, 78.0f, 67.0f);
          previous = ground;
        }
        const Vec3 position =
          flight_cell_position (valley.cell, map, flood, census, drainage);
        record_landmark (plan, CinematicLandmarkKind::Valley, valley, position);
      }
    }

    const FeatureCandidate water = waterfall.cell != no_cell ? waterfall : lake;
    if (water.cell != no_cell) {
      Vec3 subject =
        flight_cell_position (water.cell, map, flood, census, drainage);
      if (!plan.waypoints.empty ())
        subject = unwrap_near (subject, plan.waypoints.back ().position, map);
      Vec3 flow = flight_cell_direction (
        water.cell, drainage.receiver[water.cell], map, drainage);
      const Vec3 side (-flow[2], 0, flow[0]);
      Vec3 approach = subject - flow * 190.0f + side * 75.0f;
      approach[1] = subject[1] + (waterfall.cell != no_cell ? 100.0f : 125.0f);
      add_transit (plan, map, approach, subject);
      add_waypoint (plan, map, approach, subject, 65.0f, 105.0f, 59.0f);
      Vec3 close = subject - flow * 35.0f + side * 28.0f;
      close[1] = subject[1] + (waterfall.cell != no_cell ? 32.0f : 58.0f);
      add_waypoint (plan, map, close, subject, 24.0f, 62.0f, 52.0f);
      Vec3 exit = subject + flow * 170.0f - side * 55.0f;
      exit[1] = subject[1] + 85.0f;
      add_waypoint (plan, map, exit, subject, 55.0f, 115.0f, 58.0f);
      record_landmark (plan,
                       waterfall.cell != no_cell
                         ? CinematicLandmarkKind::Waterfall
                         : CinematicLandmarkKind::Lake,
                       water,
                       subject);
    }

    if (saddle.cell != no_cell) {
      Vec3 subject =
        flight_cell_position (saddle.cell, map, flood, census, drainage);
      if (!plan.waypoints.empty ())
        subject = unwrap_near (subject, plan.waypoints.back ().position, map);
      Vec3 entry = subject - saddle.direction * 260.0f;
      entry[1] = subject[1] + 90.0f;
      add_transit (plan, map, entry, subject);
      add_waypoint (plan, map, entry, subject, 60.0f, 125.0f, 60.0f);
      Vec3 pass = subject - saddle.direction * 20.0f;
      pass[1] = subject[1] + 28.0f;
      Vec3 beyond = subject + saddle.direction * 150.0f;
      beyond[1] = map.interpolated_height (beyond[0], beyond[2]) + 12.0f;
      add_waypoint (plan, map, pass, beyond, 20.0f, 88.0f, 68.0f);
      Vec3 exit = subject + saddle.direction * 240.0f;
      exit[1] = subject[1] + 70.0f;
      add_waypoint (plan, map, exit, beyond, 45.0f, 125.0f, 62.0f);
      record_landmark (plan, CinematicLandmarkKind::Saddle, saddle, subject);
    }

    if (peak.cell != no_cell) {
      Vec3 subject =
        flight_cell_position (peak.cell, map, flood, census, drainage);
      if (!plan.waypoints.empty ())
        subject = unwrap_near (subject, plan.waypoints.back ().position, map);
      const float radius = std::clamp (map.size ()[0] * 0.055f, 160.0f, 330.0f);
      Vec3 first = subject + Vec3 (radius, radius * 0.42f, 0);
      add_transit (plan, map, first, subject);
      for (int i = 0; i < 4; ++i) {
        const float angle = 0.35f + i * 0.62f;
        Vec3 eye = subject + Vec3 (std::cos (angle) * radius,
                                   radius * (0.36f + 0.04f * i),
                                   std::sin (angle) * radius);
        Vec3 look = subject + Vec3 (0, radius * 0.08f, 0);
        add_waypoint (plan, map, eye, look, 70.0f, 72.0f, 50.0f);
      }
      record_landmark (plan, CinematicLandmarkKind::Peak, peak, subject);
    }

    Vec3 final_subject = arrival;
    if (!plan.waypoints.empty ())
      final_subject =
        unwrap_near (final_subject, plan.waypoints.back ().position, map);
    Vec3 reveal = final_subject + Vec3 (-150.0f, 95.0f, -150.0f);
    add_transit (plan, map, reveal, final_subject);
    add_waypoint (plan, map, reveal, final_subject, 70.0f, 115.0f, 57.0f);
    Vec3 final_eye = final_subject + Vec3 (-28.0f, 15.0f, -28.0f);
    add_waypoint (plan, map, final_eye, final_subject, 9.0f, 42.0f, 66.0f);
    plan.landmarks.push_back ({ .kind = CinematicLandmarkKind::Arrival,
                                .cell = no_cell,
                                .score = 0.0f,
                                .position = arrival });
    return plan;
  }

  void CinematicFlight::start (const CinematicFlightPlan& plan) {
    m_waypoints = plan.waypoints;
    m_waypoint = 1;
    m_elapsed = 0.0f;
    m_final_hold = 0.0f;
    m_manual_offset = Vec3 ();
    m_acceleration = Vec3 ();
    m_bank = 0.0f;
    m_active = m_waypoints.size () >= 2;
    if (!m_active)
      return;
    m_position = m_waypoints.front ().position;
    m_subject = m_waypoints.front ().subject;
    const Vec3 initial = m_waypoints[1].position - m_position;
    m_velocity = length2 (initial) > 1e-5f
                   ? normalized (initial) * m_waypoints.front ().cruise_speed
                   : Vec3 (0, 0, m_waypoints.front ().cruise_speed);
    m_field_of_view = m_waypoints.front ().field_of_view;
  }

  void CinematicFlight::stop () noexcept {
    m_active = false;
  }

  void CinematicFlight::tick (float dt,
                              const map::HeightMap& map,
                              const CinematicFlightControls& controls) {
    if (!m_active)
      return;
    dt = std::clamp (dt, 1.0f / 240.0f, 1.0f / 20.0f);
    m_elapsed += dt;

    CinematicFlightWaypoint& gate = m_waypoints[m_waypoint];
    Vec3 travel = gate.position + m_manual_offset - m_position;
    float distance = length (travel);
    const float capture_radius = std::max (22.0f, gate.cruise_speed * 0.42f);
    const bool passed_gate =
      dot (travel, m_velocity) < 0.0f && distance < gate.cruise_speed * 1.35f;
    if ((distance < capture_radius || passed_gate) &&
        m_waypoint + 1 < m_waypoints.size ()) {
      ++m_waypoint;
      travel = m_waypoints[m_waypoint].position + m_manual_offset - m_position;
      distance = length (travel);
    }

    const Vec3 forward_body =
      length2 (m_velocity) > 1e-5f ? normalized (m_velocity) : Vec3 (0, 0, 1);
    Vec3 right = cross (forward_body, Vec3 (0, 1, 0));
    if (length2 (right) < 1e-5f)
      right = Vec3 (1, 0, 0);
    else
      normalize (right);
    const float input_alpha = 1.0f - std::exp (-2.2f * dt);
    const Vec3 wanted_offset =
      right * (controls.lateral * 70.0f) + Vec3 (0, controls.lift * 55.0f, 0);
    m_manual_offset += (wanted_offset - m_manual_offset) * input_alpha;

    const float pace = std::clamp (controls.pace, -1.0f, 1.0f);
    float desired_speed =
      m_waypoints[m_waypoint].cruise_speed * (1.0f + 0.35f * pace);
    // Reserve enough distance to turn velocity into the next gate instead of
    // orbiting it. This is the same stopping-distance law a flight controller
    // uses for waypoint capture, with a low nonzero through-speed for the
    // documentary move.
    const float braking_distance =
      std::max (0.0f, distance - capture_radius * 0.55f);
    const float capture_speed = 24.0f;
    const float braking_speed = std::sqrt (capture_speed * capture_speed +
                                           2.0f * 6.0f * braking_distance);
    desired_speed = std::min (desired_speed, braking_speed);
    const bool final_gate = m_waypoint + 1 == m_waypoints.size ();
    if (final_gate && distance < 55.0f)
      desired_speed = 0.0f;
    else if (final_gate)
      desired_speed *= std::clamp (distance / 90.0f, 0.12f, 1.0f);
    Vec3 desired_velocity =
      length2 (travel) > 1e-5f ? normalized (travel) * desired_speed : Vec3 ();

    Vec3 wanted_acceleration = (desired_velocity - m_velocity) * 0.95f;
    clamp_length (wanted_acceleration, 8.0f);
    Vec3 acceleration_step = wanted_acceleration - m_acceleration;
    clamp_length (acceleration_step, 13.0f * dt);
    m_acceleration += acceleration_step;
    m_velocity += m_acceleration * dt;
    clamp_length (m_velocity, 230.0f);
    m_position += m_velocity * dt;

    // A rotorcraft can trade speed for lift, but it cannot negotiate with a
    // ridge hidden just beyond the current sample. Probe a short braking
    // horizon and lift the body early enough to retain a clean sight corridor.
    float floor =
      map.interpolated_height (m_position[0], m_position[2]) + 18.0f;
    for (int i = 1; i <= 10; ++i) {
      const float lookahead = 0.18f * i;
      const Vec3 sample = m_position + m_velocity * lookahead;
      const float clearance = 18.0f + 3.0f * i;
      floor =
        std::max (floor,
                  map.interpolated_height (sample[0], sample[2]) + clearance -
                    std::max (0.0f, m_velocity[1]) * lookahead);
    }
    if (m_position[1] < floor) {
      m_position[1] = floor;
      m_velocity[1] = std::max (m_velocity[1], 0.0f);
      m_acceleration[1] = std::max (m_acceleration[1], 0.0f);
    }

    const CinematicFlightWaypoint& current = m_waypoints[m_waypoint];
    const float gimbal_alpha = 1.0f - std::exp (-2.8f * dt);
    m_subject += (current.subject - m_subject) * gimbal_alpha;
    m_field_of_view += (current.field_of_view - m_field_of_view) * gimbal_alpha;
    const float lateral_acceleration = dot (m_acceleration, right);
    const float wanted_bank =
      std::clamp (-lateral_acceleration / 9.81f, -0.38f, 0.38f);
    m_bank += (wanted_bank - m_bank) * (1.0f - std::exp (-3.5f * dt));

    if (final_gate && distance < 55.0f) {
      m_final_hold += dt;
      if (m_final_hold >= 1.4f)
        m_active = false;
    } else
      m_final_hold = 0.0f;
  }

  Vec3 CinematicFlight::forward () const {
    Vec3 result = m_subject - m_position;
    if (length2 (result) < 1e-5f)
      result = length2 (m_velocity) > 1e-5f ? m_velocity : Vec3 (0, 0, 1);
    return normalized (result);
  }

  Mat4 CinematicFlight::view_matrix () const {
    const Vec3 look = forward ();
    Vec3 right = cross (look, Vec3 (0, 1, 0));
    if (length2 (right) < 1e-5f)
      right = Vec3 (1, 0, 0);
    else
      normalize (right);
    Vec3 up = normalized (cross (right, look));
    up = normalized (up * std::cos (m_bank) + right * std::sin (m_bank));
    return Mat4::look_at (m_position, m_position + look * 100.0f, up);
  }
}
