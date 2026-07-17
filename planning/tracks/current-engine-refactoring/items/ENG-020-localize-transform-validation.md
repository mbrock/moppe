+++
id = "ENG-020"
title = "Localize terrain-transform validation and editing semantics"
rfc = "RFC-0001"
track = "current-engine-refactoring"
status = "ready"
depends_on = ["ENG-002"]
order = 60
areas = ["terrain", "terrain-lab"]
+++

# Localize terrain-transform validation and editing semantics

## Outcome

Each terrain transform owns its validation, identity, semantics, and editable
description, rather than contributing another arm to central switchboards.

## Scope

Start with `validate_program` and its adjacent variant dispatch. Preserve the
current `TerrainProgram` value and evaluator contract.

## Acceptance

- Adding a transform has one obvious local implementation path.
- Program validation no longer dominates terrain control-flow complexity.
- Invalid programs still fail with useful, stable diagnostics.
