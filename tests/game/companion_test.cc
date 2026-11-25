#include <moppe/environment.hh>
#include <moppe/game/companion.hh>
#include <moppe/game/companion_link.hh>
#include <moppe/game/companion_transport.hh>

#include <tests/test.hh>

#include <chrono>
#include <map>
#include <thread>
#include <vector>

using namespace moppe;
using namespace moppe::game;

namespace {
  CompanionSnapshot companion_test_walker (float x, double sent) {
    CompanionSnapshot snapshot;
    snapshot.player = 7;
    snapshot.sent = link_time (sent);
    snapshot.world = world_tag ("play-2048-123");
    snapshot.day_hours = clock_time (9.25);
    snapshot.mode = Doing::on_foot ();
    snapshot.fleet.bike.position = position (Vec3 (10, 20, 30));
    WalkerPose walker;
    walker.position = position (Vec3 (x, 20, 30));
    walker.stride_phase = 0.9f * proportion[one];
    snapshot.walker = walker;
    return snapshot;
  }
}

MOPPE_TEST (a_companion_snapshot_survives_the_wire) {
  CompanionSnapshot sent = companion_test_walker (12.5f, 4.0);
  sent.sequence = 99;
  sent.walker->heading = unit_direction (Vec3 (1, 0, 0));
  sent.walker->grounded = false;
  sent.walker->swimming = true;
  sent.walker->look_pitch = -0.3f * inclination[u::rad];
  sent.fleet.bike.wheel_spin = 123.0f * u::rad;
  sent.hung = true;
  sent.hammock.strap[0] = position (Vec3 (1, 2, 3));
  sent.hammock.strap[1] = position (Vec3 (5, 2, 3));
  sent.hammock.girth[1] = 0.2f * u::m;
  sent.hammock_load = 0.75f * proportion[one];
  sent.hearth_laid = true;
  sent.hearth = position (Vec3 (3, 2, 5));
  sent.fire_burn = 0.5f * proportion[one];
  sent.beacons = 0b101;
  sent.pan_on = true;
  sent.pan_place = position (Vec3 (3.3f, 2.2f, 5));
  sent.pan_cooked = 0.8f * proportion[one];
  sent.pan_count = 2;
  sent.pan_mushrooms[1] = 77;
  sent.pick_used = 4;
  sent.pick_count = 40;
  sent.picks_from = 38;
  sent.picks = { 1001, 1002 };
  sent.fleet.boat.position = position (Vec3 (40, 0, 9));
  sent.fleet.boat.velocity = velocity (Vec3 (0, 0, 6.5f));
  sent.bus_clock = play_time (4321.25);
  sent.bus = 1;
  sent.bus_seat = 1;
  sent.fleet.jet_ski.position = position (Vec3 (60, 0.1f, -14));
  sent.fleet.jet_ski.steer = -0.3f * u::rad;
  sent.fleet.jet_ski.lean = 0.25f * u::rad;
  sent.fleet.jet_ski.velocity = velocity (Vec3 (-15, 0, 2));
  sent.fleet.tractor.trailer_position = position (Vec3 (-20, 3, 8));
  sent.fleet.tractor.trailer_forward = unit_direction (Vec3 (0.6f, 0, 0.8f));
  sent.fleet.tractor.trailer_wheel_drop = { 0.02f * u::m, -0.04f * u::m };
  sent.fleet.tractor.trailer_spin = 1.5f * u::rad;
  sent.play_time = play_time (123.5);
  sent.world_info = {
    .tag = sent.world,
    .seed = 4321,
    .resolution = 2048,
    .extent_hm = 100,
  };
  sent.ride_target = 0x1122334455667788ULL;
  sent.sling_hooked = true;
  sent.sling_load = Machine::jeep;
  sent.sling_taut = true;
  sent.helm_rod = RodPose {
    .raise = 0.7f * inclination[u::rad],
    .bend = 0.65f * proportion[one],
    .water = position (Vec3 (55, 0, 12)),
    .out = 0.9f * proportion[one],
    .arc = 1.2f * u::m,
    .tight = 0.8f * proportion[one],
    .fish_on = true,
    .fish = { .kind = FishKind::perch, .length = 0.34f * u::m },
    .wriggle = 0.4f * game::wriggle[one],
  };
  sent.walker->rope_end = position (Vec3 (14.5f, 20.5f, 30.2f));
  sent.walker->rod = RodPose {
    .raise = 0.6f * inclination[u::rad],
    .bend = 0.35f * proportion[one],
    .water = position (Vec3 (22.0f, 19.5f, 31.0f)),
    .out = 1.0f * proportion[one],
    .arc = 0.5f * u::m,
    .tight = 0.5f * proportion[one],
  };
  sent.pan_has_fish = true;
  sent.pan_fish = { .kind = FishKind::pike, .length = 0.62f * u::m };
  sent.pan_eggs = 2;
  sent.bus_seat = 7;

  const std::vector<std::uint8_t> full_bytes = encode (sent);
  // Even a full-fleet packet stays comfortably below a 1200 B safe UDP MTU.
  MOPPE_CHECK (full_bytes.size () < 1200);
  const std::optional<CompanionSnapshot> got = decode (full_bytes);
  MOPPE_CHECK (got.has_value ());
  if (!got)
    return;
  MOPPE_CHECK (got->player == 7 && got->sequence == 99);
  MOPPE_CHECK (got->world == sent.world && got->day_hours == clock_time (9.25));
  MOPPE_CHECK (got->play_time == play_time (123.5));
  MOPPE_CHECK (got->world_info.seed == 4321 &&
               got->world_info.resolution == 2048 &&
               got->world_info.extent_hm == 100);
  MOPPE_CHECK (got->ride_target == 0x1122334455667788ULL);
  MOPPE_CHECK (got->sling_hooked && got->sling_load == Machine::jeep &&
               got->sling_taut);
  MOPPE_CHECK (got->helm_rod.has_value () && got->helm_rod->fish_on);
  MOPPE_CHECK_NEAR (got->helm_rod->bend.numerical_value_in (one), 0.65f, 1e-6f);
  MOPPE_CHECK_NEAR (position_value (got->helm_rod->water)[0], 55.0f, 1e-6f);
  MOPPE_CHECK (got->helm_rod->fish.kind == FishKind::perch);
  MOPPE_CHECK (got->mode == Doing::on_foot () && !got->glider && got->walker);
  MOPPE_CHECK (got->walker->rope_end.has_value ());
  MOPPE_CHECK_NEAR (position_value (*got->walker->rope_end)[0], 14.5f, 1e-6f);
  MOPPE_CHECK (got->walker->rod.has_value ());
  MOPPE_CHECK_NEAR (
    got->walker->rod->bend.numerical_value_in (one), 0.35f, 1e-6f);
  MOPPE_CHECK (got->pan_has_fish && got->pan_fish.kind == FishKind::pike);
  MOPPE_CHECK_NEAR (meters_value (got->pan_fish.length), 0.62f, 1e-6f);
  MOPPE_CHECK (got->pan_eggs == 2);
  MOPPE_CHECK_NEAR (position_value (got->walker->position)[0], 12.5f, 1e-6f);
  MOPPE_CHECK_NEAR (direction_value (got->walker->heading)[0], 1.0f, 1e-6f);
  MOPPE_CHECK (!got->walker->grounded);
  MOPPE_CHECK (got->walker->swimming);
  MOPPE_CHECK_NEAR (
    got->walker->look_pitch.numerical_value_in (u::rad), -0.3f, 1e-6f);
  MOPPE_CHECK_NEAR (radians_value (got->fleet.bike.wheel_spin), 123.0f, 1e-6f);
  MOPPE_CHECK (got->hung && got->lying_down ());
  MOPPE_CHECK_NEAR (position_value (got->hammock.strap[1])[0], 5.0f, 1e-6f);
  MOPPE_CHECK_NEAR (meters_value (got->hammock.girth[1]), 0.2f, 1e-6f);
  MOPPE_CHECK (got->hearth_laid);
  MOPPE_CHECK_NEAR (got->fire_burn.numerical_value_in (one), 0.5f, 1e-6f);
  MOPPE_CHECK (got->beacons == 0b101);
  MOPPE_CHECK (got->pan_on && got->pan_count == 2);
  MOPPE_CHECK (got->pan_mushrooms[1] == 77 && got->pick_used == 4);
  MOPPE_CHECK_NEAR (got->pan_cooked.numerical_value_in (one), 0.8f, 1e-6f);
  MOPPE_CHECK_NEAR (position_value (got->pan_place)[0], 3.3f, 1e-6f);
  MOPPE_CHECK (got->pick_count == 40 && got->picks_from == 38);
  MOPPE_CHECK (got->picks == sent.picks);
  MOPPE_CHECK_NEAR (position_value (got->fleet.boat.position)[0], 40.0f, 1e-6f);
  MOPPE_CHECK_NEAR (velocity_value (got->fleet.boat.velocity)[2], 6.5f, 1e-6f);
  MOPPE_CHECK (got->bus_clock == play_time (4321.25) && got->bus == 1 &&
               got->bus_seat == 7);
  MOPPE_CHECK_NEAR (
    position_value (got->fleet.jet_ski.position)[2], -14.0f, 1e-6f);
  MOPPE_CHECK_NEAR (radians_value (got->fleet.jet_ski.steer), -0.3f, 1e-6f);
  MOPPE_CHECK_NEAR (radians_value (got->fleet.jet_ski.lean), 0.25f, 1e-6f);
  MOPPE_CHECK_NEAR (
    velocity_value (got->fleet.jet_ski.velocity)[0], -15.0f, 1e-6f);
  MOPPE_CHECK_NEAR (
    position_value (got->fleet.tractor.trailer_position)[0], -20.0f, 1e-6f);
  MOPPE_CHECK_NEAR (
    direction_value (got->fleet.tractor.trailer_forward)[0], 0.6f, 1e-6f);
  MOPPE_CHECK_NEAR (
    meters_value (got->fleet.tractor.trailer_wheel_drop[1]), -0.04f, 1e-6f);
  MOPPE_CHECK_NEAR (
    radians_value (got->fleet.tractor.trailer_spin), 1.5f, 1e-6f);

  // Partial fleet delta stays compact and merges cleanly into cached poses.
  CompanionSnapshot delta = sent;
  delta.fleet_mask = (1u << static_cast<unsigned> (Machine::boat));
  delta.fleet.boat.position = position (Vec3 (88, 0, -4));
  const std::vector<std::uint8_t> delta_bytes = encode (delta);
  MOPPE_CHECK (delta_bytes.size () < 600);
  const std::optional<CompanionSnapshot> decoded_delta = decode (delta_bytes);
  MOPPE_CHECK (decoded_delta.has_value ());
  if (decoded_delta) {
    FleetPoses merged = got->fleet;
    merge_fleet_poses (merged, decoded_delta->fleet, decoded_delta->fleet_mask);
    MOPPE_CHECK_NEAR (position_value (merged.boat.position)[0], 88.0f, 1e-6f);
    MOPPE_CHECK_NEAR (position_value (merged.bike.position)[0], 10.0f, 1e-6f);
  }

  // Bus seat out of 0..7 range is rejected.
  CompanionSnapshot bad_seat = sent;
  bad_seat.bus_seat = bus_passenger_seats;
  MOPPE_CHECK (!decode (encode (bad_seat)));

  // Riding along, in which of the other player's machines.
  sent.mode = Doing::riding_along (Machine::helicopter);
  sent.walker.reset ();
  sent.hung = false;
  const std::optional<CompanionSnapshot> riding = decode (encode (sent));
  MOPPE_CHECK (riding &&
               riding->mode == Doing::riding_along (Machine::helicopter));
}

MOPPE_TEST (a_damaged_companion_snapshot_is_refused) {
  const std::vector<std::uint8_t> bytes =
    encode (companion_test_walker (1.0f, 1.0));
  // Cut short anywhere, or with something after it.
  for (std::size_t size = 0; size < bytes.size (); ++size)
    MOPPE_CHECK (!decode (std::span (bytes.data (), size)));
  std::vector<std::uint8_t> longer = bytes;
  longer.push_back (0);
  MOPPE_CHECK (!decode (longer));
  // Another version of the wire.
  std::vector<std::uint8_t> other = bytes;
  other[4] ^= 0xff;
  MOPPE_CHECK (!decode (other));
  // A position that is not a number.
  CompanionSnapshot broken = companion_test_walker (1.0f, 1.0);
  broken.fleet.bike.position = position (Vec3 (std::nanf (""), 0, 0));
  MOPPE_CHECK (!decode (encode (broken)));
}

MOPPE_TEST (every_pick_is_told_in_the_end) {
  const std::size_t picked = 300;
  std::vector<bool> told (picked, false);
  for (std::uint32_t sequence = 0; sequence < 64; ++sequence) {
    const std::uint32_t from = companion_pick_window_start (picked, sequence);
    MOPPE_CHECK (from + companion_pick_window <= picked);
    // Every other snapshot carries the newest.
    if (sequence % 2 == 0)
      MOPPE_CHECK (from + companion_pick_window == picked);
    for (std::size_t i = 0; i < companion_pick_window; ++i)
      told[from + i] = true;
  }
  for (const bool each : told)
    MOPPE_CHECK (each);
  MOPPE_CHECK (companion_pick_window_start (5, 3) == 0);
}

namespace {
  // A companion walking ten metres a second along x, heard from every
  // thirtieth of a second except in `silence`, and drawn sixty times a
  // second for `seconds`. Returns where they were drawn each frame.
  struct CompanionPlayback {
    CompanionTrack track;
    std::vector<float> drawn;
    double now = 100.0;
    int told = 0;

    void play (double seconds, double silent_from = 1e9, double until = 0) {
      const int frames = static_cast<int> (seconds * 60.0 + 0.5);
      for (int frame = 0; frame < frames; ++frame) {
        now += 1.0 / 60.0;
        const double theirs = now - 100.0;
        // Late word comes all at once.
        const bool silent = theirs >= silent_from && theirs < until;
        while (!silent && told / 30.0 <= theirs) {
          track.push (companion_test_walker (static_cast<float> (told / 3.0),
                                             told / 30.0),
                      link_time (now));
          ++told;
        }
        const std::optional<CompanionSnapshot> seen =
          track.advance (link_time (now), 1.0 / 60.0 * si::second);
        MOPPE_CHECK (seen && seen->walker);
        if (seen && seen->walker)
          drawn.push_back (position_value (seen->walker->position)[0]);
      }
    }
  };
}

MOPPE_TEST (a_companion_is_drawn_evenly_a_moment_behind) {
  CompanionPlayback playback;
  playback.play (4.0);
  // A steady link is followed closely.
  const double delay = playback.track.delay ().numerical_value_in (si::second);
  MOPPE_CHECK (delay >= 0.05 && delay < 0.09);
  MOPPE_CHECK_NEAR (
    playback.drawn.back (), static_cast<float> ((4.0 - delay) * 10.0), 0.25f);
  // Once settled every frame moves them as far as the last, though
  // snapshots come only with every other one.
  const float pace = 10.0f / 60.0f;
  for (std::size_t i = 180; i < playback.drawn.size (); ++i)
    MOPPE_CHECK_NEAR (
      playback.drawn[i] - playback.drawn[i - 1], pace, 0.02f * pace);
}

MOPPE_TEST (a_companion_does_not_jump_when_late_word_comes) {
  CompanionPlayback playback;
  playback.play (3.0, 2.0, 2.3);
  // Through the silence they are carried on, slowing; when word comes
  // they make up the ground without going back or leaping ahead.
  const float pace = 10.0f / 60.0f;
  for (std::size_t i = 120; i < playback.drawn.size (); ++i) {
    const float step = playback.drawn[i] - playback.drawn[i - 1];
    MOPPE_CHECK (step > -1e-4f && step < 2.5f * pace);
  }
  // An uneven link is followed from farther back.
  MOPPE_CHECK (playback.track.delay () > 0.2 * si::second);
}

MOPPE_TEST (a_silent_companion_stands_and_then_is_gone) {
  CompanionPlayback playback;
  playback.play (1.0);
  const float last = static_cast<float> ((playback.told - 1) / 3.0);
  CompanionTrack& track = playback.track;
  double now = playback.now;
  float before = playback.drawn.back ();
  for (int frame = 0; frame < 120; ++frame) {
    now += 1.0 / 60.0;
    const std::optional<CompanionSnapshot> seen =
      track.advance (link_time (now), 1.0 / 60.0 * si::second);
    MOPPE_CHECK (seen && seen->walker);
    if (!seen || !seen->walker)
      return;
    MOPPE_CHECK (position_value (seen->walker->position)[0] >= before - 1e-4f);
    before = position_value (seen->walker->position)[0];
  }
  // Carried on a little past the last word, and no farther.
  MOPPE_CHECK (before > last + 1.0f && before < last + 2.1f);
  MOPPE_CHECK (track.present (link_time (now)));
  MOPPE_CHECK (!track.present (link_time (now + 3.0)));
  MOPPE_CHECK (!track.advance (link_time (now + 3.0), 1.0 / 60.0 * si::second));
}

MOPPE_TEST (a_companion_rounds_the_corners_between_snapshots) {
  // Gathering speed: x = t * t, told at t = 0, 1, 2, 3.
  CompanionSnapshot told[4];
  for (int i = 0; i < 4; ++i)
    told[i] = companion_test_walker (static_cast<float> (i * i), 0.1 * i);
  // Straight between the middle two is 2.5; the truth is 2.25.
  MOPPE_CHECK_NEAR (
    position_value (
      blend (told[1], told[2], 0.5f * blend_weight[one]).walker->position)[0],
    2.5f,
    1e-4f);
  MOPPE_CHECK_NEAR (
    position_value (
      blend (&told[0], told[1], told[2], &told[3], 0.5f * blend_weight[one])
        .walker->position)[0],
    2.25f,
    1e-4f);
}

MOPPE_TEST (a_companion_does_not_slide_across_a_leap) {
  const CompanionSnapshot here = companion_test_walker (0.0f, 0.0);
  CompanionSnapshot there = companion_test_walker (500.0f, 0.1);
  const CompanionSnapshot seen = blend (here, there, 0.5f * blend_weight[one]);
  MOPPE_CHECK_NEAR (position_value (seen.walker->position)[0], 500.0f, 1e-3f);
  // The stride turns the short way round its cycle.
  CompanionSnapshot stepped = companion_test_walker (0.5f, 0.1);
  stepped.walker->stride_phase = 0.1f * proportion[one];
  MOPPE_CHECK_NEAR (blend (here, stepped, 0.5f * blend_weight[one])
                      .walker->stride_phase.numerical_value_in (one),
                    0.0f,
                    1e-4f);
}

MOPPE_TEST (two_links_on_one_machine_find_each_other) {
  std::string error;
  const std::unique_ptr<CompanionLink> first =
    CompanionLink::open ({ .port = 47960 }, error);
  if (!first)
    return; // no sockets here
  const std::unique_ptr<CompanionLink> second = CompanionLink::open (
    { .address = "127.0.0.1:" + std::to_string (first->port ()),
      .port = 47961 },
    error);
  MOPPE_CHECK (second != nullptr);
  if (!second)
    return;
  MOPPE_CHECK (first->player () != second->player ());
  const std::uint32_t world = companion_test_walker (0.0f, 0.0).world;
  first->set_world (world);
  second->set_world (world);

  // Someone in another world calling first is heard of but not taken
  // for the companion.
  std::vector<CompanionSnapshot> at_first, at_second;
  {
    const std::unique_ptr<CompanionLink> stranger = CompanionLink::open (
      { .address = "127.0.0.1:" + std::to_string (first->port ()),
        .port = 47962 },
      error);
    MOPPE_CHECK (stranger != nullptr);
    CompanionSnapshot elsewhere = companion_test_walker (3.0f, 0.0);
    elsewhere.world = world + 1;
    bool heard = false;
    for (int round = 0; round < 100 && !heard; ++round) {
      stranger->send (elsewhere, stranger->now ());
      std::this_thread::sleep_for (std::chrono::milliseconds (5));
      first->receive (at_first);
      heard = first->heard_elsewhere ();
    }
    MOPPE_CHECK (heard && at_first.empty ());
  }
  for (int round = 0; round < 100 && (at_first.empty () || at_second.empty ());
       ++round) {
    second->send (companion_test_walker (2.0f, 0.0), second->now ());
    // The first answers whoever called.
    first->receive (at_first);
    if (!at_first.empty ())
      first->send (companion_test_walker (1.0f, 0.0), first->now ());
    second->receive (at_second);
    std::this_thread::sleep_for (std::chrono::milliseconds (5));
  }
  MOPPE_CHECK (!at_first.empty () && !at_second.empty ());
  if (at_first.empty () || at_second.empty ())
    return;
  MOPPE_CHECK (at_first.front ().player == second->player ());
  MOPPE_CHECK (at_second.front ().player == first->player ());
  MOPPE_CHECK_NEAR (
    position_value (at_first.front ().walker->position)[0], 2.0f, 1e-6f);
  MOPPE_CHECK (!first->companion_address ().empty ());
}

MOPPE_TEST (companion_blend_isolates_leaps_and_smooths_all_machines) {
  // When the bike teleports 500 m, the jeep and walker still blend smoothly.
  CompanionSnapshot a = companion_test_walker (10.0f, 0.0);
  a.fleet.bike.position = position (Vec3 (0, 20, 0));
  a.fleet.jeep.position = position (Vec3 (10, 20, 0));
  a.walker->rope_end = position (Vec3 (12, 20, 0));
  a.walker->rod = RodPose {
    .bend = 0.2f * proportion[one],
    .water = position (Vec3 (20, 19, 0)),
  };

  CompanionSnapshot b = a;
  b.sent = link_time (0.1);
  b.fleet.bike.position = position (Vec3 (500, 20, 0));
  b.fleet.jeep.position = position (Vec3 (20, 20, 0));
  b.walker->position = position (Vec3 (14, 20, 30));
  b.walker->rope_end = position (Vec3 (16, 20, 0));
  b.walker->rod->bend = 0.6f * proportion[one];
  b.walker->rod->water = position (Vec3 (24, 19, 0));

  const CompanionSnapshot mid = blend (a, b, 0.5f * blend_weight[one]);
  MOPPE_CHECK_NEAR (position_value (mid.fleet.bike.position)[0], 500.0f, 1e-3f);
  MOPPE_CHECK_NEAR (position_value (mid.fleet.jeep.position)[0], 15.0f, 1e-3f);
  MOPPE_CHECK_NEAR (position_value (mid.walker->position)[0], 12.0f, 1e-3f);
  MOPPE_CHECK_NEAR (position_value (*mid.walker->rope_end)[0], 14.0f, 1e-3f);
  MOPPE_CHECK (mid.walker->rod.has_value ());
  MOPPE_CHECK_NEAR (
    mid.walker->rod->bend.numerical_value_in (one), 0.4f, 1e-3f);
  MOPPE_CHECK_NEAR (position_value (mid.walker->rod->water)[0], 22.0f, 1e-3f);

  // Slip smoothing covers jeep and snowmobile (not just the first 4 actors).
  CompanionTrack track;
  link_time_t now = link_time (50.0);
  const link_time_t origin = now;
  const duration_t frame_dt = (1.0 / 60.0) * si::second;
  int told = 0;
  std::vector<meters_t> jeep_x;
  for (int frame = 0; frame < 180; ++frame) {
    now += frame_dt;
    const duration_t t = now - origin;
    const bool silent = t >= 1.5 * si::second && t < 1.8 * si::second;
    while (!silent && (told / 30.0) * si::second <= t) {
      CompanionSnapshot s = companion_test_walker (0.0f, told / 30.0);
      s.mode = Doing::driving (Machine::jeep);
      s.walker.reset ();
      s.fleet.jeep.position =
        position (Vec3 (static_cast<float> (told / 3.0), 10, 0));
      track.push (std::move (s), now);
      ++told;
    }
    const std::optional<CompanionSnapshot> seen = track.advance (now, frame_dt);
    MOPPE_CHECK (seen.has_value ());
    if (seen)
      jeep_x.push_back (
        static_cast<float> (position_value (seen->fleet.jeep.position)[0]) *
        u::m);
  }
  const meters_t pace = (10.0f / 60.0f) * u::m;
  for (std::size_t i = 90; i < jeep_x.size (); ++i) {
    const meters_t step = jeep_x[i] - jeep_x[i - 1];
    MOPPE_CHECK (step > -1e-4f * u::m && step < 2.5f * pace);
  }
}

MOPPE_TEST (companion_rebase_preserves_coupled_parts_across_torus_seams) {
  const plan_extent_t period =
    Vec2 (1000.0f, 1000.0f) * spatial_extent[si::metre];
  CompanionSnapshot snap = companion_test_walker (4.0f, 1.0);
  snap.fleet.tractor.position = position (Vec3 (5.0f, 10.0f, 5.0f));
  snap.fleet.tractor.trailer_position = position (Vec3 (1.0f, 10.0f, 5.0f));
  snap.hung = true;
  snap.hammock.strap[0] = position (Vec3 (998.0f, 12.0f, 5.0f));
  snap.hammock.strap[1] = position (Vec3 (2.0f, 12.0f, 5.0f));
  snap.walker->position = position (Vec3 (4.0f, 10.0f, 5.0f));
  snap.walker->rope_end = position (Vec3 (1.0f, 10.0f, 5.0f));
  snap.walker->rod = RodPose { .water = position (Vec3 (997.0f, 10.0f, 5.0f)) };

  rebase_snapshot (snap, position (Vec3 (995.0f, 10.0f, 5.0f)), period);
  MOPPE_CHECK_NEAR (
    position_value (snap.fleet.tractor.position)[0], 1005.0f, 1e-3f);
  MOPPE_CHECK_NEAR (
    position_value (snap.fleet.tractor.trailer_position)[0], 1001.0f, 1e-3f);
  MOPPE_CHECK_NEAR (position_value (snap.hammock.strap[0])[0], 998.0f, 1e-3f);
  MOPPE_CHECK_NEAR (position_value (snap.hammock.strap[1])[0], 1002.0f, 1e-3f);
  MOPPE_CHECK_NEAR (position_value (snap.walker->position)[0], 1004.0f, 1e-3f);
  MOPPE_CHECK_NEAR (position_value (*snap.walker->rope_end)[0], 1001.0f, 1e-3f);
  MOPPE_CHECK_NEAR (position_value (snap.walker->rod->water)[0], 997.0f, 1e-3f);
}

MOPPE_TEST (world_mismatch_explains_the_difference) {
  const WorldSummary ours {
    .tag = 111, .seed = 123, .resolution = 4096, .extent_hm = 100
  };
  WorldSummary diff_seed = ours;
  diff_seed.tag = 222;
  diff_seed.seed = 999;
  const std::string seed_msg = describe_world_mismatch (ours, diff_seed);
  MOPPE_CHECK (seed_msg.find ("999") != std::string::npos);
  MOPPE_CHECK (seed_msg.find ("123") != std::string::npos);

  WorldSummary diff_res = ours;
  diff_res.tag = 333;
  diff_res.resolution = 2048;
  MOPPE_CHECK (describe_world_mismatch (ours, diff_res).find ("2048") !=
               std::string::npos);

  WorldSummary diff_ext = ours;
  diff_ext.tag = 444;
  diff_ext.extent_hm = 50;
  MOPPE_CHECK (describe_world_mismatch (ours, diff_ext).find ("5.0 km") !=
               std::string::npos);

  WorldSummary diff_build = ours;
  diff_build.tag = 555;
  MOPPE_CHECK (describe_world_mismatch (ours, diff_build).find ("terrain") !=
               std::string::npos);
}

MOPPE_TEST (relay_url_parsing_and_room_shorthand) {
  std::string err;
  const std::optional<RelayUrl> plain =
    parse_relay_url ("ws://127.0.0.1:27960/room/birch", err);
  MOPPE_CHECK (plain.has_value ());
  if (plain) {
    MOPPE_CHECK (!plain->secure);
    MOPPE_CHECK (plain->host == "127.0.0.1");
    MOPPE_CHECK (plain->port == 27960);
    MOPPE_CHECK (plain->path == "/room/birch");
  }
  const std::optional<RelayUrl> tls =
    parse_relay_url ("wss://relay.moppe.example/room/fjord-42", err);
  MOPPE_CHECK (tls.has_value ());
  if (tls) {
    MOPPE_CHECK (tls->secure);
    MOPPE_CHECK (tls->port == 443);
    MOPPE_CHECK (tls->path == "/room/fjord-42");
  }
  MOPPE_CHECK (!parse_relay_url ("http://example.com/room/x", err));
  MOPPE_CHECK (!parse_relay_url ("127.0.0.1:27960", err));

  moppe::set_environment ("MOPPE_RELAY_URL", "wss://play.example/moppe");
  MOPPE_CHECK (expand_multiplayer_target ("room:pine-12") ==
               "wss://play.example/moppe/room/pine-12");
  moppe::set_environment ("MOPPE_RELAY_URL", nullptr);
  MOPPE_CHECK (expand_multiplayer_target ("room:pine-12") ==
               "ws://127.0.0.1:27960/room/pine-12");

  std::uint32_t ip = 0;
  std::uint16_t port = 0;
  MOPPE_CHECK (parse_ipv4_endpoint ({ "192.168.1.42:27965" }, ip, port));
  MOPPE_CHECK (port == 27965);
  MOPPE_CHECK (format_ipv4_endpoint (ip, port).key == "192.168.1.42:27965");
}

MOPPE_TEST (memory_hub_n_peers_with_loss_jitter_and_world_switch) {
  MemoryHub::Profile profile;
  profile.latency = 0.035 * si::second;
  profile.jitter = 0.012 * si::second;
  profile.loss = 0.15f * proportion[one];
  profile.seed = 42;
  MemoryHub hub (profile);

  const WorldSummary world { .tag = world_tag ("hub-test-123"),
                             .seed = 123,
                             .resolution = 2048,
                             .extent_hm = 100 };
  std::vector<std::unique_ptr<CompanionLink>> links;
  for (int i = 0; i < 4; ++i) {
    auto link = CompanionLink::attach (
      hub.join ("peer-" + std::to_string (i)),
      [&hub] () { return hub.now (); },
      static_cast<std::uint64_t> (100 + i));
    link->set_world (world);
    links.push_back (std::move (link));
  }

  std::vector<std::map<std::uint64_t, CompanionSnapshot>> latest (4);
  for (int step = 0; step < 180; ++step) {
    hub.advance (1.0 / 60.0 * si::second);
    for (int i = 0; i < 4; ++i) {
      if (links[i]->due ()) {
        CompanionSnapshot s =
          companion_test_walker (static_cast<float> (i * 10 + step * 0.1f), 0);
        s.fleet.boat.position =
          position (Vec3 (static_cast<float> (200 + i * 25), 0, 10));
        links[i]->send (std::move (s), hub.now ());
      }
      std::vector<CompanionSnapshot> got;
      links[i]->receive (got);
      for (const CompanionSnapshot& s : got)
        latest[i][s.player] = s;
    }
  }

  for (int i = 0; i < 4; ++i) {
    MOPPE_CHECK (links[i]->peer_count () == 3);
    MOPPE_CHECK (latest[i].size () == 3);
    for (int j = 0; j < 4; ++j) {
      if (i == j)
        continue;
      const auto it = latest[i].find (static_cast<std::uint64_t> (100 + j));
      MOPPE_CHECK (it != latest[i].end ());
      if (it != latest[i].end ())
        MOPPE_CHECK_NEAR (position_value (it->second.fleet.boat.position)[0],
                          static_cast<float> (200 + j * 25),
                          1e-3f);
    }
  }

  // Fast reconnect: peer 3 restarts from the same endpoint with a new ID;
  // peers immediately evict old ID 103 instead of duplicating it.
  links[3].reset ();
  links[3] = CompanionLink::attach (
    hub.join ("peer-3"), [&hub] () { return hub.now (); }, 203);
  links[3]->set_world (world);
  for (int step = 0; step < 90; ++step) {
    hub.advance ((1.0 / 60.0) * si::second);
    for (int i = 0; i < 4; ++i) {
      if (links[i]->due ())
        links[i]->send (companion_test_walker (static_cast<float> (i), 0),
                        hub.now ());
      std::vector<CompanionSnapshot> got;
      links[i]->receive (got);
    }
  }
  MOPPE_CHECK (links[0]->peer_count () == 3);
  MOPPE_CHECK (!links[0]->address_of (203).empty ());
  MOPPE_CHECK (links[0]->address_of (103).empty ());

  // Switching world clears peers and reports the others as elsewhere.
  WorldSummary other_world = world;
  other_world.tag = world_tag ("hub-test-999");
  other_world.seed = 999;
  links[0]->set_world (other_world);
  MOPPE_CHECK (links[0]->peer_count () == 0);
  bool heard_elsewhere = false;
  for (int step = 0; step < 60 && !heard_elsewhere; ++step) {
    hub.advance (1.0 / 60.0 * si::second);
    for (int i = 1; i < 4; ++i)
      if (links[i]->due ())
        links[i]->send (companion_test_walker (5.0f, 0), hub.now ());
    std::vector<CompanionSnapshot> got;
    links[0]->receive (got);
    MOPPE_CHECK (got.empty ());
    heard_elsewhere = links[0]->heard_elsewhere ();
  }
  MOPPE_CHECK (heard_elsewhere);
  const std::optional<WorldSummary> other = links[0]->elsewhere_world ();
  MOPPE_CHECK (other && other->seed == 123);
}

MOPPE_TEST (udp_three_peers_form_a_direct_mesh) {
  std::string error;
  const std::unique_ptr<CompanionLink> a =
    CompanionLink::open ({ .port = 47970 }, error);
  if (!a)
    return;
  const std::string addr_a = "127.0.0.1:" + std::to_string (a->port ());
  const std::unique_ptr<CompanionLink> b =
    CompanionLink::open ({ .address = addr_a, .port = 47971 }, error);
  const std::unique_ptr<CompanionLink> c =
    CompanionLink::open ({ .address = addr_a, .port = 47972 }, error);
  MOPPE_CHECK (b != nullptr && c != nullptr);
  if (!b || !c)
    return;

  const WorldSummary world { .tag = world_tag ("mesh-test-123"),
                             .seed = 123,
                             .resolution = 2048,
                             .extent_hm = 100 };
  a->set_world (world);
  b->set_world (world);
  c->set_world (world);

  bool b_saw_c = false;
  bool c_saw_b = false;
  for (int round = 0; round < 160 && !(b_saw_c && c_saw_b); ++round) {
    a->send (companion_test_walker (1.0f, 0.0), a->now ());
    b->send (companion_test_walker (2.0f, 0.0), b->now ());
    c->send (companion_test_walker (3.0f, 0.0), c->now ());
    std::vector<CompanionSnapshot> got_a, got_b, got_c;
    a->receive (got_a);
    b->receive (got_b);
    c->receive (got_c);
    for (const CompanionSnapshot& s : got_b)
      if (s.player == c->player ())
        b_saw_c = true;
    for (const CompanionSnapshot& s : got_c)
      if (s.player == b->player ())
        c_saw_b = true;
    std::this_thread::sleep_for (std::chrono::milliseconds (5));
  }
  MOPPE_CHECK (b_saw_c);
  MOPPE_CHECK (c_saw_b);
  MOPPE_CHECK (a->peer_count () == 2);
}
