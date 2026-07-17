+++
id = "ENG-050"
title = "Align the CMake target graph with proven world, simulation, and scene boundaries"
rfc = "RFC-0001"
track = "current-engine-refactoring"
status = "backlog"
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
