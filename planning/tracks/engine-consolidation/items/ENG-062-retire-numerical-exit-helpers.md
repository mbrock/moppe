+++
id = "ENG-062"
title = "Retire deprecated numerical-exit helpers"
rfc = "RFC-0002"
track = "engine-consolidation"
status = "done"
depends_on = ["ENG-061"]
order = 30
areas = ["units", "terrain", "game"]
+++
# Retire deprecated numerical-exit helpers

## Outcome

The staged `meters_value` family is gone. Calculations retain quantities until
an explicit numerical boundary, and those boundaries name the unit they
produce.

## Scope

Work subsystem by subsystem. Do not replace the helper family with another
identity adapter or erase semantic quantity specifications.

## Acceptance

- No deprecated numerical-exit helper remains declared or referenced.
- Terrain tests cover the same physical results.
- A clean build has no warnings from the retired migration.

## Evidence

The `meters_value` helper family has been deleted from `quantities.hh`.
Production code and tests now name the unit at each scalar API boundary with
`numerical_value_in`; typed comparisons and arithmetic remain quantities.
The complete 199-test binary passes, and rebuilding it emits no quantity
migration or deprecation warnings.
