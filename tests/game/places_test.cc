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
      .ground = places_test_ground,
      .water = places_test_water,
      .sea =
        [] (float x, float z) {
          return places_test_water (x, z) <= places_test_sea + 0.05f;
        },
      .current =
        [] (float x, float z) {
          return places_test_in_stream (x, z) ? Vec3 (0.8f, 0.0f, 0.0f)
                                              : Vec3 (0.0f, 0.0f, 0.0f);
        },
      .sea_level = places_test_sea,
    };
  }

  terrain::RoadNetwork places_test_network (float sanctuary_left) {
    using Kind = terrain::RoadPlace::Kind;
    terrain::RoadNetwork network;
    network.places = {
      { .kind = Kind::Landing, .x_m = 1010.0f, .z_m = 500.0f },
      { .kind = Kind::Sanctuary,
        .x_m = 3000.0f,
        .z_m = 3004.0f,
        .abandoned_years_ago = sanctuary_left },
      { .kind = Kind::LakeOutlet, .x_m = 1600.0f, .z_m = 6000.0f },
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
  MOPPE_CHECK (landing.jetty.out[0] < -0.9f);
  MOPPE_CHECK_NEAR (
    country.ground (landing.jetty.root[0], landing.jetty.root[2]),
    landing.jetty.deck,
    0.3f);
  // The skiff lies afloat beside its head, bow to the land.
  const float depth =
    places_test_sea - country.ground (landing.berth[0], landing.berth[2]);
  MOPPE_CHECK (depth > 1.5f);
  MOPPE_CHECK (landing.berth_forward[0] > 0.9f);
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
  MOPPE_CHECK (std::hypot (s.centre[0] - 3000.0f, s.centre[2] - 3000.0f) <
               1.5f);
  MOPPE_CHECK (s.kept && s.ruin == 0.0f);
  MOPPE_CHECK (s.hearth[1] > 200.3f);
  // A walker at the platform's edge can light it; one at the wall
  // cannot.
  MOPPE_CHECK (sanctuary_at_hand (kept, s.hearth + Vec3 (2.0f, 0, 0)) == 0u);
  MOPPE_CHECK (!sanctuary_at_hand (kept, s.centre + s.gate * s.wall_radius));
  // Long left, the stones have tumbled and the platform stands lower.
  const game::Sanctuary& ruin = left.sanctuaries[0];
  MOPPE_CHECK (!ruin.kept && ruin.ruin > 0.5f);
  MOPPE_CHECK (ruin.hearth[1] < s.hearth[1] - 0.1f);
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
  MOPPE_CHECK (mill->at[0] > 1605.0f && mill->at[0] < 1645.0f);
  MOPPE_CHECK (!places_test_in_stream (mill->at[0], mill->at[2]));
  // ...with the wheel's axle over the water.
  const Vec3 forward = mill->forward;
  const Vec3 left (forward[2], 0.0f, -forward[0]);
  const Vec3 axle = mill->at + left * farm_model::mill_axle[0] +
                    forward * farm_model::mill_axle[2];
  MOPPE_CHECK (std::abs (axle[2] - 6000.0f) < 2.2f);
  // The house and barn keep off the water.
  for (const Building& b : farm.buildings)
    if (b.kind != Building::Kind::mill)
      MOPPE_CHECK (!places_test_in_lake (b.at[0], b.at[2]) &&
                   std::abs (b.at[2] - 6000.0f) > 8.0f);
}

MOPPE_TEST (a_beacon_catches_burns_and_dies_down) {
  MOPPE_CHECK (beacon_burn (-100.0, -100.0, 10.0) == 0.0f);
  MOPPE_CHECK (beacon_burn (0.0, -100.0, 1.0) < 0.2f);
  MOPPE_CHECK_NEAR (beacon_burn (0.0, -100.0, 20.0), 1.0f, 1e-5f);
  MOPPE_CHECK (beacon_burn (0.0, 20.0, 20.5) > 0.5f);
  MOPPE_CHECK (beacon_burn (0.0, 20.0, 30.0) == 0.0f);
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
    { .buildings = {
        { .kind = Building::Kind::house, .at = at, .forward = forward } } });
  const PlaceGround ground {
    .ground = [] (float, float) { return 10.0f; },
    .water = [] (float, float) { return 0.0f; },
    .sea = [] (float, float) { return false; },
    .current = [] (float, float) { return Vec3 (); },
  };
  test::RecordingRenderer renderer;
  PlacesLandscape landscape;
  landscape.rebuild (renderer, plan, ground);
  mov::TrunkField trunks;
  trunks.set_trunks (landscape.colliders (), 0.0f, 0.0f);
  trunks.focus (at);
  const auto walk_at = [&] (float aside) {
    GameSession session (world, surface);
    session.start_on_foot ();
    const Vec3 from = at + forward * 9.0f + left * aside;
    session.walker ().spawn (position (from), -forward);
    for (int i = 0; i < 60 * 6; ++i)
      advance_game_session (world,
                            surface,
                            session,
                            InputFrame { .drive = 0.5f },
                            seconds (1.0f / 60.0f),
                            &trunks);
    return session.walker ().position ();
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
    { .buildings = {
        { .kind = Building::Kind::barn, .at = at, .forward = forward } } });
  const PlaceGround ground {
    .ground = [] (float, float) { return 10.0f; },
    .water = [] (float, float) { return 0.0f; },
    .sea = [] (float, float) { return false; },
    .current = [] (float, float) { return Vec3 (); },
  };
  // The floor inside, and the ramp falling from the sill to the ground.
  const float floor = at[1] + farm_model::barn_floor[1];
  const Vec3 middle = at + forward * -1.0f;
  MOPPE_CHECK_NEAR (*floor_at (plan, middle[0], middle[2]), floor, 1e-4f);
  float last = floor + 0.01f;
  for (float d = barn_front + 0.05f; d < barn_front + barn_ramp_length;
       d += 0.3f) {
    const Vec3 p = at + forward * d;
    const std::optional<float> h = floor_at (plan, p[0], p[2]);
    MOPPE_CHECK (h && *h < last);
    last = h.value_or (last);
  }
  // Its foot is set into the ground.
  MOPPE_CHECK (last < at[1] + 0.05f);
  const Vec3 yard = at + forward * (barn_front + barn_ramp_length + 1.0f);
  MOPPE_CHECK (!floor_at (plan, yard[0], yard[2]));

  test::RecordingRenderer renderer;
  PlacesLandscape landscape;
  landscape.rebuild (renderer, plan, ground);
  mov::TrunkField trunks;
  trunks.set_trunks (landscape.colliders (), 0.0f, 0.0f);
  trunks.focus (at);
  const auto walk_at = [&] (float aside) {
    GameSession session (world, surface);
    session.start_on_foot ();
    const Vec3 from = at + forward * 12.0f + left * aside;
    session.walker ().spawn (position (from), -forward);
    for (int i = 0; i < 60 * 8; ++i)
      advance_game_session (world,
                            surface,
                            session,
                            InputFrame { .drive = 0.5f },
                            seconds (1.0f / 60.0f),
                            &trunks);
    return session.walker ().position ();
  };
  const Vec3 in = walk_at (0.3f);
  MOPPE_CHECK (dot (in - at, forward) < barn_front - 0.5f);
  MOPPE_CHECK_NEAR (in[1], floor, 0.1f);
  const Vec3 out = walk_at (barn_door_half + 2.0f);
  MOPPE_CHECK (dot (out - at, forward) > barn_front);
}
