#include <moppe/game/cinematic_flight.hh>

#include <moppe/profile.hh>

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
      // This is an eagle flight, not a sightseeing helicopter. The authored
      // values describe the shape of each beat; the global scale keeps every
      // beat carrying decisive forward energy.
      constexpr float flight_speed_scale = 1.55f;
      position[1] = std::max (
        position[1],
        map.interpolated_height (position[0], position[2]) + clearance);
      plan.waypoints.push_back ({ .position = position,
                                  .subject = subject,
                                  .cruise_speed = speed * flight_speed_scale,
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
        add_waypoint (plan, map, point, subject, 130.0f, 230.0f, 58.0f);
      }
    }

    Vec3 trail_cell_position (terrain::CellIndex cell,
                              const terrain::TrailNetwork& trail,
                              const map::HeightMap& map) {
      const int width = static_cast<int> (trail.source_grid.unique_width ());
      const int x = static_cast<int> (cell.value % width);
      const int z = static_cast<int> (cell.value / width);
      const float world_x = x * trail.source_grid.spacing_x_m ();
      const float world_z = z * trail.source_grid.spacing_y_m ();
      return Vec3 (
        world_x, map.interpolated_height (world_x, world_z), world_z);
    }

    void add_trail_reveal (CinematicFlightPlan& plan,
                           const map::HeightMap& map,
                           const terrain::TrailNetwork& trail) {
      if (trail.cells.size () < 8 || trail.plan.home_base == no_cell)
        return;

      // One quick oblique sweep establishes the circuit as a place before the
      // broader landscape tour begins. Coarse gates reveal its large shape
      // without making the drone consume the entire loop.
      constexpr std::size_t samples = 3;
      const std::size_t look_ahead =
        std::max<std::size_t> (1, trail.cells.size () / 8);
      Vec3 previous_ground =
        trail_cell_position (trail.plan.home_base, trail, map);
      for (std::size_t sample = 0; sample < samples; ++sample) {
        const std::size_t index = sample * trail.cells.size () / 4;
        const std::size_t ahead_index =
          (index + look_ahead) % trail.cells.size ();
        Vec3 ground = trail_cell_position (trail.cells[index], trail, map);
        ground = unwrap_near (ground, previous_ground, map);
        Vec3 subject =
          trail_cell_position (trail.cells[ahead_index], trail, map);
        subject = unwrap_near (subject, ground, map);
        Vec3 forward = subject - ground;
        forward[1] = 0.0f;
        if (length2 (forward) < 1e-5f)
          forward = Vec3 (0, 0, 1);
        else
          normalize (forward);
        const Vec3 side (-forward[2], 0, forward[0]);
        const bool establishing = sample == 0;
        Vec3 eye = ground - forward * (establishing ? 120.0f : 48.0f) +
                   side * (establishing ? 105.0f : 42.0f);
        eye[1] = ground[1] + (establishing ? 150.0f : 76.0f);
        subject[1] = map.interpolated_height (subject[0], subject[2]) + 3.0f;
        add_waypoint (plan,
                      map,
                      eye,
                      subject,
                      establishing ? 105.0f : 55.0f,
                      establishing ? 215.0f : 245.0f,
                      establishing ? 64.0f : 68.0f);
        previous_ground = ground;
      }

      const Vec3 home = trail_cell_position (trail.plan.home_base, trail, map);
      plan.landmarks.push_back (
        { .kind = CinematicLandmarkKind::Trail,
          .cell = trail.plan.home_base,
          .score = static_cast<float> (trail.cells.size ()),
          .position = home });
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
    case CinematicLandmarkKind::Trail:
      return "trail";
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
                         const Vec3& arrival,
                         const terrain::TrailNetwork* trail) {
    MOPPE_PROFILE_ZONE ("plan_cinematic_flight");
    CinematicFlightPlan plan;
    if (drainage.receiver.empty ())
      return plan;

    const FeatureCandidate valley = choose_valley (drainage, rivers);
    const FeatureCandidate waterfall = choose_flight_waterfall (rivers);
    const FeatureCandidate lake = choose_flight_lake (flood, census);
    const FeatureCandidate saddle = choose_saddle (map);
    const FeatureCandidate peak = choose_peak (map);
    const FeatureCandidate water = waterfall.cell != no_cell ? waterfall : lake;

    if (trail)
      add_trail_reveal (plan, map, *trail);

    if (valley.cell != no_cell) {
      const terrain::RiverReach* reach = valley_reach (valley.cell, rivers);
      if (reach && !reach->cells.empty ()) {
        std::size_t last_index = reach->cells.size () - 1;
        const auto water_on_reach =
          std::find (reach->cells.begin (), reach->cells.end (), water.cell);
        if (water_on_reach != reach->cells.end ()) {
          const std::size_t water_index =
            static_cast<std::size_t> (water_on_reach - reach->cells.begin ());
          // Feed the valley run directly into the waterfall approach. Flying
          // beyond a feature and doubling back created a disguised hairpin.
          last_index = water_index > 1 ? water_index - 1 : water_index;
        }
        Vec3 previous = flight_cell_position (
          reach->cells.front (), map, flood, census, drainage);
        const std::size_t samples = std::min<std::size_t> (6, last_index + 1);
        for (std::size_t i = 0; i < samples; ++i) {
          const std::size_t index =
            i * last_index / std::max<std::size_t> (1, samples - 1);
          const terrain::CellIndex cell = reach->cells[index];
          Vec3 ground =
            flight_cell_position (cell, map, flood, census, drainage);
          ground = unwrap_near (ground, previous, map);
          const terrain::CellIndex next = drainage.receiver[cell];
          const Vec3 flow = flight_cell_direction (cell, next, map, drainage);
          const Vec3 side (-flow[2], 0, flow[0]);
          Vec3 eye = ground - flow * (i == 0 ? 95.0f : 28.0f) + side * 8.0f;
          eye[1] = ground[1] + (i == 0 ? 52.0f : 22.0f);
          Vec3 subject = ground + flow * 85.0f;
          subject[1] = map.interpolated_height (subject[0], subject[2]) + 3.0f;
          add_waypoint (plan, map, eye, subject, 18.0f, 118.0f, 69.0f);
          previous = ground;
        }
        const Vec3 position =
          flight_cell_position (valley.cell, map, flood, census, drainage);
        record_landmark (plan, CinematicLandmarkKind::Valley, valley, position);
      }
    }

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
      add_waypoint (plan, map, approach, subject, 65.0f, 165.0f, 61.0f);
      Vec3 close = subject - flow * 35.0f + side * 28.0f;
      close[1] = subject[1] + (waterfall.cell != no_cell ? 32.0f : 58.0f);
      add_waypoint (plan, map, close, subject, 24.0f, 110.0f, 55.0f);
      Vec3 exit = subject + flow * 170.0f - side * 55.0f;
      exit[1] = subject[1] + 85.0f;
      add_waypoint (plan, map, exit, subject, 55.0f, 175.0f, 61.0f);
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
      add_waypoint (plan, map, entry, subject, 60.0f, 180.0f, 63.0f);
      Vec3 pass = subject - saddle.direction * 20.0f;
      pass[1] = subject[1] + 28.0f;
      Vec3 beyond = subject + saddle.direction * 150.0f;
      beyond[1] = map.interpolated_height (beyond[0], beyond[2]) + 12.0f;
      add_waypoint (plan, map, pass, beyond, 20.0f, 145.0f, 72.0f);
      Vec3 exit = subject + saddle.direction * 240.0f;
      exit[1] = subject[1] + 70.0f;
      add_waypoint (plan, map, exit, beyond, 45.0f, 190.0f, 65.0f);
      record_landmark (plan, CinematicLandmarkKind::Saddle, saddle, subject);
    }

    if (peak.cell != no_cell) {
      Vec3 subject =
        flight_cell_position (peak.cell, map, flood, census, drainage);
      if (!plan.waypoints.empty ())
        subject = unwrap_near (subject, plan.waypoints.back ().position, map);
      const float radius = std::clamp (map.size ()[0] * 0.055f, 160.0f, 330.0f);
      Vec3 incoming (1, 0, 0);
      if (!plan.waypoints.empty ()) {
        incoming = subject - plan.waypoints.back ().position;
        incoming[1] = 0.0f;
        if (length2 (incoming) < 1e-5f)
          incoming = Vec3 (1, 0, 0);
        else
          normalize (incoming);
      }
      Vec3 unwrapped_arrival = unwrap_near (arrival, subject, map);
      Vec3 outgoing = unwrapped_arrival - subject;
      outgoing[1] = 0.0f;
      if (length2 (outgoing) < 1e-5f)
        outgoing = incoming;
      else
        normalize (outgoing);
      const float signed_turn =
        std::atan2 (incoming[0] * outgoing[2] - incoming[2] * outgoing[0],
                    dot (incoming, outgoing));
      const float turn_sign = signed_turn < 0.0f ? -1.0f : 1.0f;
      const float orbit_angle =
        std::clamp (std::abs (signed_turn), 0.65f, 2.2f);
      const Vec3 radial = Vec3 (incoming[2], 0, -incoming[0]) * turn_sign;
      Vec3 first = subject + radial * radius;
      first[1] += radius * 0.36f;
      add_transit (plan, map, first, subject);
      for (int i = 0; i < 5; ++i) {
        const float progress = static_cast<float> (i) / 4.0f;
        const float angle = orbit_angle * progress;
        Vec3 eye = subject + radial * (std::cos (angle) * radius) +
                   incoming * (std::sin (angle) * radius);
        eye[1] += radius * (0.36f + 0.12f * progress);
        Vec3 look = subject + Vec3 (0, radius * 0.08f, 0);
        add_waypoint (plan, map, eye, look, 70.0f, 125.0f, 53.0f);
      }
      record_landmark (plan, CinematicLandmarkKind::Peak, peak, subject);
    }

    Vec3 final_subject = arrival;
    if (!plan.waypoints.empty ())
      final_subject =
        unwrap_near (final_subject, plan.waypoints.back ().position, map);
    Vec3 reveal = final_subject + Vec3 (-150.0f, 95.0f, -150.0f);
    add_transit (plan, map, reveal, final_subject);
    add_waypoint (plan, map, reveal, final_subject, 70.0f, 170.0f, 60.0f);
    Vec3 final_eye = final_subject + Vec3 (-28.0f, 15.0f, -28.0f);
    add_waypoint (plan, map, final_eye, final_subject, 9.0f, 75.0f, 68.0f);
    plan.landmarks.push_back ({ .kind = CinematicLandmarkKind::Arrival,
                                .cell = no_cell,
                                .score = 0.0f,
                                .position = arrival });
    return plan;
  }

  Vec3 CinematicFlight::curve_position (std::size_t segment, float t) const {
    const auto tangent = [this] (std::size_t index) {
      const Vec3& current = m_waypoints[index].position;
      if (index == 0)
        return (m_waypoints[1].position - current) * 0.42f;
      if (index + 1 == m_waypoints.size ())
        return (current - m_waypoints[index - 1].position) * 0.42f;

      const Vec3& previous = m_waypoints[index - 1].position;
      const Vec3& next = m_waypoints[index + 1].position;
      Vec3 direction = next - previous;
      if (length2 (direction) < 1e-5f)
        return Vec3 ();
      normalize (direction);
      const float incoming = length (current - previous);
      const float outgoing = length (next - current);
      return direction * (0.42f * std::min (incoming, outgoing));
    };

    const Vec3& from = m_waypoints[segment].position;
    const Vec3& to = m_waypoints[segment + 1].position;
    const Vec3 from_tangent = tangent (segment);
    const Vec3 to_tangent = tangent (segment + 1);
    const float t2 = t * t;
    const float t3 = t2 * t;
    const float h00 = 2.0f * t3 - 3.0f * t2 + 1.0f;
    const float h10 = t3 - 2.0f * t2 + t;
    const float h01 = -2.0f * t3 + 3.0f * t2;
    const float h11 = t3 - t2;
    return from * h00 + from_tangent * h10 + to * h01 + to_tangent * h11;
  }

  CinematicFlight::RouteState
  CinematicFlight::route_state (float distance) const {
    if (m_arc_samples.empty ())
      return {};
    distance = std::clamp (distance, 0.0f, m_arc_samples.back ().distance);
    const auto right =
      std::lower_bound (m_arc_samples.begin (),
                        m_arc_samples.end (),
                        distance,
                        [] (const ArcSample& sample, float value) {
                          return sample.distance < value;
                        });
    const ArcSample* after =
      right == m_arc_samples.end () ? &m_arc_samples.back () : &*right;
    const ArcSample* before =
      right == m_arc_samples.begin () ? after : &*(right - 1);
    const float span = after->distance - before->distance;
    const float alpha =
      span > 1e-5f ? (distance - before->distance) / span : 0.0f;

    std::size_t segment = before->segment;
    float t = before->t;
    if (before->segment == after->segment)
      t += (after->t - before->t) * alpha;
    else {
      segment = after->segment;
      t = after->t * alpha;
    }
    const float lift_alpha = alpha * alpha * (3.0f - 2.0f * alpha);
    const float lift =
      before->terrain_lift +
      (after->terrain_lift - before->terrain_lift) * lift_alpha;
    const float curve_speed_limit =
      before->speed_limit + (after->speed_limit - before->speed_limit) * alpha;
    Vec3 position = curve_position (segment, t);
    position[1] += lift;

    const float eased = t * t * (3.0f - 2.0f * t);
    const CinematicFlightWaypoint& from = m_waypoints[segment];
    const CinematicFlightWaypoint& to = m_waypoints[segment + 1];
    return {
      .position = position,
      .subject = linear_vector_interpolate (from.subject, to.subject, eased),
      .cruise_speed = std::min (
        curve_speed_limit,
        from.cruise_speed + (to.cruise_speed - from.cruise_speed) * eased),
      .field_of_view =
        from.field_of_view + (to.field_of_view - from.field_of_view) * eased
    };
  }

  void CinematicFlight::build_flight_ribbon (const map::HeightMap& map) {
    m_arc_samples.clear ();
    if (m_waypoints.size () < 2)
      return;

    Vec3 previous = curve_position (0, 0.0f);
    float distance = 0.0f;
    for (std::size_t segment = 0; segment + 1 < m_waypoints.size ();
         ++segment) {
      const float chord = length (m_waypoints[segment + 1].position -
                                  m_waypoints[segment].position);
      const int steps = std::max (12, static_cast<int> (chord / 7.0f));
      const int first = segment == 0 ? 0 : 1;
      for (int step = first; step <= steps; ++step) {
        const float t = static_cast<float> (step) / steps;
        const Vec3 point = curve_position (segment, t);
        if (!m_arc_samples.empty ())
          distance += length (point - previous);
        const float ground = map.interpolated_height (point[0], point[2]);
        m_arc_samples.push_back (
          { .distance = distance,
            .segment = segment,
            .t = t,
            .terrain_lift = std::max (0.0f, ground + 22.0f - point[1]),
            .speed_limit = 360.0f });
        previous = point;
      }
    }

    // Raise the ribbon before a ridge and let it settle after the crest.
    // A Gaussian clearance envelope avoids the slope discontinuities produced
    // by a simple max-clearance cone: the aircraft begins climbing before the
    // obstruction, rounds the vertical crest, then dives away continuously.
    std::vector<float> minimum_lift;
    minimum_lift.reserve (m_arc_samples.size ());
    for (const ArcSample& sample : m_arc_samples)
      minimum_lift.push_back (sample.terrain_lift);
    constexpr float anticipation = 95.0f;
    constexpr float horizon = 300.0f;
    for (std::size_t i = 0; i < m_arc_samples.size (); ++i) {
      float lift = minimum_lift[i];
      for (std::size_t j = i; j > 0; --j) {
        const float separation =
          m_arc_samples[i].distance - m_arc_samples[j - 1].distance;
        if (separation > horizon)
          break;
        const float weight = std::exp (-0.5f * separation * separation /
                                       (anticipation * anticipation));
        lift = std::max (lift, minimum_lift[j - 1] * weight);
      }
      for (std::size_t j = i + 1; j < m_arc_samples.size (); ++j) {
        const float separation =
          m_arc_samples[j].distance - m_arc_samples[i].distance;
        if (separation > horizon)
          break;
        const float weight = std::exp (-0.5f * separation * separation /
                                       (anticipation * anticipation));
        lift = std::max (lift, minimum_lift[j] * weight);
      }
      m_arc_samples[i].terrain_lift = lift;
    }
    std::vector<float> smoothed (m_arc_samples.size ());
    for (int pass = 0; pass < 8; ++pass) {
      smoothed.front () = m_arc_samples.front ().terrain_lift;
      smoothed.back () = m_arc_samples.back ().terrain_lift;
      for (std::size_t i = 1; i + 1 < m_arc_samples.size (); ++i)
        smoothed[i] = std::max (minimum_lift[i],
                                0.25f * m_arc_samples[i - 1].terrain_lift +
                                  0.50f * m_arc_samples[i].terrain_lift +
                                  0.25f * m_arc_samples[i + 1].terrain_lift);
      for (std::size_t i = 0; i < m_arc_samples.size (); ++i)
        m_arc_samples[i].terrain_lift = smoothed[i];
    }

    // The vertical anticipation changes the true path length slightly.
    distance = 0.0f;
    previous =
      curve_position (m_arc_samples.front ().segment, m_arc_samples.front ().t);
    previous[1] += m_arc_samples.front ().terrain_lift;
    m_arc_samples.front ().distance = 0.0f;
    for (std::size_t i = 1; i < m_arc_samples.size (); ++i) {
      Vec3 point =
        curve_position (m_arc_samples[i].segment, m_arc_samples[i].t);
      point[1] += m_arc_samples[i].terrain_lift;
      distance += length (point - previous);
      m_arc_samples[i].distance = distance;
      previous = point;
    }

    // Tight curvature lowers the admissible airspeed, but this flight should
    // carve through a bend instead of tiptoeing around it. The high lateral
    // authority is closer to a bird or racing drone than a tour helicopter.
    // Propagate the limit in both directions so speed changes are anticipated.
    std::vector<Vec3> points;
    points.reserve (m_arc_samples.size ());
    for (const ArcSample& sample : m_arc_samples) {
      Vec3 point = curve_position (sample.segment, sample.t);
      point[1] += sample.terrain_lift;
      points.push_back (point);
    }
    for (std::size_t i = 1; i + 1 < m_arc_samples.size (); ++i) {
      std::size_t left = i - 1;
      while (left > 0 &&
             m_arc_samples[i].distance - m_arc_samples[left].distance < 16.0f)
        --left;
      std::size_t right = i + 1;
      while (right + 1 < m_arc_samples.size () &&
             m_arc_samples[right].distance - m_arc_samples[i].distance < 16.0f)
        ++right;
      Vec3 incoming = points[i] - points[left];
      Vec3 outgoing = points[right] - points[i];
      if (length2 (incoming) < 1e-5f || length2 (outgoing) < 1e-5f)
        continue;
      normalize (incoming);
      normalize (outgoing);
      const float angle =
        std::acos (std::clamp (dot (incoming, outgoing), -1.0f, 1.0f));
      const float tangent_separation =
        0.5f * (m_arc_samples[right].distance - m_arc_samples[left].distance);
      const float curvature = angle / std::max (1.0f, tangent_separation);
      if (curvature > 1e-5f)
        m_arc_samples[i].speed_limit =
          std::clamp (std::sqrt (24.0f / curvature), 42.0f, 360.0f);
    }
    for (std::size_t i = m_arc_samples.size () - 1; i > 0; --i) {
      const float span =
        m_arc_samples[i].distance - m_arc_samples[i - 1].distance;
      const float braking_limit =
        std::sqrt (m_arc_samples[i].speed_limit * m_arc_samples[i].speed_limit +
                   2.0f * 18.0f * span);
      m_arc_samples[i - 1].speed_limit =
        std::min (m_arc_samples[i - 1].speed_limit, braking_limit);
    }
    for (std::size_t i = 1; i < m_arc_samples.size (); ++i) {
      const float span =
        m_arc_samples[i].distance - m_arc_samples[i - 1].distance;
      const float acceleration_limit = std::sqrt (
        m_arc_samples[i - 1].speed_limit * m_arc_samples[i - 1].speed_limit +
        2.0f * 14.0f * span);
      m_arc_samples[i].speed_limit =
        std::min (m_arc_samples[i].speed_limit, acceleration_limit);
    }
  }

  void CinematicFlight::start (const CinematicFlightPlan& plan,
                               const map::HeightMap& map) {
    m_waypoints = plan.waypoints;
    for (int pass = 0; pass < 4 && m_waypoints.size () >= 2; ++pass) {
      std::vector<CinematicFlightWaypoint> rounded;
      rounded.reserve (m_waypoints.size () * 2);
      rounded.push_back (m_waypoints.front ());
      for (std::size_t i = 0; i + 1 < m_waypoints.size (); ++i) {
        const CinematicFlightWaypoint& from = m_waypoints[i];
        const CinematicFlightWaypoint& to = m_waypoints[i + 1];
        const auto blend = [&] (float t) {
          return CinematicFlightWaypoint {
            .position =
              linear_vector_interpolate (from.position, to.position, t),
            .subject = linear_vector_interpolate (from.subject, to.subject, t),
            .cruise_speed =
              from.cruise_speed + (to.cruise_speed - from.cruise_speed) * t,
            .field_of_view =
              from.field_of_view + (to.field_of_view - from.field_of_view) * t
          };
        };
        rounded.push_back (blend (0.25f));
        rounded.push_back (blend (0.75f));
      }
      rounded.push_back (m_waypoints.back ());
      m_waypoints = std::move (rounded);
    }
    build_flight_ribbon (map);
    m_elapsed = 0.0f;
    m_final_hold = 0.0f;
    m_route_distance = 0.0f;
    m_manual_offset = Vec3 ();
    m_manual_velocity = Vec3 ();
    m_bank = 0.0f;
    m_look_direction = Vec3 (0, 0, 1);
    m_longitudinal_acceleration = 0.0f;
    m_active = m_arc_samples.size () >= 2;
    if (!m_active)
      return;

    const RouteState initial = route_state (0.0f);
    const RouteState ahead =
      route_state (std::min (100.0f, m_arc_samples.back ().distance));
    m_position = initial.position;
    m_speed = initial.cruise_speed;
    Vec3 direction = ahead.position - initial.position;
    if (length2 (direction) < 1e-5f)
      direction = Vec3 (0, 0, 1);
    else
      normalize (direction);
    m_velocity = direction * m_speed;
    m_look_direction = direction;
    m_field_of_view = initial.field_of_view;
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

    const float route_end = m_arc_samples.back ().distance;
    const float remaining = route_end - m_route_distance;
    const RouteState before = route_state (m_route_distance);
    const float pace = std::clamp (controls.pace, -1.0f, 1.0f);
    float wanted_speed = before.cruise_speed * (1.0f + 0.30f * pace);
    const float stopping_speed =
      std::sqrt (std::max (0.0f, 2.0f * 5.0f * remaining));
    wanted_speed = std::min (wanted_speed, stopping_speed);

    const float wanted_acceleration =
      std::clamp ((wanted_speed - m_speed) * 1.8f, -18.0f, 14.0f);
    const float acceleration_step =
      std::clamp (wanted_acceleration - m_longitudinal_acceleration,
                  -32.0f * dt,
                  32.0f * dt);
    m_longitudinal_acceleration += acceleration_step;
    m_speed = std::max (0.0f, m_speed + m_longitudinal_acceleration * dt);
    m_route_distance = std::min (route_end, m_route_distance + m_speed * dt);

    const RouteState current = route_state (m_route_distance);
    const float tangent_span = std::clamp (m_speed * 0.16f, 12.0f, 32.0f);
    const RouteState behind = route_state (m_route_distance - tangent_span);
    const RouteState ahead = route_state (m_route_distance + tangent_span);
    Vec3 path_forward = ahead.position - behind.position;
    if (length2 (path_forward) < 1e-5f)
      path_forward = m_velocity;
    if (length2 (path_forward) < 1e-5f)
      path_forward = Vec3 (0, 0, 1);
    normalize (path_forward);

    Vec3 right = cross (path_forward, Vec3 (0, 1, 0));
    if (length2 (right) < 1e-5f)
      right = Vec3 (1, 0, 0);
    else
      normalize (right);
    const Vec3 wanted_offset =
      right * (controls.lateral * 70.0f) + Vec3 (0, controls.lift * 55.0f, 0);
    Vec3 wanted_manual_velocity = (wanted_offset - m_manual_offset) * 0.75f;
    clamp_length (wanted_manual_velocity, 22.0f);
    Vec3 manual_velocity_step = wanted_manual_velocity - m_manual_velocity;
    clamp_length (manual_velocity_step, 28.0f * dt);
    m_manual_velocity += manual_velocity_step;
    m_manual_offset += m_manual_velocity * dt;

    const Vec3 previous_position = m_position;
    m_position = current.position + m_manual_offset;
    const float floor =
      map.interpolated_height (m_position[0], m_position[2]) + 18.0f;
    m_position[1] = std::max (m_position[1], floor);
    m_velocity = (m_position - previous_position) / dt;

    // Aim through a long corridor of future flight instead of continually
    // correcting toward the nearest landmark. A stabilized drone gimbal sees
    // one committed swoop; the terrain feature still shaped the route itself.
    const float look_near = std::clamp (m_speed * 0.28f, 35.0f, 90.0f);
    const float look_far = std::clamp (m_speed * 1.45f, 220.0f, 430.0f);
    const RouteState view_near = route_state (m_route_distance + look_near);
    const RouteState view_far = route_state (m_route_distance + look_far);
    Vec3 wanted_look = view_far.position - view_near.position;
    if (length2 (wanted_look) < 1e-5f)
      wanted_look = path_forward;
    else
      normalize (wanted_look);
    Vec3 framed_subject = current.subject - current.position;
    if (length2 (framed_subject) > 1e-5f) {
      normalize (framed_subject);
      wanted_look = wanted_look * 0.48f + framed_subject * 0.52f;
      if (length2 (wanted_look) > 1e-5f)
        normalize (wanted_look);
    }
    const float gimbal_alpha = 1.0f - std::exp (-1.9f * dt);
    m_look_direction += (wanted_look - m_look_direction) * gimbal_alpha;
    if (length2 (m_look_direction) > 1e-5f)
      normalize (m_look_direction);

    const float speed_fov =
      std::clamp ((m_speed - 70.0f) * 0.055f, 0.0f, 10.0f);
    const float dive_fov = std::clamp (-path_forward[1] * 9.0f, 0.0f, 5.0f);
    const float wanted_fov = current.field_of_view + speed_fov + dive_fov;
    m_field_of_view +=
      (wanted_fov - m_field_of_view) * (1.0f - std::exp (-3.0f * dt));

    // Roll from the broad upcoming arc, never from frame-local acceleration.
    // This keeps a gently banked horizon through a whole turn and eliminates
    // the left-right rocking caused by tiny corrections in the flight line.
    Vec3 arc_now = view_near.position - current.position;
    Vec3 arc_future = view_far.position - view_near.position;
    arc_now[1] = 0.0f;
    arc_future[1] = 0.0f;
    float turn = 0.0f;
    if (length2 (arc_now) > 1e-5f && length2 (arc_future) > 1e-5f) {
      normalize (arc_now);
      normalize (arc_future);
      turn =
        std::atan2 (cross (arc_now, arc_future)[1], dot (arc_now, arc_future));
    }
    const float wanted_bank = std::clamp (-turn * 0.48f, -0.30f, 0.30f);
    m_bank += (wanted_bank - m_bank) * (1.0f - std::exp (-0.85f * dt));

    if (m_route_distance >= route_end - 0.01f) {
      m_speed = 0.0f;
      m_final_hold += dt;
      if (m_final_hold >= 1.1f)
        m_active = false;
    } else
      m_final_hold = 0.0f;
  }
  Vec3 CinematicFlight::forward () const {
    Vec3 result = m_look_direction;
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
