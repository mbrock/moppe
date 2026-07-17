+++
id = "ENG-022"
title = "Make GeneratedWorld the owner of completed world artifacts"
rfc = "RFC-0001"
track = "current-engine-refactoring"
status = "done"
depends_on = ["ENG-021", "ENG-011"]
order = 80
areas = ["world", "map", "terrain"]
+++

# Make GeneratedWorld the owner of completed world artifacts

## Outcome

A completed world owns its authoritative terrain, surface atlas, water atlas,
and named analysis products such as drainage, lakes, rivers, and trails.

## Scope

Move ownership and construction order out of `MoppeGame`; do not yet change
the asynchronous handoff or runtime actor ownership.

## Acceptance

- The generated world is a coherent value with clear immutable/mutable rules.
- World analyses have named ownership instead of parallel optionals in the app.
- Existing loading and gameplay behavior remains intact.

## Evidence

`game::GeneratedWorld` is a stable renderer-free value containing the recipe,
normalized world parameters, authoritative heightmap, materialized
`SurfaceAtlas`, terrain history, optional water surface and trails, and one
named complete hydrology product. Its explicit `Builder` performs terrain
completion in order; `MoppeGame` retains only const borrows for gameplay and
presentation, while the Lab gets one named mutable map transaction. Regenerate
uses an in-place builder reset so vehicles and glider keep valid terrain and
surface references.

`tests/game/generated_world_test.cc` exercises named hydrology, atlas and water
materialization, and address-stable reset without a renderer. `cmake --build
build -j 2`, `./build/moppe-tests` (192 tests), and `tools/plan check` passed.
The deterministic Terrain Lab and normal river-capture paths also wrote
`/tmp/moppe-eng-022-terrain-lab.png` and `/tmp/moppe-eng-022-water.png` with
seed 123 and the fast terrain profile.
