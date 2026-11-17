#include <moppe/game/walker.hh>
#include <moppe/game/world_ground.hh>
#include <moppe/terrain/readings.hh>

#include <cmath>

namespace moppe::game {
  plan_extent_t WorldGround::period () const {
    const terrain::TerrainDomain& domain = surface ().domain ();
    return plan_extent (domain.period_x (), domain.period_z ());
  }

  terrain::SurfaceElevation
  WorldGround::elevation (const plan_position_t& at) const {
    return spatial::sample<terrain::surface_elevation> (
      surface (), at_height (at, 0.0f * u::m));
  }

  terrain::SurfaceElevation
  WorldGround::still_water (const plan_position_t& at) const {
    return terrain::surface_elevation_point (still_water_at (
      world, surface (), &generated.water_surface (), lift (plan_value (at))));
  }

  bool WorldGround::open (const plan_position_t& at,
                          meters_t room,
                          meters_t relief) const {
    const terrain::SurfaceElevation h = elevation (at);
    terrain::SurfaceElevation low = h, high = h;
    for (int i = 0; i < 12; ++i) {
      const radians_t b = 6.2831853f * i / 12.0f * u::rad;
      const plan_position_t p =
        at + along (plan_unit_direction (Vec2 (std::cos (radians_value (b)),
                                               std::sin (radians_value (b)))),
                    room);
      const terrain::SurfaceElevation g = elevation (p);
      if (still_water (p) > g - 0.6f * u::m)
        return false;
      low = std::min (low, g);
      high = std::max (high, g);
    }
    if (high - low > relief)
      return false;
    for (const mov::Trunk& trunk :
         trunks.gather (lift (plan_value (at)), meters_value (room) + 2.0f)) {
      const plan_displacement_t apart = plan_position (plan (trunk.root)) - at;
      const meters_t clear = room + trunk.radius * u::m;
      if (length2 (plan_value (apart)) * u::m * u::m < clear * clear)
        return false;
    }
    return true;
  }

  plan_position_t WorldGround::open_near (const plan_position_t& want,
                                          meters_t room,
                                          meters_t relief,
                                          meters_t search) const {
    for (meters_t r = 0.0f * u::m; r <= search; r += 1.5f * u::m) {
      // The ring's circumference in steps of a metre and a half.
      const int around =
        std::max (1,
                  static_cast<int> (
                    (6.2831853f * r / (1.5f * u::m)).numerical_value_in (one)));
      for (int k = 0; k < around; ++k) {
        const radians_t a = 6.2831853f * k / around * u::rad;
        const plan_position_t at =
          want +
          along (plan_unit_direction (Vec2 (std::cos (radians_value (a)),
                                            std::sin (radians_value (a)))),
                 r);
        if (open (at, room, relief))
          return at;
      }
    }
    return want;
  }

  bool WorldGround::on_road (const plan_position_t& at,
                             meters_t reach,
                             bool decks) const {
    const terrain::RoadSurface& roads = generated.road_surface ();
    const terrain::RoadSample road = roads.nearest (at);
    const terrain::RoadFormation& formation = roads.formation ();
    return road.found && road.kind == terrain::Road::Kind::Road &&
           (decks || !road.bridge) &&
           abs (road.across) <
             formation.half_width + formation.shoulder + reach;
  }

  position_t WorldGround::trail_cell (terrain::CellIndex cell) const {
    if (cell == terrain::no_cell)
      return {};
    const terrain::TerrainDomain& grid = generated.trails ().domain;
    const std::size_t width = grid.width ();
    const plan_position_t at = plan_position (
      static_cast<float> (cell.value % width) * grid.spacing_x (),
      static_cast<float> (cell.value / width) * grid.spacing_z ());
    return terrain::at_elevation (at, elevation (at));
  }

  position_t WorldGround::trail_point (const plan_position_t& point) const {
    return terrain::at_elevation (point, elevation (point));
  }

  direction_t WorldGround::trail_heading () const {
    const auto& points = generated.trails ().alignment.points;
    if (points.size () < 2)
      return unit_direction (Vec3 (0, 0, 1));
    // Levelled, then normalized as a vector of the world.
    Vec3 level =
      displacement_value (trail_point (points[1]) - trail_point (points[0]));
    level[1] = 0.0f;
    return unit_direction (length2 (level) > 1e-5f ? normalized (level)
                                                   : Vec3 (0, 0, 1));
  }

  PlaceGround WorldGround::place_ground () const {
    // PlaceGround is planned in bare metres; each reading crosses into it
    // here, from the typed queries. Each keeps a copy of this view, which
    // may not outlive it.
    const terrain::WaterSheets& sheets = generated.water_surface ();
    const terrain::SurfaceElevation sea =
      terrain::surface_elevation_point (world.water_level);
    // Each reading keeps a copy of this view, which may not outlive it.
    const auto level = [g = *this] (const plan_position_t& at) {
      return g.still_water (at);
    };
    return {
      .ground = elevation_field (),
      .water = level,
      .sea =
        [level, sea] (const plan_position_t& at) {
          return level (at) <= sea + 0.05f * u::m;
        },
      .current =
        [&sheets] (const plan_position_t& at) {
          return spatial::sample<terrain::water_velocity> (
            sheets, at_height (at, 0.0f * u::m));
        },
      .forest =
        [&readings = generated.readings ()] (const plan_position_t& at) {
          return spatial::sample<map::forest_cover> (
            readings, at_height (at, 0.0f * u::m));
        },
      .roads = &generated.road_surface (),
      .sea_level = sea,
      .period = period (),
    };
  }
}
