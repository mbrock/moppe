#include <moppe/game/farm_model.hh>
#include <moppe/game/game_session.hh>
#include <moppe/game/places.hh>
#include <moppe/map/surface.hh>

#include <tests/recording_renderer.hh>
#include <tests/test.hh>

#include <algorithm>
#include <cmath>

using namespace moppe;
using namespace moppe::game;

namespace {
  // A made country: west of x = 2000 a beach falling into a sea at 50 m
  // (its shore at x = 1000); east of it a round hill whose top stands at
  // (3000, 3000), 200 m up; and north of z = 5000 level ground at 81 m
  // holding a lake at 80 m round (1500, 6000), whose outlet runs east
  // down a stream four metres wide along z = 6000.
  constexpr float places_test_sea = 50.0f;

  bool places_test_in_lake (float x, float z) {
    return std::hypot (x - 1500.0f, z - 6000.0f) < 100.0f;
  }

  bool places_test_in_stream (float x, float z) {
    return x >= 1590.0f && std::abs (z - 6000.0f) < 2.0f;
  }

  float places_test_ground (float x, float z) {
    if (z > 5000.0f)
      return places_test_in_lake (x, z)     ? 78.0f
             : places_test_in_stream (x, z) ? 79.5f
                                            : 81.0f;
    if (x < 2000.0f)
      return places_test_sea + (x - 1000.0f) * 0.2f;
    const float r = std::hypot (x - 3000.0f, z - 3000.0f);
    return 200.0f - 0.002f * r * r;
  }

  float places_test_water (float x, float z) {
    if (z > 5000.0f &&
        (places_test_in_lake (x, z) || places_test_in_stream (x, z)))
      return 80.0f;
    return places_test_sea;
  }

  PlaceGround places_test_country () {
    return {
      .ground =
        [] (plan_position_t at) {
          return terrain::surface_elevation_point (
            places_test_ground (plan_value (at).x (), plan_value (at).z ()) *
            u::m);
        },
      .water =
        [] (plan_position_t at) {
          return terrain::surface_elevation_point (
            places_test_water (plan_value (at).x (), plan_value (at).z ()) *
            u::m);
        },
      .sea =
        [] (plan_position_t at) {
          return places_test_water (plan_value (at).x (),
                                    plan_value (at).z ()) <=
                 places_test_sea + 0.05f;
        },
      .current =
        [] (plan_position_t at) {
          return (places_test_in_stream (plan_value (at).x (),
                                         plan_value (at).z ())
                    ? Vec3 (0.8f, 0.0f, 0.0f)
                    : Vec3 (0.0f, 0.0f, 0.0f)) *
                 terrain::water_velocity[u::m / u::s];
        },
      .sea_level = terrain::surface_elevation_point (places_test_sea * u::m),
    };
  }

  // The ground's height under a place, in metres.
  float places_test_ground_under (const PlaceGround& country,
                                  const position_t& at) {
    return terrain::surface_elevation_value (country.ground (plan (at)));
  }

  terrain::RoadNetwork places_test_network (float sanctuary_left) {
    using Kind = terrain::RoadPlace::Kind;
    terrain::RoadNetwork network;
    network.places = {
      { .kind = Kind::Landing, .at = plan_position (1010.0f, 500.0f) },
      { .kind = Kind::Sanctuary,
        .at = plan_position (3000.0f, 3004.0f),
        .abandoned = sanctuary_left * moppe::astronomy::Julian_year },
      { .kind = Kind::LakeOutlet, .at = plan_position (1600.0f, 6000.0f) },
    };
    return network;
  }
}

MOPPE_TEST (the_landing_jetty_runs_out_to_water_deep_enough_to_moor) {
  const PlaceGround country = places_test_country ();
  const PlacesPlan plan = plan_places (places_test_network (0.0f), country, 1);
  MOPPE_CHECK (plan.landing.has_value ());
  if (!plan.landing)
    return;
  const Landing& landing = *plan.landing;
  // Out to sea, which lies west, from where the beach comes up to the
  // deck.
  MOPPE_CHECK (direction_value (landing.jetty.out)[0] < -0.9f);
  MOPPE_CHECK_NEAR (places_test_ground_under (country, landing.jetty.root),
                    terrain::surface_elevation_value (landing.jetty.deck),
                    0.3f);
  // The skiff lies afloat beside its head, bow to the land.
  const float depth =
    places_test_sea - places_test_ground_under (country, landing.berth);
  MOPPE_CHECK (depth > 1.5f);
  MOPPE_CHECK (direction_value (landing.berth_forward)[0] > 0.9f);
  MOPPE_CHECK (cleared (plan, landing.jetty.root));
}

MOPPE_TEST (a_sanctuary_stands_on_its_summit_and_falls_once_left) {
  const PlaceGround country = places_test_country ();
  const PlacesPlan kept = plan_places (places_test_network (0.0f), country, 1);
  const PlacesPlan left =
    plan_places (places_test_network (900.0f), country, 1);
  MOPPE_CHECK (kept.sanctuaries.size () == 1 && left.sanctuaries.size () == 1);
  if (kept.sanctuaries.empty () || left.sanctuaries.empty ())
    return;
  const game::Sanctuary& s = kept.sanctuaries[0];
  // On the top, not where the planner's coarse node put it.
  const Vec3& centre = position_value (s.centre);
  MOPPE_CHECK (std::hypot (centre[0] - 3000.0f, centre[2] - 3000.0f) < 1.5f);
  MOPPE_CHECK (s.kept && s.ruin == 0.0f * proportion[one]);
  MOPPE_CHECK (position_value (s.hearth)[1] > 200.3f);
  // A walker at the platform's edge can light it; one at the wall
  // cannot.
  MOPPE_CHECK (
    sanctuary_at_hand (
      kept, position (position_value (s.hearth) + Vec3 (2.0f, 0, 0))) == 0u);
  MOPPE_CHECK (!sanctuary_at_hand (
    kept,
    s.centre +
      quantity_cast<isq::position_vector> (along (s.gate, s.wall_radius))));
  // Long left, the stones have tumbled and the platform stands lower.
  const game::Sanctuary& ruin = left.sanctuaries[0];
  MOPPE_CHECK (!ruin.kept && ruin.ruin > 0.5f * proportion[one]);
  MOPPE_CHECK (position_value (ruin.hearth)[1] <
               position_value (s.hearth)[1] - 0.1f);
}

MOPPE_TEST (the_mill_turns_its_wheel_in_the_outlet_stream) {
  const PlaceGround country = places_test_country ();
  const PlacesPlan plan = plan_places (places_test_network (0.0f), country, 1);
  MOPPE_CHECK (plan.farms.size () == 1);
  if (plan.farms.empty ())
    return;
  const Farm& farm = plan.farms[0];
  const Building* mill = nullptr;
  for (const Building& b : farm.buildings)
    if (b.kind == Building::Kind::mill)
      mill = &b;
  MOPPE_CHECK (mill != nullptr);
  MOPPE_CHECK (farm.buildings.size () >= 2);
  if (!mill)
    return;
  // Downstream of the outlet, on dry ground beside the stream...
  const Vec3& mill_at = position_value (mill->at);
  MOPPE_CHECK (mill_at[0] > 1605.0f && mill_at[0] < 1645.0f);
  MOPPE_CHECK (!places_test_in_stream (mill_at[0], mill_at[2]));
  // ...with the wheel's axle over the water.
  const Vec3 forward = direction_value (mill->forward);
  const Vec3 left (forward[2], 0.0f, -forward[0]);
  const Vec3 axle = mill_at + left * farm_model::mill_axle[0] +
                    forward * farm_model::mill_axle[2];
  MOPPE_CHECK (std::abs (axle[2] - 6000.0f) < 2.2f);
  // The house and barn keep off the water.
  for (const Building& b : farm.buildings)
    if (b.kind != Building::Kind::mill)
      MOPPE_CHECK (!places_test_in_lake (position_value (b.at)[0],
                                         position_value (b.at)[2]) &&
                   std::abs (position_value (b.at)[2] - 6000.0f) > 8.0f);
}

MOPPE_TEST (a_beacon_catches_burns_and_dies_down) {
  const auto burn = [] (double lit, double out, double now) {
    return beacon_burn (play_time (lit), play_time (out), play_time (now))
      .numerical_value_in (one);
  };
  MOPPE_CHECK (burn (-100.0, -100.0, 10.0) == 0.0f);
  MOPPE_CHECK (burn (0.0, -100.0, 1.0) < 0.2f);
  MOPPE_CHECK_NEAR (burn (0.0, -100.0, 20.0), 1.0f, 1e-5f);
  MOPPE_CHECK (burn (0.0, 20.0, 20.5) > 0.5f);
  MOPPE_CHECK (burn (0.0, 20.0, 30.0) == 0.0f);
}

// A farmhouse can be walked into: straight at its open door the hiker
// climbs the step onto the floor and into the room, while straight at
// the wall beside it they stay outside.
MOPPE_TEST (the_farmhouse_is_walked_into_through_its_door) {
  map::SurfaceGeometry surface (terrain::TerrainDomain (
    129, 129, spatial_extent_in_metres (Vec3 (128, 0, 128))));
  std::ranges::fill (
    spatial::get<terrain::surface_elevation> (surface),
    terrain::surface_elevation_point (10.0f * mp_units::si::metre));
  map::rebuild_geometry (surface);
  game::WorldParams world;
  world.map_size = spatial_extent_in_metres (Vec3 (128, 20, 128));
  world.resolution = 129;
  world.water_level = 0 * u::m;
  // Its front faces +x, so the model's x (its left) runs along -z.
  const Vec3 at (64, 10, 64), forward (1, 0, 0), left (0, 0, -1);
  PlacesPlan plan;
  plan.farms.push_back (
    { .buildings = { { .kind = Building::Kind::house,
                       .at = position (at),
                       .forward = unit_direction (forward) } } });
  const PlaceGround ground {
    .ground =
      [] (plan_position_t) {
        return terrain::surface_elevation_point (10.0f * u::m);
      },
    .water =
      [] (plan_position_t) {
        return terrain::surface_elevation_point (0.0f * u::m);
      },
    .sea = [] (plan_position_t) { return false; },
    .current =
      [] (plan_position_t) {
        return Vec3 () * terrain::water_velocity[u::m / u::s];
      },
  };
  test::RecordingRenderer renderer;
  PlacesLandscape landscape;
  landscape.rebuild (renderer, plan, ground);
  mov::TrunkField trunks;
  trunks.set_trunks (landscape.colliders (),
                     plan_extent (0.0f * u::m, 0.0f * u::m));
  trunks.focus (position (at));
  const auto walk_at = [&] (float aside) {
    GameSession session (world, surface);
    session.start_on_foot ();
    const Vec3 from = at + forward * 9.0f + left * aside;
    session.walker ().spawn (position (from), unit_direction (-forward));
    for (int i = 0; i < 60 * 6; ++i)
      advance_game_session (world,
                            surface,
                            session,
                            InputFrame { .drive = 0.5f * control_signal[one] },
                            seconds (1.0f / 60.0f),
                            &trunks);
    return position_value (session.walker ().position ());
  };
  // The doorway is at x -0.9 of the model, its left.
  const Vec3 in = walk_at (-0.9f);
  MOPPE_CHECK (dot (in - at, forward) < 1.0f);
  MOPPE_CHECK_NEAR (in[1], at[1] + 0.64f, 0.1f);
  const Vec3 out = walk_at (1.6f);
  MOPPE_CHECK (dot (out - at, forward) > 2.5f);
}

// The barn is walked into up the ramp to its open doors and onto its
// floor, where the animals stand too (floor_at); its walls keep the
// hiker out beside the doors.
MOPPE_TEST (the_barn_is_walked_into_up_its_ramp) {
  map::SurfaceGeometry surface (terrain::TerrainDomain (
    129, 129, spatial_extent_in_metres (Vec3 (128, 0, 128))));
  std::ranges::fill (
    spatial::get<terrain::surface_elevation> (surface),
    terrain::surface_elevation_point (10.0f * mp_units::si::metre));
  map::rebuild_geometry (surface);
  game::WorldParams world;
  world.map_size = spatial_extent_in_metres (Vec3 (128, 20, 128));
  world.resolution = 129;
  world.water_level = 0 * u::m;
  const Vec3 at (64, 10, 64), forward (1, 0, 0), left (0, 0, -1);
  PlacesPlan plan;
  plan.farms.push_back (
    { .buildings = { { .kind = Building::Kind::barn,
                       .at = position (at),
                       .forward = unit_direction (forward) } } });
  const PlaceGround ground {
    .ground =
      [] (plan_position_t) {
        return terrain::surface_elevation_point (10.0f * u::m);
      },
    .water =
      [] (plan_position_t) {
        return terrain::surface_elevation_point (0.0f * u::m);
      },
    .sea = [] (plan_position_t) { return false; },
    .current =
      [] (plan_position_t) {
        return Vec3 () * terrain::water_velocity[u::m / u::s];
      },
  };
  // The floor inside, and the ramp falling from the sill to the ground.
  const float floor = at[1] + farm_model::barn_floor[1];
  const Vec3 middle = at + forward * -1.0f;
  MOPPE_CHECK_NEAR (terrain::surface_elevation_value (*floor_at (
                      plan, moppe::plan_position (moppe::plan (middle)))),
                    floor,
                    1e-4f);
  float last = floor + 0.01f;
  for (meters_t d = barn_front + 0.05f * u::m;
       d < barn_front + barn_ramp_length;
       d += 0.3f * u::m) {
    const Vec3 p = at + forward * meters_value (d);
    const std::optional<terrain::SurfaceElevation> h =
      floor_at (plan, moppe::plan_position (moppe::plan (p)));
    MOPPE_CHECK (h && terrain::surface_elevation_value (*h) < last);
    last = h ? terrain::surface_elevation_value (*h) : last;
  }
  // Its foot is set into the ground.
  MOPPE_CHECK (last < at[1] + 0.05f);
  const Vec3 yard =
    at + forward * meters_value (barn_front + barn_ramp_length + 1.0f * u::m);
  MOPPE_CHECK (!floor_at (plan, moppe::plan_position (moppe::plan (yard))));

  test::RecordingRenderer renderer;
  PlacesLandscape landscape;
  landscape.rebuild (renderer, plan, ground);
  mov::TrunkField trunks;
  trunks.set_trunks (landscape.colliders (),
                     plan_extent (0.0f * u::m, 0.0f * u::m));
  trunks.focus (position (at));
  const auto walk_at = [&] (float aside) {
    GameSession session (world, surface);
    session.start_on_foot ();
    const Vec3 from = at + forward * 12.0f + left * aside;
    session.walker ().spawn (position (from), unit_direction (-forward));
    for (int i = 0; i < 60 * 8; ++i)
      advance_game_session (world,
                            surface,
                            session,
                            InputFrame { .drive = 0.5f * control_signal[one] },
                            seconds (1.0f / 60.0f),
                            &trunks);
    return position_value (session.walker ().position ());
  };
  const Vec3 in = walk_at (0.3f);
  MOPPE_CHECK (dot (in - at, forward) < meters_value (barn_front) - 0.5f);
  MOPPE_CHECK_NEAR (in[1], floor, 0.1f);
  const Vec3 out = walk_at (meters_value (barn_door_half) + 2.0f);
  MOPPE_CHECK (dot (out - at, forward) > meters_value (barn_front));
}
