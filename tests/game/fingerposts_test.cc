#include <moppe/game/fingerposts.hh>
#include <moppe/game/place_names.hh>

#include <tests/test.hh>

#include <algorithm>
#include <cmath>
#include <set>
#include <string>

using namespace moppe;
using terrain::Road;
using terrain::RoadPlace;

namespace {
  // A cart road west to east with the trailhead 300 m west of a junction
  // and the landing 400 m east of it, and a footpath, long abandoned,
  // leaving the junction north for a sanctuary 350 m off.
  terrain::RoadNetwork tee () {
    terrain::RoadNetwork network;
    network.places = {
      { RoadPlace::Kind::Trailhead, 200.0f, 500.0f, 10.0f },
      { RoadPlace::Kind::Landing, 900.0f, 500.0f, 1.0f },
      { RoadPlace::Kind::Sanctuary, 500.0f, 150.0f, 80.0f },
    };
    Road road;
    for (float x = 200.0f; x <= 900.0f; x += 50.0f)
      road.points.push_back ({ x, 500.0f });
    Road path;
    path.kind = Road::Kind::Footpath;
    path.abandoned_years_ago = 120.0f;
    for (float z = 500.0f; z >= 150.0f; z -= 50.0f)
      path.points.push_back ({ 500.0f, z });
    network.roads = { road, path };
    return network;
  }
}

MOPPE_TEST (fingerposts_point_each_way_to_the_places_it_leads_to) {
  const auto posts = game::plan_fingerposts (tee (), 1000.0f, 1000.0f);
  MOPPE_CHECK (posts.size () == 1);
  const game::Fingerpost& post = posts.front ();
  MOPPE_CHECK_NEAR (post.junction_x_m, 500.0f, 1.0f);
  MOPPE_CHECK_NEAR (post.junction_z_m, 500.0f, 1.0f);
  // Off the tread, in the widest gap: south of the road.
  MOPPE_CHECK (post.z_m > 505.0f);
  MOPPE_CHECK (post.arms.size () == 3);
  int found = 0;
  for (const game::Fingerpost::Arm& arm : post.arms) {
    MOPPE_CHECK (arm.boards.size () == 1);
    const auto& board = arm.boards.front ();
    if (board.place == 0) {
      MOPPE_CHECK (arm.direction_x < -0.99f);
      MOPPE_CHECK_NEAR (board.metres, 300.0f, 1.0f);
      MOPPE_CHECK (!arm.abandoned && arm.kind == Road::Kind::Road);
    } else if (board.place == 1) {
      MOPPE_CHECK (arm.direction_x > 0.99f);
      MOPPE_CHECK_NEAR (board.metres, 400.0f, 1.0f);
    } else {
      MOPPE_CHECK (arm.direction_z < -0.99f);
      MOPPE_CHECK_NEAR (board.metres, 350.0f, 1.0f);
      MOPPE_CHECK (arm.abandoned && arm.kind == Road::Kind::Footpath);
    }
    found |= 1 << board.place;
  }
  MOPPE_CHECK (found == 7);
}

MOPPE_TEST (place_names_are_the_seeds_own_and_say_what_the_place_is) {
  terrain::RoadNetwork network;
  for (int i = 0; i < 30; ++i)
    network.places.push_back ({ RoadPlace::Kind::LakeOutlet });
  network.places.push_back ({ RoadPlace::Kind::Landing });
  const auto names = game::name_places (network, 123);
  MOPPE_CHECK (names == game::name_places (network, 123));
  MOPPE_CHECK (names != game::name_places (network, 124));
  MOPPE_CHECK (std::set<std::string> (names.begin (), names.end ()).size () ==
               names.size ());
  const auto ends = [] (const std::string& name, std::string_view end) {
    return name.size () > end.size () && name.ends_with (end);
  };
  for (int i = 0; i < 30; ++i)
    MOPPE_CHECK (ends (names[i], "kvarn") || ends (names[i], "fors") ||
                 ends (names[i], "forsen") || ends (names[i], "os") ||
                 ends (names[i], "ström"));
  const std::string& landing = names.back ();
  MOPPE_CHECK (ends (landing, "vik") || ends (landing, "viken") ||
               ends (landing, "hamn") || ends (landing, "näs") ||
               ends (landing, "sund") || ends (landing, "udden"));
  // A place keeps its name when another kind's places change.
  network.places.push_back ({ RoadPlace::Kind::Sanctuary });
  MOPPE_CHECK (game::name_places (network, 123)[0] == names[0]);
}
