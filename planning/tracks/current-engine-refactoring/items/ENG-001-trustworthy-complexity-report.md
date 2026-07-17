+++
id = "ENG-001"
title = "Make the complexity report trustworthy under unity builds"
rfc = "RFC-0001"
track = "current-engine-refactoring"
status = "ready"
depends_on = []
order = 10
areas = ["tools", "build"]
+++

# Make the complexity report trustworthy under unity builds

## Outcome

The report analyzes only the current repository's intended C++ sources and
associates cognitive-complexity diagnostics with their original files.

## Scope

Use an analysis-specific source enumeration or non-unity compilation database.
Exclude build products, copied worktrees, tests, and unrelated tools unless a
separate report explicitly requests them.

## Acceptance

- `make complexity` produces a current-source-only CSV.
- Cyclomatic and cognitive rows refer to the same original source locations.
- The report documents which source roots and build configuration it used.
