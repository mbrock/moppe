#include <moppe/game/cinematic_flight.hh>

#include <moppe/gfx/signal.hh>
#include <moppe/profile.hh>
#include <moppe/terrain/torus.hh>

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <iostream>
#include <limits>
#include <utility>

namespace moppe::game {
  namespace {
    constexpr terrain::CellIndex no_cell = terrain::no_cell;

    // A score ranks candidates of one kind against each other only, each
    // kind by its own measure (metres, square metres, ...), so it stays a
    // plain number.
    struct FeatureCandidate {
      terrain::CellIndex cell = no_cell;
      float score = -std::numeric_limits<float>::infinity ();
      direction_t direction = unit_direction (Vec3 (0, 0, 1));
    };

    // A displacement as what moves a position_t (docs/typed-quantities.md).
    auto flight_shift (const displacement_t& by) {
      return quantity_cast<isq::position_vector> (by);
    }

    meters_t flight_length (const displacement_t& d) {
      return length (displacement_value (d)) * u::m;
    }

    meters_t flight_height_of (const position_t& at) {
      return position_value (at)[1] * u::m;
    }

    void flight_set_height (position_t& at, meters_t y) {
      position_value (at)[1] = meters_value (y);
    }

    void flight_raise (position_t& at, meters_t by) {
      position_value (at)[1] += meters_value (by);
    }

    // The ground's elevation under `at`, as a height in metres.
    meters_t flight_ground (const map::SurfaceGeometry& surface,
                            const plan_position_t& at) {
      return terrain::surface_elevation_value (
               spatial::sample<terrain::surface_elevation> (
                 surface, at_height (at, 0.0f * u::m))) *
             u::m;
    }

    // `from` eased toward `to` by `weight`.
    template <typename Q>
    Q flight_ease (const Q& from, const Q& to, blend_weight_t weight) {
      return from + (to - from) * weight.numerical_value_in (one);
    }

    // The smoothstep of `weight`: eased in and out.
    blend_weight_t flight_smoothstep (blend_weight_t weight) {
      const magnitude_t x = weight.numerical_value_in (one);
      return (x * x * (3.0f - 2.0f * x)).numerical_value_in (one) *
             blend_weight[one];
    }

    void clamp_length (velocity_t& value, speed_t maximum) {
      const speed_t magnitude = length (velocity_value (value)) * u::m / u::s;
      if (magnitude > maximum && magnitude > 1e-5f * u::m / u::s)
        velocity_value (value) *=
          (maximum / magnitude).numerical_value_in (one);
    }

    meters_t unwrap_lane (meters_t at, meters_t reference, meters_t size) {
      while (at - reference > size * 0.5f)
        at -= size;
      while (at - reference < -size * 0.5f)
        at += size;
      return at;
    }

    plan_position_t unwrap_near (const plan_position_t& point,
                                 const plan_position_t& reference,
                                 const map::SurfaceGeometry& surface) {
      return plan_position (
        unwrap_lane (
          x_of (point), x_of (reference), surface.domain ().period_x ()),
        unwrap_lane (
          z_of (point), z_of (reference), surface.domain ().period_z ()));
    }

    position_t unwrap_near (const position_t& point,
                            const position_t& reference,
                            const map::SurfaceGeometry& surface) {
      return at_height (unwrap_near (plan (point), plan (reference), surface),
                        flight_height_of (point));
    }

    // Which way `d` goes seen from above, or `fallback` where it hardly
    // goes anywhere.
    direction_t level_direction (const displacement_t& d,
                                 const direction_t& fallback) {
      Vec3 level = displacement_value (d);
      level[1] = 0.0f;
      if (length2 (level) < 1e-5f)
        return fallback;
      return unit_direction (normalized (level));
    }

    // A quarter turn round from `way`, level.
    direction_t beside (const direction_t& way) {
      return unit_direction (
        Vec3 (-direction_value (way)[2], 0, direction_value (way)[0]));
    }

    // The level right of `look`, or east when it looks straight up or down.
    direction_t right_of (const direction_t& look) {
      const direction_t right =
        unit_direction (cross (direction_value (look), Vec3 (0, 1, 0)));
      if (length2 (direction_value (right)) < 1e-5f)
        return unit_direction (Vec3 (1, 0, 0));
      return unit_direction (normalized (direction_value (right)));
    }

    terrain::SurfaceElevation
    flight_height_sample (const map::SurfaceGeometry& surface, int x, int z) {
      x =
        terrain::wrap_index (x, static_cast<int> (surface.domain ().width ()));
      z =
        terrain::wrap_index (z, static_cast<int> (surface.domain ().height ()));
      return spatial::get<terrain::surface_elevation> (
        surface[terrain::TerrainIndex { static_cast<std::size_t> (x),
                                        static_cast<std::size_t> (z) }]);
    }

    position_t flight_cell_position (terrain::CellIndex cell,
                                     const map::SurfaceGeometry& surface,
                                     const terrain::FloodField& flood,
                                     const terrain::LakeCensus& census,
                                     const terrain::DrainageGraph& drainage) {
      const std::size_t width = drainage.width ();
      const int x = static_cast<int> (cell % width);
      const int z = static_cast<int> (cell / width);
      const meters_t spacing_x = surface.domain ().spacing_x ();
      const meters_t spacing_z = surface.domain ().spacing_z ();
      const bool wet = census.body_at (terrain::CellIndex { cell }) !=
                         terrain::LakeCensus::dry ||
                       flood.ocean[cell];
      const terrain::SurfaceElevation y =
        wet
          ? terrain::surface_elevation_point (flood.water_level_m (cell) * u::m)
          : spatial::get<terrain::surface_elevation> (
              surface[terrain::TerrainIndex { static_cast<std::size_t> (x),
                                              static_cast<std::size_t> (z) }]);
      return position (Vec3 (meters_value (x * spacing_x),
                             terrain::surface_elevation_value (y),
                             meters_value (z * spacing_z)));
    }

    direction_t flight_cell_direction (terrain::CellIndex from,
                                       terrain::CellIndex to,
                                       const map::SurfaceGeometry& surface,
                                       const terrain::DrainageGraph& drainage) {
      const int width = static_cast<int> (drainage.width ());
      const int height = static_cast<int> (drainage.height ());
      const int from_x = static_cast<int> (from % drainage.width ());
      const int from_z = static_cast<int> (from / drainage.width ());
      const int to_x = static_cast<int> (to % drainage.width ());
      const int to_z = static_cast<int> (to / drainage.width ());
      int dx = to_x - from_x;
      int dz = to_z - from_z;
      dx = terrain::minimum_image_cells (dx, width);
      dz = terrain::minimum_image_cells (dz, height);
      const displacement_t step = displacement (
        Vec3 (meters_value (dx * surface.domain ().spacing_x ()),
              0,
              meters_value (dz * surface.domain ().spacing_z ())));
      if (length2 (displacement_value (step)) < 1e-5f)
        return unit_direction (Vec3 (0, 0, 1));
      return direction_of (step);
    }

    FeatureCandidate choose_valley (const terrain::DrainageGraph& drainage,
                                    const terrain::RiverNetwork& rivers) {
      FeatureCandidate best;
      for (const terrain::RiverReach& reach : rivers.reaches) {
        if (reach.cells.size () < 4)
          continue;
        const square_meters_t area =
          std::max (1.0f * u::m * u::m, reach.downstream_area);
        const float score = std::sqrt (area.numerical_value_in (u::m * u::m)) *
                            reach.cells.size ();
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
          (fall.drop).numerical_value_in (moppe::u::m) *
          std::sqrt (
            std::max (1.0f,
                      (fall.contributing_area)
                        .numerical_value_in (moppe::u::m * moppe::u::m)));
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
      for (const terrain::WaterBody& candidate : census.water_bodies ()) {
        if (candidate.classification == terrain::WaterBodyClass::Sea)
          continue;
        if (!body || candidate.area > body->area)
          body = &candidate;
      }
      FeatureCandidate best;
      if (!body)
        return best;
      best.score = (body->area).numerical_value_in (moppe::u::m * moppe::u::m);
      meters_t deepest = -1.0f * u::m;
      for (std::uint32_t cell = 0; cell < census.cell_count (); ++cell)
        if (census.body_at (terrain::CellIndex { cell }) == body->id &&
            flood.water_depth_m (cell) * u::m > deepest) {
          deepest = flood.water_depth_m (cell) * u::m;
          best.cell = terrain::CellIndex (cell);
        }
      return best;
    }

    FeatureCandidate choose_peak (const map::SurfaceGeometry& surface) {
      FeatureCandidate best;
      const int width = static_cast<int> (surface.domain ().width ());
      const int height = static_cast<int> (surface.domain ().height ());
      const int stride = std::max (2, width / 96);
      const int radius = std::max (3, width / 64);
      for (int z = 0; z < height; z += stride)
        for (int x = 0; x < width; x += stride) {
          const terrain::SurfaceElevation center =
            flight_height_sample (surface, x, z);
          terrain::SurfaceElevation ring_high =
            terrain::surface_elevation_point (
              -std::numeric_limits<float>::infinity () * u::m);
          terrain::SurfaceElevation ring_low =
            terrain::surface_elevation_point (
              std::numeric_limits<float>::infinity () * u::m);
          for (int dz : { -radius, 0, radius })
            for (int dx : { -radius, 0, radius }) {
              if (dx == 0 && dz == 0)
                continue;
              const terrain::SurfaceElevation sample =
                flight_height_sample (surface, x + dx, z + dz);
              ring_high = std::max (ring_high, sample);
              ring_low = std::min (ring_low, sample);
            }
          if (center < ring_high)
            continue;
          const terrain::ElevationChange prominence = center - ring_low;
          const float score = terrain::surface_elevation_value (center) +
                              1.8f * prominence.numerical_value_in (u::m);
          if (score > best.score) {
            best.cell = static_cast<terrain::CellIndex> (z * width + x);
            best.score = score;
          }
        }
      return best;
    }

    FeatureCandidate choose_saddle (const map::SurfaceGeometry& surface) {
      FeatureCandidate best;
      const int width = static_cast<int> (surface.domain ().width ());
      const int height = static_cast<int> (surface.domain ().height ());
      const int stride = std::max (2, width / 96);
      const int radius = std::max (3, width / 80);
      for (int z = 0; z < height; z += stride)
        for (int x = 0; x < width; x += stride) {
          const terrain::SurfaceElevation c =
            flight_height_sample (surface, x, z);
          const terrain::SurfaceElevation n =
            flight_height_sample (surface, x, z - radius);
          const terrain::SurfaceElevation s =
            flight_height_sample (surface, x, z + radius);
          const terrain::SurfaceElevation e =
            flight_height_sample (surface, x + radius, z);
          const terrain::SurfaceElevation w =
            flight_height_sample (surface, x - radius, z);
          const terrain::ElevationChange across_x =
            std::min (n - c, s - c) + std::min (c - e, c - w);
          const terrain::ElevationChange across_z =
            std::min (e - c, w - c) + std::min (c - n, c - s);
          const terrain::ElevationChange shape = std::max (across_x, across_z);
          if (shape <= terrain::ElevationChange::zero ())
            continue;
          const float score = shape.numerical_value_in (u::m) +
                              0.12f * terrain::surface_elevation_value (c);
          if (score > best.score) {
            best.cell = static_cast<terrain::CellIndex> (z * width + x);
            best.score = score;
            best.direction = unit_direction (
              across_x > across_z ? Vec3 (1, 0, 0) : Vec3 (0, 0, 1));
          }
        }
      return best;
    }

    void add_waypoint (CinematicFlightPlan& plan,
                       const map::SurfaceGeometry& surface,
                       position_t position,
                       const position_t& subject,
                       meters_t clearance,
                       speed_t speed,
                       degrees_t field_of_view) {
      // This is an eagle flight, not a sightseeing helicopter. The authored
      // values describe the shape of each beat; the global scale keeps every
      // beat carrying decisive forward energy.
      constexpr scale_factor_t flight_speed_scale = 1.55f * scale_factor[one];
      flight_set_height (
        position,
        std::max (flight_height_of (position),
                  flight_ground (surface, moppe::plan (position)) + clearance));
      plan.waypoints.push_back (
        { .position = position,
          .subject = subject,
          .cruise_speed = speed * flight_speed_scale.numerical_value_in (one),
          .field_of_view = field_of_view });
    }

    void add_transit (CinematicFlightPlan& plan,
                      const map::SurfaceGeometry& surface,
                      position_t destination,
                      const position_t& subject) {
      if (plan.waypoints.empty ())
        return;
      const position_t start = plan.waypoints.back ().position;
      destination = unwrap_near (destination, start, surface);
      const displacement_t delta = destination - start;
      const meters_t horizontal = std::hypot (displacement_value (delta)[0],
                                              displacement_value (delta)[2]) *
                                  u::m;
      const int steps =
        std::max (1,
                  static_cast<int> (
                    (horizontal / (650.0f * u::m)).numerical_value_in (one)));
      for (int i = 1; i < steps; ++i) {
        const proportion_t t = static_cast<float> (i) / steps * one;
        position_t point = start + flight_shift (displacement_t (delta * t));
        flight_set_height (
          point, flight_ground (surface, moppe::plan (point)) + 150.0f * u::m);
        add_waypoint (plan,
                      surface,
                      point,
                      subject,
                      130.0f * u::m,
                      230.0f * u::m / u::s,
                      58.0f * u::deg);
      }
    }

    position_t trail_alignment_position (const plan_position_t& at,
                                         const map::SurfaceGeometry& surface) {
      return at_height (at, flight_ground (surface, at));
    }

    void add_trail_reveal (CinematicFlightPlan& plan,
                           const map::SurfaceGeometry& surface,
                           const terrain::TrailNetwork& trail) {
      const auto& alignment = trail.alignment.points;
      if (alignment.size () < 8 || trail.plan.home_base == no_cell)
        return;

      // One quick oblique sweep establishes the circuit as a place before the
      // broader landscape tour begins. Coarse gates reveal its large shape
      // without making the drone consume the entire loop.
      constexpr std::size_t samples = 3;
      const std::size_t look_ahead =
        std::max<std::size_t> (1, alignment.size () / 8);
      position_t previous_ground =
        trail_alignment_position (alignment.front (), surface);
      for (std::size_t sample = 0; sample < samples; ++sample) {
        const std::size_t index = sample * alignment.size () / 4;
        const std::size_t ahead_index =
          (index + look_ahead) % alignment.size ();
        position_t ground =
          trail_alignment_position (alignment[index], surface);
        ground = unwrap_near (ground, previous_ground, surface);
        position_t subject =
          trail_alignment_position (alignment[ahead_index], surface);
        subject = unwrap_near (subject, ground, surface);
        const direction_t forward =
          level_direction (subject - ground, unit_direction (Vec3 (0, 0, 1)));
        const direction_t side (unit_direction (Vec3 (
          -direction_value (forward)[2], 0, direction_value (forward)[0])));
        const bool establishing = sample == 0;
        position_t eye =
          ground -
          flight_shift (
            along (forward, (establishing ? 120.0f : 48.0f) * u::m)) +
          flight_shift (along (side, (establishing ? 105.0f : 42.0f) * u::m));
        flight_set_height (eye,
                           flight_height_of (ground) +
                             (establishing ? 150.0f : 76.0f) * u::m);
        flight_set_height (subject,
                           flight_ground (surface, moppe::plan (subject)) +
                             3.0f * u::m);
        add_waypoint (plan,
                      surface,
                      eye,
                      subject,
                      (establishing ? 105.0f : 55.0f) * u::m,
                      (establishing ? 215.0f : 245.0f) * u::m / u::s,
                      (establishing ? 64.0f : 68.0f) * u::deg);
        previous_ground = ground;
      }

      const position_t home =
        trail_alignment_position (alignment.front (), surface);
      plan.landmarks.push_back (
        { .kind = CinematicLandmarkKind::Trail,
          .cell = trail.plan.home_base,
          .score = static_cast<float> (alignment.size ()),
          .position = home });
    }

    void record_landmark (CinematicFlightPlan& plan,
                          CinematicLandmarkKind kind,
                          const FeatureCandidate& feature,
                          const position_t& position) {
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
  plan_cinematic_flight (const map::SurfaceGeometry& surface,
                         const terrain::FloodField& flood,
                         const terrain::LakeCensus& census,
                         const terrain::DrainageGraph& drainage,
                         const terrain::RiverNetwork& rivers,
                         const position_t& arrival,
                         const terrain::TrailNetwork* trail) {
    MOPPE_PROFILE_ZONE ("plan_cinematic_flight");
    CinematicFlightPlan plan;
    if (drainage.receiver.empty ())
      return plan;

    const FeatureCandidate valley = choose_valley (drainage, rivers);
    const FeatureCandidate waterfall = choose_flight_waterfall (rivers);
    const FeatureCandidate lake = choose_flight_lake (flood, census);
    const FeatureCandidate saddle = choose_saddle (surface);
    const FeatureCandidate peak = choose_peak (surface);
    const FeatureCandidate water = waterfall.cell != no_cell ? waterfall : lake;

    if (trail)
      add_trail_reveal (plan, surface, *trail);

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
        position_t previous = flight_cell_position (
          reach->cells.front (), surface, flood, census, drainage);
        const std::size_t samples = std::min<std::size_t> (6, last_index + 1);
        for (std::size_t i = 0; i < samples; ++i) {
          const std::size_t index =
            i * last_index / std::max<std::size_t> (1, samples - 1);
          const terrain::CellIndex cell = reach->cells[index];
          position_t ground =
            flight_cell_position (cell, surface, flood, census, drainage);
          ground = unwrap_near (ground, previous, surface);
          const terrain::CellIndex next = drainage.receiver[cell];
          const direction_t flow =
            flight_cell_direction (cell, next, surface, drainage);
          const direction_t side = beside (flow);
          position_t eye =
            ground -
            flight_shift (along (flow, (i == 0 ? 95.0f : 28.0f) * u::m)) +
            flight_shift (along (side, 8.0f * u::m));
          flight_set_height (
            eye, flight_height_of (ground) + (i == 0 ? 52.0f : 22.0f) * u::m);
          position_t subject =
            ground + flight_shift (along (flow, 85.0f * u::m));
          flight_set_height (subject,
                             flight_ground (surface, moppe::plan (subject)) +
                               3.0f * u::m);
          add_waypoint (plan,
                        surface,
                        eye,
                        subject,
                        18.0f * u::m,
                        118.0f * u::m / u::s,
                        69.0f * u::deg);
          previous = ground;
        }
        const position_t position =
          flight_cell_position (valley.cell, surface, flood, census, drainage);
        record_landmark (plan, CinematicLandmarkKind::Valley, valley, position);
      }
    }

    if (water.cell != no_cell) {
      position_t subject =
        flight_cell_position (water.cell, surface, flood, census, drainage);
      if (!plan.waypoints.empty ())
        subject =
          unwrap_near (subject, plan.waypoints.back ().position, surface);
      const direction_t flow = flight_cell_direction (
        water.cell, drainage.receiver[water.cell], surface, drainage);
      const direction_t side = beside (flow);
      position_t approach = subject -
                            flight_shift (along (flow, 190.0f * u::m)) +
                            flight_shift (along (side, 75.0f * u::m));
      flight_set_height (approach,
                         flight_height_of (subject) +
                           (waterfall.cell != no_cell ? 100.0f : 125.0f) *
                             u::m);
      add_transit (plan, surface, approach, subject);
      add_waypoint (plan,
                    surface,
                    approach,
                    subject,
                    65.0f * u::m,
                    165.0f * u::m / u::s,
                    61.0f * u::deg);
      position_t close = subject - flight_shift (along (flow, 35.0f * u::m)) +
                         flight_shift (along (side, 28.0f * u::m));
      flight_set_height (close,
                         flight_height_of (subject) +
                           (waterfall.cell != no_cell ? 32.0f : 58.0f) * u::m);
      add_waypoint (plan,
                    surface,
                    close,
                    subject,
                    24.0f * u::m,
                    110.0f * u::m / u::s,
                    55.0f * u::deg);
      position_t exit = subject + flight_shift (along (flow, 170.0f * u::m)) -
                        flight_shift (along (side, 55.0f * u::m));
      flight_set_height (exit, flight_height_of (subject) + 85.0f * u::m);
      add_waypoint (plan,
                    surface,
                    exit,
                    subject,
                    55.0f * u::m,
                    175.0f * u::m / u::s,
                    61.0f * u::deg);
      record_landmark (plan,
                       waterfall.cell != no_cell
                         ? CinematicLandmarkKind::Waterfall
                         : CinematicLandmarkKind::Lake,
                       water,
                       subject);
    }

    if (saddle.cell != no_cell) {
      position_t subject =
        flight_cell_position (saddle.cell, surface, flood, census, drainage);
      if (!plan.waypoints.empty ())
        subject =
          unwrap_near (subject, plan.waypoints.back ().position, surface);
      position_t entry =
        subject - flight_shift (along (saddle.direction, 260.0f * u::m));
      flight_set_height (entry, flight_height_of (subject) + 90.0f * u::m);
      add_transit (plan, surface, entry, subject);
      add_waypoint (plan,
                    surface,
                    entry,
                    subject,
                    60.0f * u::m,
                    180.0f * u::m / u::s,
                    63.0f * u::deg);
      position_t pass =
        subject - flight_shift (along (saddle.direction, 20.0f * u::m));
      flight_set_height (pass, flight_height_of (subject) + 28.0f * u::m);
      position_t beyond =
        subject + flight_shift (along (saddle.direction, 150.0f * u::m));
      flight_set_height (
        beyond, flight_ground (surface, moppe::plan (beyond)) + 12.0f * u::m);
      add_waypoint (plan,
                    surface,
                    pass,
                    beyond,
                    20.0f * u::m,
                    145.0f * u::m / u::s,
                    72.0f * u::deg);
      position_t exit =
        subject + flight_shift (along (saddle.direction, 240.0f * u::m));
      flight_set_height (exit, flight_height_of (subject) + 70.0f * u::m);
      add_waypoint (plan,
                    surface,
                    exit,
                    beyond,
                    45.0f * u::m,
                    190.0f * u::m / u::s,
                    65.0f * u::deg);
      record_landmark (plan, CinematicLandmarkKind::Saddle, saddle, subject);
    }

    if (peak.cell != no_cell) {
      position_t subject =
        flight_cell_position (peak.cell, surface, flood, census, drainage);
      if (!plan.waypoints.empty ())
        subject =
          unwrap_near (subject, plan.waypoints.back ().position, surface);
      const meters_t radius = std::clamp (
        surface.domain ().period_x () * 0.055f, 160.0f * u::m, 330.0f * u::m);
      const direction_t east = unit_direction (Vec3 (1, 0, 0));
      direction_t incoming = east;
      if (!plan.waypoints.empty ())
        incoming =
          level_direction (subject - plan.waypoints.back ().position, east);
      const position_t unwrapped_arrival =
        unwrap_near (arrival, subject, surface);
      const direction_t outgoing =
        level_direction (unwrapped_arrival - subject, incoming);
      const radians_t signed_turn =
        std::atan2 (
          direction_value (incoming)[0] * direction_value (outgoing)[2] -
            direction_value (incoming)[2] * direction_value (outgoing)[0],
          dot (direction_value (incoming), direction_value (outgoing))) *
        u::rad;
      const bool turning_right = signed_turn < 0.0f * u::rad;
      const radians_t orbit_angle =
        std::clamp (abs (signed_turn), 0.65f * u::rad, 2.2f * u::rad);
      const direction_t radial = unit_direction (
        Vec3 (
          direction_value (incoming)[2], 0, -direction_value (incoming)[0]) *
        (turning_right ? -1.0f : 1.0f));
      position_t first = subject + flight_shift (along (radial, radius));
      flight_raise (first, radius * 0.36f);
      add_transit (plan, surface, first, subject);
      for (int i = 0; i < 5; ++i) {
        const proportion_t progress = static_cast<float> (i) / 4.0f * one;
        const radians_t angle = radians_t (orbit_angle * progress);
        position_t eye =
          subject +
          flight_shift (
            along (radial, std::cos (radians_value (angle)) * radius)) +
          flight_shift (
            along (incoming, std::sin (radians_value (angle)) * radius));
        flight_raise (
          eye,
          meters_t (radius * (0.36f * proportion[one] + 0.12f * progress)));
        const position_t look =
          subject + flight_shift (displacement (
                      Vec3 (0, meters_value (radius * 0.08f), 0)));
        add_waypoint (plan,
                      surface,
                      eye,
                      look,
                      70.0f * u::m,
                      125.0f * u::m / u::s,
                      53.0f * u::deg);
      }
      record_landmark (plan, CinematicLandmarkKind::Peak, peak, subject);
    }

    position_t final_subject = arrival;
    if (!plan.waypoints.empty ())
      final_subject =
        unwrap_near (final_subject, plan.waypoints.back ().position, surface);
    const position_t reveal =
      final_subject +
      flight_shift (displacement (Vec3 (-150.0f, 95.0f, -150.0f)));
    add_transit (plan, surface, reveal, final_subject);
    add_waypoint (plan,
                  surface,
                  reveal,
                  final_subject,
                  70.0f * u::m,
                  170.0f * u::m / u::s,
                  60.0f * u::deg);
    const position_t final_eye =
      final_subject +
      flight_shift (displacement (Vec3 (-28.0f, 15.0f, -28.0f)));
    add_waypoint (plan,
                  surface,
                  final_eye,
                  final_subject,
                  9.0f * u::m,
                  75.0f * u::m / u::s,
                  68.0f * u::deg);
    plan.landmarks.push_back ({ .kind = CinematicLandmarkKind::Arrival,
                                .cell = no_cell,
                                .score = 0.0f,
                                .position = arrival });
    return plan;
  }

  std::vector<const terrain::RiverReach*>
  greatest_river_course (const terrain::LakeCensus& census,
                         const terrain::RiverNetwork& rivers) {
    using terrain::RiverReach;
    // The greatest river to reach the sea, and from its mouth upward the
    // larger branch at every confluence and the larger inflow of every
    // lake, to the reach where it first shows.
    const auto reaches_sea = [&] (const RiverReach& reach) {
      return reach.downstream_ocean ||
             (census.domain ().contains (reach.downstream_body) &&
              census.water_body (reach.downstream_body).classification ==
                terrain::WaterBodyClass::Sea);
    };
    std::vector<const RiverReach*> course;
    const RiverReach* mouth = nullptr;
    for (const RiverReach& reach : rivers.reaches)
      if (reach.downstream_reach == RiverReach::no_id && reaches_sea (reach) &&
          reach.alignment.points.size () >= 2 &&
          (!mouth || reach.downstream_area > mouth->downstream_area))
        mouth = &reach;
    if (!mouth)
      return course;
    course.push_back (mouth);
    std::vector<std::uint8_t> taken (rivers.reaches.size (), 0);
    taken[mouth->id] = 1;
    for (;;) {
      const RiverReach& lowest = *course.back ();
      const RiverReach* above = nullptr;
      for (const RiverReach& reach : rivers.reaches) {
        if (taken[reach.id] || reach.alignment.points.size () < 2)
          continue;
        const bool joins = reach.downstream_reach == lowest.id;
        const bool feeds = reach.downstream_reach == RiverReach::no_id &&
                           !reaches_sea (reach) &&
                           census.domain ().contains (lowest.upstream_body) &&
                           reach.downstream_body == lowest.upstream_body;
        if ((joins || feeds) &&
            (!above || reach.downstream_area > above->downstream_area))
          above = &reach;
      }
      if (!above)
        break;
      taken[above->id] = 1;
      course.push_back (above);
    }
    return course;
  }

  CinematicFlightPlan plan_river_flight (const map::SurfaceGeometry& surface,
                                         std::span<const RiverFlightTree> trees,
                                         const terrain::WaterSheets& water,
                                         const terrain::LakeCensus& census,
                                         const terrain::DrainageGraph& drainage,
                                         const terrain::RiverNetwork& rivers,
                                         seconds_t duration) {
    MOPPE_PROFILE_ZONE ("plan_river_flight");
    using terrain::RiverReach;
    CinematicFlightPlan plan;

    const std::vector<const RiverReach*> course =
      greatest_river_course (census, rivers);
    if (course.empty ())
      return plan;

    // Above that, the dry valley: from cell to cell by whichever sends the
    // most water down, up to the divide.
    const int width = static_cast<int> (drainage.width ());
    const int height = static_cast<int> (drainage.height ());
    const meters_t spacing_x = surface.domain ().spacing_x ();
    const meters_t spacing_z = surface.domain ().spacing_z ();
    std::vector<plan_position_t> line;
    if (!course.back ()->cells.empty ()) {
      std::vector<terrain::CellIndex> ascent;
      terrain::CellIndex cell = course.back ()->cells.front ();
      for (int step = 0; step < 4096; ++step) {
        terrain::CellIndex donor = no_cell;
        square_meters_t most = 0.0f * u::m * u::m;
        for (int dz = -1; dz <= 1; ++dz)
          for (int dx = -1; dx <= 1; ++dx) {
            const terrain::CellIndex next = static_cast<std::uint32_t> (
              terrain::wrap_index (static_cast<int> (cell / width) + dz,
                                   height) *
                width +
              terrain::wrap_index (static_cast<int> (cell % width) + dx,
                                   width));
            if (next == cell || drainage.receiver[next] != cell)
              continue;
            const square_meters_t area = drainage.contributing_area_at (next);
            if (area > most) {
              most = area;
              donor = next;
            }
          }
        if (donor == no_cell)
          break;
        ascent.push_back (donor);
        cell = donor;
      }
      for (auto from = ascent.rbegin (); from != ascent.rend (); ++from) {
        const plan_position_t point =
          plan_position (static_cast<float> (*from % width) * spacing_x,
                         static_cast<float> (*from / width) * spacing_z);
        line.push_back (
          line.empty () ? point : unwrap_near (point, line.back (), surface));
      }
    }
    for (auto reach = course.rbegin (); reach != course.rend (); ++reach)
      for (const terrain::RiverAlignmentPoint& point :
           (*reach)->alignment.points) {
        const plan_position_t at = point.place;
        line.push_back (
          line.empty () ? at : unwrap_near (at, line.back (), surface));
      }
    if (line.size () < 2)
      return plan;
    // And a little way out to sea.
    {
      plan_displacement_t seaward = line.back () - line[line.size () - 2];
      for (std::size_t back = 3; back <= 40 && back <= line.size (); ++back)
        if (length2 (plan_value (seaward)) < 1.0f)
          seaward = line.back () - line[line.size () - back];
      if (length2 (plan_value (seaward)) > 1e-6f)
        line.push_back (line.back () +
                        along (direction_of (seaward), 160.0f * u::m));
    }

    // The flight's gates, a few seconds apart, their line eased of the
    // lattice's corners and the stream's wriggles.
    constexpr meters_t gate_spacing = 30.0f * u::m;
    std::vector<plan_position_t> gates { line.front () };
    {
      meters_t carried = 0.0f * u::m;
      for (std::size_t point = 1; point < line.size (); ++point) {
        plan_position_t from = line[point - 1];
        const plan_position_t to = line[point];
        meters_t span = length (to - from);
        while (carried + span >= gate_spacing) {
          const meters_t step = gate_spacing - carried;
          from = from + (to - from) * (step / span).numerical_value_in (one);
          span -= step;
          carried = 0.0f * u::m;
          gates.push_back (from);
        }
        carried += span;
      }
      gates.push_back (line.back ());
    }
    for (int pass = 0; pass < 3; ++pass) {
      std::vector<plan_position_t> eased = gates;
      for (std::size_t gate = 1; gate + 1 < gates.size (); ++gate)
        eased[gate] = plan_position (plan_value (gates[gate - 1]) * 0.25f +
                                     plan_value (gates[gate]) * 0.5f +
                                     plan_value (gates[gate + 1]) * 0.25f);
      gates = std::move (eased);
    }

    // Each gate stands over the water, or the ground where there is none
    // yet: low where the country is open, and clear of the crowns where
    // trees stand by the stream, which it climbs over ahead of time.
    const auto elevation = [] (const auto& bundle, const plan_position_t& at) {
      return terrain::SurfaceElevation (
        spatial::sample<terrain::surface_elevation> (
          bundle, at_height (at, 0.0f * u::m)));
    };
    const meters_t period_x = surface.domain ().period_x ();
    const meters_t period_z = surface.domain ().period_z ();
    std::vector<terrain::SurfaceElevation> level (gates.size ());
    std::vector<terrain::ElevationChange> above (
      gates.size (), terrain::ElevationChange (9.0f * u::m));
    for (std::size_t gate = 0; gate < gates.size (); ++gate)
      level[gate] = std::max (elevation (surface, gates[gate]),
                              elevation (water, gates[gate]));
    constexpr meters_t beside = 22.0f * u::m;
    {
      // The gates by the tile of the world they stand in, so that each
      // tree asks only those about it.
      const int tiles_x =
        std::max (1,
                  static_cast<int> (
                    (period_x / (32.0f * u::m)).numerical_value_in (one)));
      const int tiles_z =
        std::max (1,
                  static_cast<int> (
                    (period_z / (32.0f * u::m)).numerical_value_in (one)));
      const auto tile = [&] (const plan_position_t& at) {
        const auto lane = [] (meters_t at, meters_t extent, int tiles) {
          const meters_t lap =
            at - extent * std::floor ((at / extent).numerical_value_in (one));
          return std::min (
            tiles - 1,
            static_cast<int> ((lap / extent).numerical_value_in (one) * tiles));
        };
        return std::pair { lane (x_of (at), period_x, tiles_x),
                           lane (z_of (at), period_z, tiles_z) };
      };
      std::vector<std::vector<std::uint32_t>> standing (
        static_cast<std::size_t> (tiles_x) * tiles_z);
      for (std::size_t gate = 0; gate < gates.size (); ++gate) {
        const auto [x, z] = tile (gates[gate]);
        standing[static_cast<std::size_t> (z) * tiles_x + x].push_back (
          static_cast<std::uint32_t> (gate));
      }
      for (const RiverFlightTree& tree : trees) {
        const auto [x, z] = tile (tree.at);
        for (int dz = -1; dz <= 1; ++dz)
          for (int dx = -1; dx <= 1; ++dx)
            for (const std::uint32_t gate :
                 standing[static_cast<std::size_t> (
                            terrain::wrap_index (z + dz, tiles_z)) *
                            tiles_x +
                          terrain::wrap_index (x + dx, tiles_x)]) {
              const meters_t away_x = terrain::minimum_image_delta (
                x_of (tree.at) - x_of (gates[gate]), period_x);
              const meters_t away_z = terrain::minimum_image_delta (
                z_of (tree.at) - z_of (gates[gate]), period_z);
              if (away_x * away_x + away_z * away_z > beside * beside)
                continue;
              // The tree's top over the water here, and room over that.
              const terrain::ElevationChange top =
                elevation (surface, tree.at) + tree.height - level[gate];
              above[gate] = std::max<terrain::ElevationChange> (
                above[gate], top + 7.0f * u::m);
            }
      }
    }
    {
      std::vector<terrain::ElevationChange> highest = above;
      for (std::size_t gate = 0; gate < gates.size (); ++gate)
        for (std::size_t near = gate >= 2 ? gate - 2 : 0;
             near < std::min (gate + 3, gates.size ());
             ++near)
          highest[gate] = std::max (highest[gate], above[near]);
      above = std::move (highest);
      for (int pass = 0; pass < 4; ++pass) {
        std::vector<terrain::ElevationChange> eased = above;
        for (std::size_t gate = 1; gate + 1 < gates.size (); ++gate)
          eased[gate] = 0.25f * above[gate - 1] + 0.5f * above[gate] +
                        0.25f * above[gate + 1];
        above = std::move (eased);
      }
    }

    const meters_t distance =
      gate_spacing * static_cast<float> (gates.size () - 1);
    const speed_t pace = duration > 0.0f * u::s
                           ? std::clamp<speed_t> (distance / duration,
                                                  5.0f * u::m / u::s,
                                                  90.0f * u::m / u::s)
                           : 22.0f * u::m / u::s;
    plan.waypoints.reserve (gates.size ());
    for (std::size_t gate = 0; gate < gates.size (); ++gate) {
      // It watches the water some way ahead: farther from higher up, but
      // more steeply down.
      const std::size_t ahead =
        std::min (gates.size () - 1,
                  gate + 1 +
                    static_cast<std::size_t> (
                      ((18.0f * u::m + 1.25f * above[gate]) / gate_spacing)
                        .numerical_value_in (one)));
      const position_t position =
        terrain::at_elevation (gates[gate], level[gate] + above[gate]);
      position_t subject = terrain::at_elevation (gates[ahead], level[ahead]);
      if (ahead == gate)
        subject =
          position +
          flight_shift (
            displacement (lift (plan_value (gates[gate] - gates[gate - 1])))) -
          flight_shift (displacement (
            Vec3 (0.0f, (0.4f * above[gate]).numerical_value_in (u::m), 0.0f)));
      plan.waypoints.push_back ({ .position = position,
                                  .subject = subject,
                                  .cruise_speed = pace,
                                  .field_of_view = 60.0f * u::deg });
    }
    plan.style = { .clearance = 5.0f * u::m,
                   .floor = 3.5f * u::m,
                   .anticipation = 30.0f * u::m,
                   .slowest_turn = 6.0f * u::m / u::s,
                   .look_near_least = 12.0f * u::m,
                   .look_near_most = 30.0f * u::m,
                   .look_far_least = 45.0f * u::m,
                   .look_far_most = 110.0f * u::m,
                   .subject_weight = 0.6f * one,
                   .bank = 0.16f * u::rad };
    std::cerr << "moppe: river flight: " << course.size () << " reaches, "
              << meters_value (distance) << " m at " << speed_value (pace)
              << " m/s, about " << seconds_value (distance / pace) << " s\n";
    return plan;
  }

  position_t CinematicFlight::curve_position (std::size_t segment,
                                              blend_weight_t t) const {
    const auto tangent = [this] (std::size_t index) -> displacement_t {
      const position_t& current = m_waypoints[index].position;
      if (index == 0)
        return (m_waypoints[1].position - current) * 0.42f;
      if (index + 1 == m_waypoints.size ())
        return (current - m_waypoints[index - 1].position) * 0.42f;

      const position_t& previous = m_waypoints[index - 1].position;
      const position_t& next = m_waypoints[index + 1].position;
      const displacement_t across = next - previous;
      if (length2 (displacement_value (across)) < 1e-5f)
        return displacement_t {};
      const direction_t direction = direction_of (across);
      const meters_t incoming = flight_length (current - previous);
      const meters_t outgoing = flight_length (next - current);
      return along (direction, 0.42f * std::min (incoming, outgoing));
    };

    const position_t& from = m_waypoints[segment].position;
    const position_t& to = m_waypoints[segment + 1].position;
    const displacement_t from_tangent = tangent (segment);
    const displacement_t to_tangent = tangent (segment + 1);
    const magnitude_t s = t.numerical_value_in (one);
    const magnitude_t s2 = s * s;
    const magnitude_t s3 = s2 * s;
    const magnitude_t h00 = 2.0f * s3 - 3.0f * s2 + 1.0f;
    const magnitude_t h10 = s3 - 2.0f * s2 + s;
    const magnitude_t h01 = -2.0f * s3 + 3.0f * s2;
    const magnitude_t h11 = s3 - s2;
    return moppe::position (
      position_value (from) * scalar_value (h00) +
      displacement_value (from_tangent) * scalar_value (h10) +
      position_value (to) * scalar_value (h01) +
      displacement_value (to_tangent) * scalar_value (h11));
  }

  CinematicFlight::RouteState
  CinematicFlight::route_state (meters_t distance) const {
    if (m_arc_samples.empty ())
      return {};
    distance =
      std::clamp (distance, 0.0f * u::m, m_arc_samples.back ().distance);
    const auto right =
      std::lower_bound (m_arc_samples.begin (),
                        m_arc_samples.end (),
                        distance,
                        [] (const ArcSample& sample, meters_t value) {
                          return sample.distance < value;
                        });
    const ArcSample* after =
      right == m_arc_samples.end () ? &m_arc_samples.back () : &*right;
    const ArcSample* before =
      right == m_arc_samples.begin () ? after : &*(right - 1);
    const meters_t span = after->distance - before->distance;
    const blend_weight_t alpha =
      span > 1e-5f * u::m
        ? ((distance - before->distance) / span).numerical_value_in (one) *
            blend_weight[one]
        : 0.0f * blend_weight[one];

    std::size_t segment = before->segment;
    blend_weight_t t = before->t;
    if (before->segment == after->segment)
      t = flight_ease (t, after->t, alpha);
    else {
      segment = after->segment;
      t = after->t * alpha.numerical_value_in (one);
    }
    const blend_weight_t lift_alpha = flight_smoothstep (alpha);
    const meters_t lift =
      flight_ease (before->terrain_lift, after->terrain_lift, lift_alpha);
    const speed_t curve_speed_limit =
      flight_ease (before->speed_limit, after->speed_limit, alpha);
    position_t position = curve_position (segment, t);
    flight_raise (position, lift);

    const blend_weight_t eased = flight_smoothstep (t);
    const CinematicFlightWaypoint& from = m_waypoints[segment];
    const CinematicFlightWaypoint& to = m_waypoints[segment + 1];
    return {
      .position = position,
      .subject = moppe::position (linear_vector_interpolate (
        position_value (from.subject), position_value (to.subject), eased)),
      .cruise_speed =
        std::min (curve_speed_limit,
                  flight_ease (from.cruise_speed, to.cruise_speed, eased)),
      .field_of_view = flight_ease (from.field_of_view, to.field_of_view, eased)
    };
  }

  void
  CinematicFlight::build_flight_ribbon (const map::SurfaceGeometry& surface) {
    m_arc_samples.clear ();
    if (m_waypoints.size () < 2)
      return;

    position_t previous = curve_position (0, 0.0f * blend_weight[one]);
    meters_t distance = 0.0f * u::m;
    for (std::size_t segment = 0; segment + 1 < m_waypoints.size ();
         ++segment) {
      const meters_t chord = flight_length (m_waypoints[segment + 1].position -
                                            m_waypoints[segment].position);
      const int steps = std::max (
        12,
        static_cast<int> ((chord / (7.0f * u::m)).numerical_value_in (one)));
      const int first = segment == 0 ? 0 : 1;
      for (int step = first; step <= steps; ++step) {
        const blend_weight_t t =
          static_cast<float> (step) / steps * blend_weight[one];
        const position_t point = curve_position (segment, t);
        if (!m_arc_samples.empty ())
          distance += flight_length (point - previous);
        const meters_t ground = flight_ground (surface, moppe::plan (point));
        m_arc_samples.push_back (
          { .distance = distance,
            .segment = segment,
            .t = t,
            .terrain_lift =
              std::max (0.0f * u::m,
                        ground + m_style.clearance - flight_height_of (point)),
            .speed_limit = 360.0f * u::m / u::s });
        previous = point;
      }
    }

    // Raise the ribbon before a ridge and let it settle after the crest.
    // A Gaussian clearance envelope avoids the slope discontinuities produced
    // by a simple max-clearance cone: the aircraft begins climbing before the
    // obstruction, rounds the vertical crest, then dives away continuously.
    std::vector<meters_t> minimum_lift;
    minimum_lift.reserve (m_arc_samples.size ());
    for (const ArcSample& sample : m_arc_samples)
      minimum_lift.push_back (sample.terrain_lift);
    const meters_t anticipation = m_style.anticipation;
    const meters_t horizon = 3.2f * anticipation;
    const auto envelope = [anticipation] (meters_t separation) {
      return std::exp (
               (-0.5f * separation * separation / (anticipation * anticipation))
                 .numerical_value_in (one)) *
             one;
    };
    for (std::size_t i = 0; i < m_arc_samples.size (); ++i) {
      meters_t lift = minimum_lift[i];
      for (std::size_t j = i; j > 0; --j) {
        const meters_t separation =
          m_arc_samples[i].distance - m_arc_samples[j - 1].distance;
        if (separation > horizon)
          break;
        const proportion_t weight = envelope (separation);
        lift = std::max (lift, meters_t (minimum_lift[j - 1] * weight));
      }
      for (std::size_t j = i + 1; j < m_arc_samples.size (); ++j) {
        const meters_t separation =
          m_arc_samples[j].distance - m_arc_samples[i].distance;
        if (separation > horizon)
          break;
        const proportion_t weight = envelope (separation);
        lift = std::max (lift, meters_t (minimum_lift[j] * weight));
      }
      m_arc_samples[i].terrain_lift = lift;
    }
    std::vector<meters_t> smoothed (m_arc_samples.size ());
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
    distance = 0.0f * u::m;
    previous =
      curve_position (m_arc_samples.front ().segment, m_arc_samples.front ().t);
    flight_raise (previous, m_arc_samples.front ().terrain_lift);
    m_arc_samples.front ().distance = 0.0f * u::m;
    for (std::size_t i = 1; i < m_arc_samples.size (); ++i) {
      position_t point =
        curve_position (m_arc_samples[i].segment, m_arc_samples[i].t);
      flight_raise (point, m_arc_samples[i].terrain_lift);
      distance += flight_length (point - previous);
      m_arc_samples[i].distance = distance;
      previous = point;
    }

    // Tight curvature lowers the admissible airspeed, but this flight should
    // carve through a bend instead of tiptoeing around it. The high lateral
    // authority is closer to a bird or racing drone than a tour helicopter.
    // Propagate the limit in both directions so speed changes are anticipated.
    std::vector<position_t> points;
    points.reserve (m_arc_samples.size ());
    for (const ArcSample& sample : m_arc_samples) {
      position_t point = curve_position (sample.segment, sample.t);
      flight_raise (point, sample.terrain_lift);
      points.push_back (point);
    }
    for (std::size_t i = 1; i + 1 < m_arc_samples.size (); ++i) {
      std::size_t left = i - 1;
      while (left > 0 &&
             m_arc_samples[i].distance - m_arc_samples[left].distance <
               16.0f * u::m)
        --left;
      std::size_t right = i + 1;
      while (right + 1 < m_arc_samples.size () &&
             m_arc_samples[right].distance - m_arc_samples[i].distance <
               16.0f * u::m)
        ++right;
      const displacement_t incoming = points[i] - points[left];
      const displacement_t outgoing = points[right] - points[i];
      if (length2 (displacement_value (incoming)) < 1e-5f ||
          length2 (displacement_value (outgoing)) < 1e-5f)
        continue;
      const radians_t angle =
        std::acos (std::clamp (dot (direction_value (direction_of (incoming)),
                                    direction_value (direction_of (outgoing))),
                               -1.0f,
                               1.0f)) *
        u::rad;
      const meters_t tangent_separation =
        0.5f * (m_arc_samples[right].distance - m_arc_samples[left].distance);
      const auto curvature = angle / std::max (1.0f * u::m, tangent_separation);
      if (curvature > 1e-5f * u::rad / u::m)
        m_arc_samples[i].speed_limit = std::clamp<speed_t> (
          std::sqrt (24.0f / curvature.numerical_value_in (u::rad / u::m)) *
            u::m / u::s,
          m_style.slowest_turn,
          360.0f * u::m / u::s);
    }
    for (std::size_t i = m_arc_samples.size () - 1; i > 0; --i) {
      const meters_t span =
        m_arc_samples[i].distance - m_arc_samples[i - 1].distance;
      const speed_t limit = m_arc_samples[i].speed_limit;
      const speed_t braking_limit =
        std::sqrt (speed_value (limit) * speed_value (limit) +
                   2.0f * 18.0f * meters_value (span)) *
        u::m / u::s;
      m_arc_samples[i - 1].speed_limit =
        std::min (m_arc_samples[i - 1].speed_limit, braking_limit);
    }
    for (std::size_t i = 1; i < m_arc_samples.size (); ++i) {
      const meters_t span =
        m_arc_samples[i].distance - m_arc_samples[i - 1].distance;
      const speed_t limit = m_arc_samples[i - 1].speed_limit;
      const speed_t acceleration_limit =
        std::sqrt (speed_value (limit) * speed_value (limit) +
                   2.0f * 14.0f * meters_value (span)) *
        u::m / u::s;
      m_arc_samples[i].speed_limit =
        std::min (m_arc_samples[i].speed_limit, acceleration_limit);
    }
  }

  void CinematicFlight::start (const CinematicFlightPlan& plan,
                               const map::SurfaceGeometry& surface) {
    m_waypoints = plan.waypoints;
    m_style = plan.style;
    for (int pass = 0; pass < 4 && m_waypoints.size () >= 2; ++pass) {
      std::vector<CinematicFlightWaypoint> rounded;
      rounded.reserve (m_waypoints.size () * 2);
      rounded.push_back (m_waypoints.front ());
      for (std::size_t i = 0; i + 1 < m_waypoints.size (); ++i) {
        const CinematicFlightWaypoint& from = m_waypoints[i];
        const CinematicFlightWaypoint& to = m_waypoints[i + 1];
        const auto blend = [&] (blend_weight_t t) {
          const auto between = [&] (const position_t& a, const position_t& b) {
            return moppe::position (linear_vector_interpolate (
              position_value (a), position_value (b), t));
          };
          return CinematicFlightWaypoint {
            .position = between (from.position, to.position),
            .subject = between (from.subject, to.subject),
            .cruise_speed = flight_ease (from.cruise_speed, to.cruise_speed, t),
            .field_of_view =
              flight_ease (from.field_of_view, to.field_of_view, t)
          };
        };
        rounded.push_back (blend (0.25f * blend_weight[one]));
        rounded.push_back (blend (0.75f * blend_weight[one]));
      }
      rounded.push_back (m_waypoints.back ());
      m_waypoints = std::move (rounded);
    }
    build_flight_ribbon (surface);
    m_elapsed = 0.0f * u::s;
    m_final_hold = 0.0f * u::s;
    m_route_distance = 0.0f * u::m;
    m_manual_offset = displacement_t {};
    m_manual_velocity = velocity_t {};
    m_bank = 0.0f * u::rad;
    m_look_direction = unit_direction (Vec3 (0, 0, 1));
    m_longitudinal_acceleration = 0.0f * u::m / u::s2;
    m_active = m_arc_samples.size () >= 2;
    if (!m_active)
      return;

    const RouteState initial = route_state (0.0f * u::m);
    const RouteState ahead =
      route_state (std::min (100.0f * u::m, m_arc_samples.back ().distance));
    m_position = initial.position;
    m_speed = initial.cruise_speed;
    const displacement_t toward = ahead.position - initial.position;
    const direction_t direction = length2 (displacement_value (toward)) < 1e-5f
                                    ? unit_direction (Vec3 (0, 0, 1))
                                    : direction_of (toward);
    m_velocity = velocity (direction_value (direction) * speed_value (m_speed));
    m_look_direction = direction;
    m_field_of_view = initial.field_of_view;
  }

  void CinematicFlight::stop () noexcept {
    m_active = false;
  }

  void CinematicFlight::tick (seconds_t step,
                              const map::SurfaceGeometry& surface,
                              const CinematicFlightControls& controls) {
    if (!m_active)
      return;
    const seconds_t dt =
      std::clamp (step, 1.0f / 240.0f * u::s, 1.0f / 20.0f * u::s);
    m_elapsed += dt;

    const meters_t route_end = m_arc_samples.back ().distance;
    const meters_t remaining = route_end - m_route_distance;
    const RouteState before = route_state (m_route_distance);
    const control_signal_t pace =
      std::clamp<control_signal_t> (controls.pace, -1.0f * one, 1.0f * one);
    // The pace stick, as a gain on the route's cruise.
    const scale_factor_t pace_gain =
      1.0f * scale_factor[one] +
      quantity_cast<scale_factor> (quantity_cast<dimensionless> (0.30f * pace));
    speed_t wanted_speed = speed_t (before.cruise_speed * pace_gain);
    const speed_t stopping_speed =
      std::sqrt (std::max (0.0f, 2.0f * 5.0f * meters_value (remaining))) *
      u::m / u::s;
    wanted_speed = std::min (wanted_speed, stopping_speed);

    const metres_per_second_squared_t wanted_acceleration =
      std::clamp<metres_per_second_squared_t> ((wanted_speed - m_speed) *
                                                 (1.8f / u::s),
                                               -18.0f * u::m / u::s2,
                                               14.0f * u::m / u::s2);
    // The most the thrust changes in a second.
    const auto jerk = 32.0f * u::m / (u::s2 * u::s);
    const metres_per_second_squared_t acceleration_step =
      std::clamp<metres_per_second_squared_t> (wanted_acceleration -
                                                 m_longitudinal_acceleration,
                                               -jerk * dt,
                                               jerk * dt);
    m_longitudinal_acceleration += acceleration_step;
    m_speed = std::max<speed_t> (0.0f * u::m / u::s,
                                 m_speed + m_longitudinal_acceleration * dt);
    m_route_distance =
      std::min<meters_t> (route_end, m_route_distance + m_speed * dt);

    const RouteState current = route_state (m_route_distance);
    const meters_t tangent_span = std::clamp<meters_t> (
      m_speed * (0.16f * u::s), 12.0f * u::m, 32.0f * u::m);
    const RouteState behind = route_state (m_route_distance - tangent_span);
    const RouteState ahead = route_state (m_route_distance + tangent_span);
    Vec3 path_way = displacement_value (ahead.position - behind.position);
    if (length2 (path_way) < 1e-5f)
      path_way = velocity_value (m_velocity);
    if (length2 (path_way) < 1e-5f)
      path_way = Vec3 (0, 0, 1);
    const direction_t path_forward = unit_direction (normalized (path_way));

    const direction_t right = right_of (path_forward);
    const displacement_t wanted_offset =
      along (right, meters_t (controls.lateral * (70.0f * u::m))) +
      displacement (
        Vec3 (0, meters_value (meters_t (controls.lift * (55.0f * u::m))), 0));
    velocity_t wanted_manual_velocity =
      (wanted_offset - m_manual_offset) * (0.75f / u::s);
    clamp_length (wanted_manual_velocity, 22.0f * u::m / u::s);
    velocity_t manual_velocity_step =
      wanted_manual_velocity - m_manual_velocity;
    clamp_length (manual_velocity_step, 28.0f * u::m / u::s2 * dt);
    m_manual_velocity += manual_velocity_step;
    m_manual_offset += m_manual_velocity * dt;

    const position_t previous_position = m_position;
    m_position = current.position + flight_shift (m_manual_offset);
    const meters_t floor =
      flight_ground (surface, moppe::plan (m_position)) + m_style.floor;
    flight_set_height (m_position,
                       std::max (flight_height_of (m_position), floor));
    m_velocity = (m_position - previous_position) / dt;

    // Aim through a long corridor of future flight instead of continually
    // correcting toward the nearest landmark. A stabilized drone gimbal sees
    // one committed swoop; the terrain feature still shaped the route itself.
    const meters_t look_near = std::clamp<meters_t> (m_speed * (0.28f * u::s),
                                                     m_style.look_near_least,
                                                     m_style.look_near_most);
    const meters_t look_far = std::clamp<meters_t> (
      m_speed * (1.45f * u::s), m_style.look_far_least, m_style.look_far_most);
    const RouteState view_near = route_state (m_route_distance + look_near);
    const RouteState view_far = route_state (m_route_distance + look_far);
    const displacement_t corridor = view_far.position - view_near.position;
    direction_t wanted_look = length2 (displacement_value (corridor)) < 1e-5f
                                ? path_forward
                                : direction_of (corridor);
    const displacement_t framed = current.subject - current.position;
    if (length2 (displacement_value (framed)) > 1e-5f) {
      const direction_t framed_subject = direction_of (framed);
      const float subject_weight =
        m_style.subject_weight.numerical_value_in (one);
      wanted_look = unit_direction (
        direction_value (wanted_look) * (1.0f - subject_weight) +
        direction_value (framed_subject) * subject_weight);
      if (length2 (direction_value (wanted_look)) > 1e-5f)
        wanted_look =
          unit_direction (normalized (direction_value (wanted_look)));
    }
    const blend_weight_t gimbal_alpha =
      (1.0f - std::exp (-1.9f * seconds_value (dt))) * blend_weight[one];
    m_look_direction = unit_direction (
      direction_value (m_look_direction) +
      (direction_value (wanted_look) - direction_value (m_look_direction)) *
        gimbal_alpha.numerical_value_in (one));
    if (length2 (direction_value (m_look_direction)) > 1e-5f)
      m_look_direction =
        unit_direction (normalized (direction_value (m_look_direction)));

    const degrees_t speed_fov = std::clamp<degrees_t> (
      (m_speed - 70.0f * u::m / u::s) * (0.055f * u::deg * u::s / u::m),
      0.0f * u::deg,
      10.0f * u::deg);
    const degrees_t dive_fov =
      std::clamp<degrees_t> (-direction_value (path_forward)[1] * 9.0f * u::deg,
                             0.0f * u::deg,
                             5.0f * u::deg);
    const degrees_t wanted_fov = current.field_of_view + speed_fov + dive_fov;
    m_field_of_view += (wanted_fov - m_field_of_view) *
                       (1.0f - std::exp (-3.0f * seconds_value (dt)));

    // Roll from the broad upcoming arc, never from frame-local acceleration.
    // This keeps a gently banked horizon through a whole turn and eliminates
    // the left-right rocking caused by tiny corrections in the flight line.
    Vec3 arc_now = displacement_value (view_near.position - current.position);
    Vec3 arc_future =
      displacement_value (view_far.position - view_near.position);
    arc_now[1] = 0.0f;
    arc_future[1] = 0.0f;
    radians_t turn = 0.0f * u::rad;
    if (length2 (arc_now) > 1e-5f && length2 (arc_future) > 1e-5f) {
      normalize (arc_now);
      normalize (arc_future);
      turn =
        std::atan2 (cross (arc_now, arc_future)[1], dot (arc_now, arc_future)) *
        u::rad;
    }
    const radians_t most_bank = m_style.bank;
    const radians_t wanted_bank =
      std::clamp (-turn * 0.48f, -most_bank, most_bank);
    m_bank +=
      (wanted_bank - m_bank) * (1.0f - std::exp (-0.85f * seconds_value (dt)));

    if (m_route_distance >= route_end - 0.01f * u::m) {
      m_speed = 0.0f * u::m / u::s;
      m_final_hold += dt;
      if (m_final_hold >= 1.1f * u::s)
        m_active = false;
    } else
      m_final_hold = 0.0f * u::s;
  }
  direction_t CinematicFlight::forward () const {
    Vec3 result = direction_value (m_look_direction);
    if (length2 (result) < 1e-5f)
      result = length2 (velocity_value (m_velocity)) > 1e-5f
                 ? velocity_value (m_velocity)
                 : Vec3 (0, 0, 1);
    return unit_direction (normalized (result));
  }

  Mat4 CinematicFlight::view_matrix () const {
    const direction_t look = forward ();
    const direction_t right = right_of (look);
    const direction_t level_up = unit_direction (
      normalized (cross (direction_value (right), direction_value (look))));
    const direction_t up = unit_direction (normalized (
      direction_value (level_up) * std::cos (radians_value (m_bank)) +
      direction_value (right) * std::sin (radians_value (m_bank))));
    return Mat4::look_at (position_value (m_position),
                          position_value (m_position) +
                            direction_value (look) * 100.0f,
                          direction_value (up));
  }
}
