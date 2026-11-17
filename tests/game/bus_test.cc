#include <moppe/game/bus.hh>
#include <moppe/game/places.hh>

#include <tests/test.hh>

#include <cmath>
#include <string>
#include <vector>

using namespace moppe;
using namespace moppe::game;

namespace {
  // A kilometre square world, level at 10 m and dry, with the loop running
  // once round it along z = 500 eastward from the landing at x = 0, and
  // one road leaving the loop at x = 500 northward to a lake's outlet at
  // z = 900.
  constexpr float bus_test_period = 1000.0f;

  terrain::Road bus_test_loop () {
    terrain::Road loop;
    loop.loop = true;
    for (int i = 0; i <= 250; ++i) {
      loop.points.push_back ({ 4.0f * static_cast<float> (i), 500.0f });
      loop.heights.push_back (10.0f);
    }
    return loop;
  }

  PlaceGround bus_test_country () {
    return { .ground = [] (float, float) { return 10.0f; },
             .water = [] (float, float) { return 0.0f; },
             .sea = [] (float, float) { return false; },
             .current = [] (float, float) { return Vec3 (); },
             .sea_level = 0.0f,
             .period_x = bus_test_period,
             .period_z = bus_test_period };
  }

  terrain::RoadNetwork bus_test_network () {
    using Kind = terrain::RoadPlace::Kind;
    terrain::RoadNetwork network;
    network.places = {
      { .kind = Kind::Trailhead, .x_m = 200.0f, .z_m = 100.0f },
      { .kind = Kind::Landing, .x_m = 0.0f, .z_m = 500.0f },
      { .kind = Kind::LakeOutlet, .x_m = 500.0f, .z_m = 900.0f },
    };
    network.roads.push_back (bus_test_loop ());
    terrain::Road branch;
    for (int i = 0; i <= 100; ++i) {
      branch.points.push_back ({ 500.0f, 500.0f + 4.0f * i });
      branch.heights.push_back (10.0f);
    }
    network.roads.push_back (branch);
    return network;
  }
}

MOPPE_TEST (bus_stops_at_the_landing_and_past_each_branch_on_the_right) {
  const std::vector<std::string> names { "Granbodarna", "Gäddvik", "Laxfors" };
  const BusLine line = plan_bus_line (
    bus_test_network (), names, PlacesPlan {}, bus_test_country (), 7);
  MOPPE_CHECK (std::fabs (line.length ().numerical_value_in (u::m) - 1000.0f) <
               0.01f);
  MOPPE_CHECK (line.stops ().size () == 2);
  // The landing's at its start, and the branch's 30 m past it, named for
  // the outlet the branch leads to (400 m off, so not "vägskäl").
  const BusStop& landing = line.stops ()[0];
  const BusStop& branch = line.stops ()[1];
  MOPPE_CHECK (landing.name == "Gäddvik");
  MOPPE_CHECK (landing.along.numerical_value_in (u::m) < 1.0f);
  MOPPE_CHECK (branch.name == "Laxfors");
  MOPPE_CHECK (std::fabs (branch.along.numerical_value_in (u::m) - 530.0f) <
               1.0f);
  // Eastward along +x, the right is +z: the signs stand south of the
  // road's line, past the tread, the shelters further back.
  for (const BusStop& stop : line.stops ()) {
    MOPPE_CHECK (stop.sign[2] > 503.5f && stop.sign[2] < 506.0f);
    MOPPE_CHECK (stop.shelter.has_value () &&
                 (*stop.shelter)[2] > stop.sign[2] + 1.0f);
  }
}

MOPPE_TEST (buses_keep_their_timetable_round_the_loop) {
  // Stops at 0 and 400 m: each run gathers pace at 0.8 m/s² to 12.5 m/s,
  // which takes 15.625 s over 97.66 m, keeps it, and brakes as hard. 400 m
  // takes 400 / 12.5 + 15.625 = 47.625 s and 600 m 63.625 s; with 20 s
  // at each stop a round is 151.25 s, so one bus runs.
  BusStop first, second;
  first.name = "A";
  first.along = 0.0f * u::m;
  second.name = "B";
  second.along = 400.0f * u::m;
  const BusLine line (bus_test_loop (), { first, second });
  MOPPE_CHECK (line.buses () == 1);
  MOPPE_CHECK (std::fabs (seconds_value (line.round_trip ()) - 151.25f) <
               0.01f);

  // As play begins it stands at the first stop, in the right-hand lane.
  const BusPose start = line.where (play_time (0.0)).front ();
  MOPPE_CHECK (start.at_stop == std::optional<std::size_t> (0));
  MOPPE_CHECK (std::fabs (start.position[2] - 501.7f) < 0.01f);
  MOPPE_CHECK (start.forward[0] > 0.99f);
  // The second stop's board: due once the first stop's 20 s and the run
  // are done.
  const BusDue due = line.due (1, play_time (0.0));
  MOPPE_CHECK (!due.here);
  MOPPE_CHECK (std::fabs (due.wait.numerical_value_in (si::second) - 67.625) <
               0.01);
  // Halfway through the run, at its middle (the run is symmetric).
  const BusPose middle = line.where (play_time (20.0 + 47.625 / 2)).front ();
  MOPPE_CHECK (std::fabs (middle.along.numerical_value_in (u::m) - 200.0f) <
               0.05f);
  MOPPE_CHECK (std::fabs (speed_value (middle.speed) - 12.5f) < 0.01f);
  const BusDue there = line.due (1, play_time (70.0));
  MOPPE_CHECK (there.here);
  MOPPE_CHECK (std::fabs (there.wait.numerical_value_in (si::second) -
                          (67.625 + 20.0 - 70.0)) < 0.01);

  // Never faster than its pace nor jumping, across the stops and the
  // loop's seam, over two rounds; and the board counts down as it goes.
  float along = 0.0f;
  double was_due = due.wait.numerical_value_in (si::second);
  for (int k = 1; k <= 3025; ++k) {
    const double t = 0.1 * k;
    const BusPose pose = line.where (play_time (t)).front ();
    const float now = pose.along.numerical_value_in (u::m);
    float moved = now - along;
    if (moved < -500.0f)
      moved += 1000.0f;
    MOPPE_CHECK (moved >= -1e-3f && moved <= 1.25f + 1e-3f);
    MOPPE_CHECK (speed_value (pose.speed) <= 12.5f + 1e-3f);
    along = now;
    const BusDue board = line.due (1, play_time (t));
    if (!board.here) {
      const double wait = board.wait.numerical_value_in (si::second);
      MOPPE_CHECK (std::fabs (was_due - wait - 0.1) < 1e-3 || wait > was_due);
      was_due = wait;
    }
  }
}
