+++
id = "ENG-022"
title = "Make GeneratedWorld the owner of completed world artifacts"
rfc = "RFC-0001"
track = "current-engine-refactoring"
status = "backlog"
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
