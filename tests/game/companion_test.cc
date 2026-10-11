#include <moppe/game/companion.hh>
#include <moppe/game/companion_link.hh>

#include <tests/test.hh>

#include <chrono>
#include <thread>
#include <vector>

using namespace moppe;
using namespace moppe::game;

namespace {
  CompanionSnapshot companion_test_walker (float x, double sent) {
    CompanionSnapshot snapshot;
    snapshot.player = 7;
    snapshot.sent = sent;
    snapshot.world = world_tag (123, 2048, 0);
    snapshot.day_hours = 9.25;
    snapshot.mode = M_FOOT;
    snapshot.bike.position = Vec3 (10, 20, 30);
    WalkerPose walker;
    walker.position = Vec3 (x, 20, 30);
    walker.stride_phase = 0.9f;
    snapshot.walker = walker;
    return snapshot;
  }
}

MOPPE_TEST (a_companion_snapshot_survives_the_wire) {
  CompanionSnapshot sent = companion_test_walker (12.5f, 4.0);
  sent.sequence = 99;
  sent.walker->heading = Vec3 (1, 0, 0);
  sent.walker->grounded = false;
  sent.walker->look_pitch = -0.3f;
  sent.bike.wheel_spin_radians = 123.0f;
  sent.hung = true;
  sent.hammock.strap[0] = Vec3 (1, 2, 3);
  sent.hammock.strap[1] = Vec3 (5, 2, 3);
  sent.hammock.girth[1] = 0.2f;
  sent.hammock_load = 0.75f;
  sent.hearth_laid = true;
  sent.hearth = Vec3 (3, 2, 5);
  sent.fire_burn = 0.5f;
  sent.pick_count = 40;
  sent.picks_from = 38;
  sent.picks = { 1001, 1002 };

  const std::optional<CompanionSnapshot> got = decode (encode (sent));
  MOPPE_CHECK (got.has_value ());
  if (!got)
    return;
  MOPPE_CHECK (got->player == 7 && got->sequence == 99);
  MOPPE_CHECK (got->world == sent.world && got->day_hours == 9.25);
  MOPPE_CHECK (got->mode == M_FOOT && !got->glider && got->walker);
  MOPPE_CHECK_NEAR (got->walker->position[0], 12.5f, 1e-6f);
  MOPPE_CHECK_NEAR (got->walker->heading[0], 1.0f, 1e-6f);
  MOPPE_CHECK (!got->walker->grounded);
  MOPPE_CHECK_NEAR (got->walker->look_pitch, -0.3f, 1e-6f);
  MOPPE_CHECK_NEAR (got->bike.wheel_spin_radians, 123.0f, 1e-6f);
  MOPPE_CHECK (got->hung && got->lying_down ());
  MOPPE_CHECK_NEAR (got->hammock.strap[1][0], 5.0f, 1e-6f);
  MOPPE_CHECK_NEAR (got->hammock.girth[1], 0.2f, 1e-6f);
  MOPPE_CHECK (got->hearth_laid);
  MOPPE_CHECK_NEAR (got->fire_burn, 0.5f, 1e-6f);
  MOPPE_CHECK (got->pick_count == 40 && got->picks_from == 38);
  MOPPE_CHECK (got->picks == sent.picks);
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
  broken.bike.position = Vec3 (std::nanf (""), 0, 0);
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

MOPPE_TEST (a_companion_is_drawn_between_snapshots) {
  CompanionTrack track;
  // Sent every tenth of a second, a metre on each time; this machine's
  // clock reads a hundred seconds more than theirs.
  for (int i = 0; i <= 5; ++i)
    track.push (companion_test_walker (static_cast<float> (i), 0.1 * i),
                100.0 + 0.1 * i);
  MOPPE_CHECK (track.present (100.5));
  // Drawn a tenth of a second behind: at 100.45, where they were at 0.35.
  const std::optional<CompanionSnapshot> seen = track.sample (100.45);
  MOPPE_CHECK (seen && seen->walker);
  if (seen && seen->walker)
    MOPPE_CHECK_NEAR (seen->walker->position[0], 3.5f, 1e-3f);
  // With no more word they are carried on a little, then stand.
  const std::optional<CompanionSnapshot> later = track.sample (101.5);
  MOPPE_CHECK (later && later->walker);
  if (later && later->walker) {
    MOPPE_CHECK (later->walker->position[0] > 5.0f);
    MOPPE_CHECK (later->walker->position[0] < 8.0f);
  }
  // And after long silence they have gone.
  MOPPE_CHECK (!track.present (104.0));
  MOPPE_CHECK (!track.sample (104.0));
}

MOPPE_TEST (a_companion_does_not_slide_across_a_leap) {
  const CompanionSnapshot here = companion_test_walker (0.0f, 0.0);
  CompanionSnapshot there = companion_test_walker (500.0f, 0.1);
  const CompanionSnapshot seen = blend (here, there, 0.5f);
  MOPPE_CHECK_NEAR (seen.walker->position[0], 500.0f, 1e-3f);
  // The stride turns the short way round its cycle.
  CompanionSnapshot stepped = companion_test_walker (0.5f, 0.1);
  stepped.walker->stride_phase = 0.1f;
  MOPPE_CHECK_NEAR (
    blend (here, stepped, 0.5f).walker->stride_phase, 0.0f, 1e-4f);
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

  std::vector<CompanionSnapshot> at_first, at_second;
  for (int round = 0; round < 100 && (at_first.empty () || at_second.empty ());
       ++round) {
    second->send (companion_test_walker (2.0f, 0.0));
    // The first answers whoever called.
    first->receive (at_first);
    if (!at_first.empty ())
      first->send (companion_test_walker (1.0f, 0.0));
    second->receive (at_second);
    std::this_thread::sleep_for (std::chrono::milliseconds (5));
  }
  MOPPE_CHECK (!at_first.empty () && !at_second.empty ());
  if (at_first.empty () || at_second.empty ())
    return;
  MOPPE_CHECK (at_first.front ().player == second->player ());
  MOPPE_CHECK (at_second.front ().player == first->player ());
  MOPPE_CHECK_NEAR (at_first.front ().walker->position[0], 2.0f, 1e-6f);
  MOPPE_CHECK (!first->companion_address ().empty ());
}
