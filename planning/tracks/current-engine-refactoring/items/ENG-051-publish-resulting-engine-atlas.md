+++
id = "ENG-051"
title = "Publish the resulting engine atlas and retire the migration track"
rfc = "RFC-0001"
track = "current-engine-refactoring"
status = "done"
depends_on = ["ENG-050"]
order = 190
areas = ["docs", "architecture"]
+++
# Publish the resulting engine atlas and retire the migration track

## Outcome

Architecture documentation enumerates the actual domains, intrinsic sections,
world/session state boundaries, presentations, and target dependencies; this
track records what was completed, deferred, or dropped.

## Scope

Update living architecture documents and RFC status. Do not erase the history
of decisions that explains the resulting shape.

## Acceptance

- A new reader can map the engine without reading this execution history.
- The track has no ambiguous active or ready work items at closure.

## Evidence

`docs/engine-atlas.md` is the current reader entry: it maps source domains,
all named surface/water intrinsic sections, completed-world/session/frame
boundaries, presentation bridges, lifecycle handoff, and CMake dependencies.
The documentation index now leads to it and to the generated-world and
game-state details. `renderer-design.md` is explicitly a Metal implementation
record with its old cutover rationale retained as history; `refactoring-seams`
is now a preserved characterization baseline and its target diagram includes
the real simulation-to-render dependency.

RFC-0001 records the realized result, the explicit deferrals, and that no work
item was dropped. This track's browsable graph includes its closure node, and
all 19 work-item statuses are `done`; there are no `ready` or `active` nodes.
