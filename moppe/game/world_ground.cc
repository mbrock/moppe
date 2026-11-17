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
      const float b = 6.2831853f * i / 12.0f;
      const plan_position_t p =
        at +
        along (plan_unit_direction (Vec2 (std::cos (b), std::sin (b))), room);
      const terrain::SurfaceElevation g = elevation (p);
      if (still_water (p) > g - 0.6f * u::m)
        return false;
      low = std::min (low, g);
      high = std::max (high, g);
    }
    if (high - low > relief)
      return false;
    const Vec2& centre = plan_value (at);
    for (const mov::Trunk& trunk :
         trunks.gather (lift (centre), meters_value (room) + 2.0f)) {
      const plan_displacement_t apart = plan_position (plan (trunk.root)) - at;
      const meters_t clear = room + trunk.radius * u::m;
      if (length2 (plan_value (apart)) <
          meters_value (clear) * meters_value (clear))
        return false;
    }
    return true;
  }

  plan_position_t WorldGround::open_near (const plan_position_t& want,
                                          meters_t room,
                                          meters_t relief,
                                          meters_t search) const {
    for (meters_t r = 0.0f * u::m; r <= search; r += 1.5f * u::m) {
      const int around =
        std::max (1, static_cast<int> (6.2831853f * meters_value (r) / 1.5f));
      for (int k = 0; k < around; ++k) {
        const float a = 6.2831853f * k / around;
        const plan_position_t at =
          want +
          along (plan_unit_direction (Vec2 (std::cos (a), std::sin (a))), r);
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

  Vec3 WorldGround::trail_cell (terrain::CellIndex cell) const {
    if (cell == terrain::no_cell)
      return {};
    const terrain::TerrainDomain& grid = generated.trails ().domain;
    const std::size_t width = grid.width ();
    const float x =
      (cell.value % width) * (grid.spacing_x ()).numerical_value_in (u::m);
    const float z =
      (cell.value / width) * (grid.spacing_z ()).numerical_value_in (u::m);
    return Vec3 (x, height (Vec3 (x, 0.0f, z)), z);
  }

  Vec3 WorldGround::trail_point (const plan_position_t& point) const {
    return lift (plan_value (point), height (lift (plan_value (point))));
  }

  Vec3 WorldGround::trail_heading () const {
    const auto& points = generated.trails ().alignment.points;
    if (points.size () < 2)
      return Vec3 (0, 0, 1);
    Vec3 direction = trail_point (points[1]) - trail_point (points[0]);
    direction[1] = 0.0f;
    return length2 (direction) > 1e-5f ? normalized (direction)
                                       : Vec3 (0, 0, 1);
  }

  PlaceGround WorldGround::place_ground () const {
    const terrain::WaterSheets& sheets = generated.water_surface ();
    const float sea = world.water_level.numerical_value_in (u::m);
    // Each reading keeps a copy of this view, which may not outlive it.
    const auto level = [g = *this] (float x, float z) {
      return g.still_level (Vec3 (x, 0.0f, z));
    };
    return {
      .ground = reader (),
      .water = level,
      .sea = [level, sea] (float x,
                           float z) { return level (x, z) <= sea + 0.05f; },
      .current =
        [&sheets] (float x, float z) {
          return spatial::sample<terrain::water_velocity> (
                   sheets, moppe::position (Vec3 (x, 0.0f, z)))
            .numerical_value_in (u::m / u::s);
        },
      .forest =
        [&readings = generated.readings ()] (float x, float z) {
          return spatial::sample<map::forest_cover> (
                   readings, moppe::position (Vec3 (x, 0.0f, z)))
            .numerical_value_in (one);
        },
      .roads = &generated.road_surface (),
      .sea_level = sea,
      .period_x = period_x (),
      .period_z = period_z (),
    };
  }
}
