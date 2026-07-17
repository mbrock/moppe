+++
id = "ENG-002"
title = "Characterize the world, surface, and replay seams before moving them"
rfc = "RFC-0001"
track = "current-engine-refactoring"
status = "ready"
depends_on = ["ENG-001"]
order = 20
areas = ["tests", "map", "game"]
+++

# Characterize the world, surface, and replay seams before moving them

## Outcome

The existing behavior at the intended refactoring boundaries is captured by
small tests and deterministic artifacts.

## Scope

Surface sampling parity, materialization barriers, fixed-step state restore,
and the completed-world loading transition. This is characterization, not a
new golden corpus for every subsystem.

## Acceptance

- Tests fail if a moved boundary changes its observable contract.
- A smoke path reaches a generated world rather than stopping at loading.
- Any newly captured golden has an explicit owner and update policy.
