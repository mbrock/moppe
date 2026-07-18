+++
id = "ENG-032"
title = "Advance GameSession through one public simulation operation"
rfc = "RFC-0001"
track = "current-engine-refactoring"
status = "done"
depends_on = ["ENG-031"]
order = 120
areas = ["simulation", "game"]
+++

# Advance GameSession through one public simulation operation

## Outcome

Fixed-step gameplay advances through a small, testable operation taking a
world, a session, an input frame, and a typed duration.

## Scope

Extract ordinary playable simulation first. Loading, benchmark scheduling,
and application mode switching remain above the seam initially.

## Acceptance

- The central gameplay portion of `MoppeGame::tick` becomes delegation.
- Replay tests drive the same public advance path as live play.

## Evidence

`game::advance_game_session(context, session, input, seconds_t)` takes a
renderer-free completed-world context, one `GameSession`, one `InputFrame`,
and a typed duration. It contains ordinary input application, actors,
effects, scoring, camera, and FOV; `MoppeGame::tick` retains mode selection,
global clock/weather, benchmark scheduling, water inspection, and platform
effects above or beside the call.

`game_session_advance_replays_an_input_tape_on_the_same_world` records
keyboard input with `InputFrameAdapter`, advances a live session, restores its
checkpoint into a replacement session, and replays the exact frames through
the same public operation. It checks non-vacuous motion and camera action plus
the replayed physics, camera, odometer, star, and dust state.

`./tools/format --check`, `git diff --check`, `cmake --build build -j 2`,
`./build/moppe-tests` (195 tests), `ctest --test-dir build
--output-on-failure`, and `./tools/plan check` passed. Live demo and
water-inspection captures wrote `/tmp/moppe-eng032-play.png` and
`/tmp/moppe-eng032-water.png`; an in-process regeneration capture wrote
`/tmp/moppe-eng032-regen.png` after the second completed-world handoff.
