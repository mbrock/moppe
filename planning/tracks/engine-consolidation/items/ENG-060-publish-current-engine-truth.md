+++
id = "ENG-060"
title = "Publish the post-harmonization engine truth"
rfc = "RFC-0002"
track = "engine-consolidation"
status = "done"
depends_on = []
order = 10
areas = ["docs", "architecture", "workflow"]
+++
# Publish the post-harmonization engine truth

## Outcome

The engine atlas, current-status page, generated-world description, surface
storage description, and build instructions agree with the implementation
after direct generation and bundle consolidation.

## Scope

Preserve dated project notes as history, but stop presenting deleted Terrain
Lab, `TerrainProgram`, `map::Surface`, or `map::WaterSurface` values as current
architecture.

## Acceptance

- A new reader can trace construction, ownership, simulation, presentation,
  and build composition using names that exist in `master`.
- A clean checkout's documented test command builds the excluded test target
  before invoking CTest.
- Historical progress notes identify the date at which their present-tense
  claims stopped being current.

## Evidence

`docs/status.md` is the living status entry, and `docs/engine-atlas.md` now
describes the direct `SurfaceGeometry`/`SurfaceReadings`/`WaterSheets`
ownership, literal world build, completed-owner handoff, session/frame seams,
Metal/WebGPU backends, and direct source-group build. The focused generated
world, surface storage, game-state, terrain, trail, renderer, and
characterization documents use the same names.

The README builds `moppe-tests` before invoking CTest. `docs/project.org`
identifies itself as the notebook through 2026-07-17 and points readers to the
living status and planning surfaces.
