+++
id = "ENG-031"
title = "Collect mutable runtime state in GameSession"
rfc = "RFC-0001"
track = "current-engine-refactoring"
status = "ready"
depends_on = ["ENG-023", "ENG-030"]
order = 110
areas = ["simulation", "game"]
+++

# Collect mutable runtime state in GameSession

## Outcome

The player modes, actors, camera, effects, score, and session clock have one
concrete mutable owner attached to an immutable generated world.

## Scope

Retain distinct bike, car, foot, and glider semantics. Avoid an ECS or a
generic actor manager.

## Acceptance

- `MoppeGame` no longer privately owns the bulk of gameplay fields.
- A session has a clear value-state/checkpoint boundary.
- Immutable world references and physical configuration remain outside it.
