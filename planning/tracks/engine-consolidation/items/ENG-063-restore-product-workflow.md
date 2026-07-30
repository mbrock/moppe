+++
id = "ENG-063"
title = "Restore Moppe as the ordinary repository workflow"
rfc = "RFC-0002"
track = "engine-consolidation"
status = "done"
depends_on = ["ENG-062"]
order = 40
areas = ["build", "workshops"]
+++
# Restore Moppe as the ordinary repository workflow

## Outcome

Plain `make` builds Moppe. Atelier, Lavoir, and Étalon remain named explicit
targets with their experiments and validation intact.

## Scope

Fix workshop warnings and small build-list debris encountered by the closure.
Do not delete the workshops or make their type/storage systems dependencies of
Moppe.

## Acceptance

- `make` selects the game.
- `make lavoir` and `make etalon-test` remain healthy.
- The normal game and test build remain source-group unity builds.

## Evidence

The Makefile's default target is again `moppe`; a plain `make` configures the
optimized desktop build and produces `moppe.app`. Lavoir no longer retains its
constructor-only lattice spacing, so both the workshop and the shared test
translation unit build without that warning. `make lavoir`,
`make etalon-test`, and `make test` all pass, while the CMake game and test
targets retain their source-group unity builds.
