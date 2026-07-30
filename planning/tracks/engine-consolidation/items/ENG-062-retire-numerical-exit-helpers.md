+++
id = "ENG-062"
title = "Retire deprecated numerical-exit helpers"
rfc = "RFC-0002"
track = "engine-consolidation"
status = "ready"
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
