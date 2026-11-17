#include <moppe/game/walker.hh>
#include <moppe/game/world_ground.hh>
#include <moppe/terrain/readings.hh>

#include <cmath>

namespace moppe::game {
  float WorldGround::period_x () const {
    return surface ().domain ().period_x ().numerical_value_in (u::m);
  }

  float WorldGround::period_z () const {
    return surface ().domain ().period_z ().numerical_value_in (u::m);
  }

  float WorldGround::height (const Vec3& at) const {
    return terrain::surface_elevation_value (
      spatial::sample<terrain::surface_elevation> (
        surface (), moppe::position (Vec3 (at[0], 0.0f, at[2]))));
  }

  float WorldGround::still_level (const Vec3& at) const {
    return still_water_at (world, surface (), &generated.water_surface (), at)
      .numerical_value_in (u::m);
  }

  bool WorldGround::open (const Vec3& at, float room, float relief) const {
    const float h = height (at);
    float low = h, high = h;
    for (int i = 0; i < 12; ++i) {
      const float b = 6.2831853f * i / 12.0f;
      const Vec3 p = at + Vec3 (std::cos (b), 0.0f, std::sin (b)) * room;
      const float g = height (p);
      if (still_level (p) > g - 0.6f)
        return false;
      low = std::min (low, g);
      high = std::max (high, g);
    }
    if (high - low > relief)
      return false;
    for (const mov::Trunk& trunk : trunks.gather (at, room + 2.0f)) {
      const float dx = trunk.root[0] - at[0];
      const float dz = trunk.root[2] - at[2];
      const float clear = room + trunk.radius;
      if (dx * dx + dz * dz < clear * clear)
        return false;
    }
    return true;
  }

  Vec3 WorldGround::open_near (const Vec3& want,
                               float room,
                               float relief,
                               float search) const {
    for (float r = 0.0f; r <= search; r += 1.5f) {
      const int around = std::max (1, static_cast<int> (6.2831853f * r / 1.5f));
      for (int k = 0; k < around; ++k) {
        const float a = 6.2831853f * k / around;
        const Vec3 at = want + Vec3 (std::cos (a), 0.0f, std::sin (a)) * r;
        if (open (at, room, relief))
          return at;
      }
    }
    return want;
  }

  bool WorldGround::on_road (const Vec3& at, float reach, bool decks) const {
    const terrain::RoadSurface& roads = generated.road_surface ();
    const terrain::RoadSample road = roads.nearest (at[0], at[2]);
    const terrain::RoadFormation& formation = roads.formation ();
    return road.found && road.kind == terrain::Road::Kind::Road &&
           (decks || !road.bridge) &&
           std::abs (road.across) <
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

  Vec3
  WorldGround::trail_point (const terrain::TrailAlignmentPoint& point) const {
    return Vec3 (
      point.x_m, height (Vec3 (point.x_m, 0.0f, point.z_m)), point.z_m);
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
