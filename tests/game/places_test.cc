#include <moppe/game/farm_model.hh>
#include <moppe/game/places.hh>

#include <tests/test.hh>

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
