+++
id = "ENG-050"
title = "Align the CMake target graph with proven world, simulation, and scene boundaries"
rfc = "RFC-0001"
track = "current-engine-refactoring"
status = "done"
depends_on = ["ENG-033", "ENG-042", "ENG-043"]
order = 180
areas = ["build", "architecture"]
+++
# Align the CMake target graph with proven world, simulation, and scene boundaries

## Outcome

The target graph exposes the architecture established by the earlier work:
spatial/terrain, world, simulation, scene, portable rendering, backend, and
application composition.

## Scope

Move targets only after dependency direction is true in code. Keep the normal
build lean and preserve test/tool entry points.

## Acceptance

- The broad `moppe_game` target is no longer the owner of unrelated layers.
- A fresh default build and the ordinary game-plus-tests path remain practical.

## Evidence

`moppe_spatial`, `moppe_terrain`, `moppe_world`, `moppe_simulation`,
`moppe_scene`, and `moppe_app` now express the completed ownership direction.
The world target owns concrete terrain and Terrain Lab model construction;
simulation owns the mutable session; scene owns presentation; and the app owns
the two platform-service callers. The separate test compilation found and
fixed two unity-only test namespace leaks, so the suite also now compiles as
ordinary translation units. `docs/refactoring-seams.md` records the graph and
the deliberate Apple-common versus selected-platform boundary.

Validation on 2026-07-17:

- Fresh default Ninja configuration builds `moppe` and `moppe-tests`; `ctest
  --test-dir /tmp/moppe-eng050-default --output-on-failure` passes all 203
  tests.
- A separate non-unity developer configuration builds the game, testbed, and
  terrain tools, then passes all 203 CTest tests. `MOPPE_FRAMES=30
  moppe-testbed`, a deterministic river capture, and a Terrain Lab capture
  all rendered successfully from that configuration.
- A fresh iPhone Simulator Xcode configuration builds `moppe-ios` with
  `CODE_SIGNING_ALLOWED=NO` and reports `BUILD SUCCEEDED`.
