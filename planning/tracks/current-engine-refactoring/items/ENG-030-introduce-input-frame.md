+++
id = "ENG-030"
title = "Translate platform events into a typed InputFrame"
rfc = "RFC-0001"
track = "current-engine-refactoring"
status = "done"
depends_on = ["ENG-002"]
order = 100
areas = ["platform", "simulation"]
+++

# Translate platform events into a typed InputFrame

## Outcome

Keyboard, touch, scripted benchmark input, and cinematic controls become one
per-tick input value before gameplay reads them.

## Scope

Keep platform event delivery intact; this is an adapter at the game boundary,
not a new input framework.

## Acceptance

- Simulation can be driven by recorded input values without platform events.
- Existing key and touch behavior is covered by tests or an equivalent smoke
  path.

## Evidence

`game::InputFrame` carries typed turn, drive, boost, and ordinary gameplay
actions without platform types. `InputFrameAdapter` preserves the existing
keyboard, touch `ControlState`, cinematic, and mount-combo mappings until one
frame is consumed by `MoppeGame::apply_input_frame`. The graphics benchmark
now produces the same recorded value directly. `moppe-tests` covers recorded,
keyboard, touch, and cinematic inputs; `ctest --test-dir build
--output-on-failure` passes.
