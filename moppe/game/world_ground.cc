#include <moppe/game/walker.hh>
#include <moppe/game/world_ground.hh>
#include <moppe/gfx/signal.hh>
#include <moppe/terrain/readings.hh>
#include <moppe/terrain/snow_climate.hh>

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
      world, surface (), generated.water_view (), at_height (at, 0.0f * u::m)));
  }

  bool WorldGround::frozen_water (const plan_position_t& at) const {
    const terrain::SurfaceElevation ground = elevation (at);
    const terrain::SurfaceElevation level = still_water (at);
    if (level <= ground)
      return false;
    const terrain::SurfaceElevation freeze_y =
      terrain::surface_elevation_point (
        world.water_level + 0.62f * std::max (1.0f * u::m, land_relief));
    if (level < freeze_y)
      return false;
    const terrain::WaterView water = generated.water_view ();
    if (!water)
      return true;
    const speed_t flow_speed =
      length (plan (
        water.sample<terrain::water_velocity> (at_height (at, 0.0f * u::m))
          .numerical_value_in (u::m / u::s))) *
      u::m / u::s;
    return flow_speed < 0.15f * u::m / u::s;
  }

  proportion_t WorldGround::snow_cover (const plan_position_t& at) const {
    if (water_depth (at) > terrain::ElevationChange {})
      return frozen_water (at) ? 0.90f * proportion[one] : proportion_t {};
    constexpr proportion_t part = 1.0f * proportion[one];
    const position_t where = at_height (at, 0.0f * u::m);
    const meters_t height =
      quantity_cast<isq::length> (elevation (at).delta_from_zero ());
    const meters_t relief = std::max (1.0f * u::m, land_relief);
    const proportion_t rise =
      quantity_cast<proportion> ((height - world.water_level) / relief);
    const map::SurfaceNormal normal = map::ground_normal_at (surface (), where);
    const Vec3& n = normal.numerical_value_ref_in (one);
    const proportion_t lee = (-0.80f * n[2] + 0.60f * n[0]) * part;
    const proportion_t shelter = quantity_cast<proportion> (
      generated.reading<map::deposition_cover> (where));
    const map::SnowSupport support =
      generated.reading<map::snow_support> (where);
    const proportion_t tread =
      band (0.80f * part,
            0.88f * part,
            quantity_cast<proportion> (
              generated.reading<map::trail_influence> (where)));
    return band (terrain::snow_climate::onset,
                 0.79f * part,
                 rise +
                   terrain::snow_climate::lee.numerical_value_in (one) * lee +
                   terrain::snow_climate::shelter.numerical_value_in (one) *
                     shelter) *
           band (0.55f * part, 0.78f * part, support).numerical_value_in (one) *
           (1.0f - tread.numerical_value_in (one));
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
         trunks.gather (at_height (at, 0.0f * u::m), room + 2.0f * u::m)) {
      const plan_displacement_t apart = plan (trunk.root) - at;
      const meters_t clear = room + trunk.radius;
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
        [g = *this] (const plan_position_t& at) {
          return g.generated.water_view ().sample<terrain::water_velocity> (
            at_height (at, 0.0f * u::m));
        },
      .forest =
        [g = *this] (const plan_position_t& at) {
          return g.generated.reading<map::forest_cover> (
            at_height (at, 0.0f * u::m));
        },
      .roads = &generated.road_surface (),
      .sea_level = sea,
      .period = period (),
    };
  }
}
