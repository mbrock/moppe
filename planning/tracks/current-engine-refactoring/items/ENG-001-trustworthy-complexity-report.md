+++
id = "ENG-001"
title = "Make the complexity report trustworthy under unity builds"
rfc = "RFC-0001"
track = "current-engine-refactoring"
status = "done"
depends_on = []
order = 10
areas = ["tools", "build"]
+++

# Make the complexity report trustworthy under unity builds

## Outcome

The report analyzes only the current repository's intended C++ sources and
associates cognitive-complexity diagnostics with their original files.

## Scope

Use an analysis-specific non-unity compilation database and enumerate only
sources under `moppe/` from it.
Exclude build products, copied worktrees, tests, and unrelated tools unless a
separate report explicitly requests them.

## Acceptance

- `make complexity` produces a current-source-only CSV.
- Cyclomatic and cognitive rows refer to the same original source locations.
- The report documents which source roots and build configuration it used.

## Evidence

`make complexity` generated 867 rows from 62 `moppe/` sources with the
non-unity `homebrew-llvm` preset. Its `complexity-metadata.json` records the
source roots and disabled unity/test configuration; every CSV location names
an existing source beneath `moppe/`. The standalone `moppe` target built from
that configuration.
