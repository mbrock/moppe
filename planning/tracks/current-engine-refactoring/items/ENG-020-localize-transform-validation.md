+++
id = "ENG-020"
title = "Localize terrain-transform validation and editing semantics"
rfc = "RFC-0001"
track = "current-engine-refactoring"
status = "done"
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

## Evidence

Every concrete transform now owns `validate`, `description`, `detail`, and
editable-property methods. `validate_program` and the Terrain Lab use only
generic variant visitation, so no type-specific validation, identity,
semantics, or property rows remain in their switchboards. Focused program
tests cover each transform's description and property count plus stable
invalid-program diagnostics. `cmake --build build --target moppe-tests -j 2`,
`./build/moppe-tests` (187 tests), `ctest --test-dir build --output-on-failure`,
and `cmake --build build --target moppe -j 2` passed.
