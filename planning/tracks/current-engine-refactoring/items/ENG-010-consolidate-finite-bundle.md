+++
id = "ENG-010"
title = "Consolidate the finite typed Bundle abstraction"
rfc = "RFC-0001"
track = "current-engine-refactoring"
status = "done"
depends_on = ["ENG-002"]
order = 30
areas = ["spatial", "atelier"]
+++

# Consolidate the finite typed Bundle abstraction

## Outcome

Terrain sections and Atelier organism state use one shared finite,
columnar, quantity-spec-labelled bundle implementation.

## Scope

Move common behavior deliberately and migrate both consumers. Do not broaden
the finite abstraction into a speculative general section calculus here.

## Acceptance

- There is one implementation of `Bundle`, `BundleRow`, `BundleFocus`, and
  spec-based `get`.
- Surface and tree tests retain their present semantics.
- The duplicate Atelier implementation is removed rather than wrapped.

## Evidence

`cmake --build build --target moppe-tests` and
`ctest --test-dir build --output-on-failure` pass, including the surface and
Atelier tree/bundle semantics. `cmake --build build --target atelier` also
passes. `atelier::Tree` and `atelier::HexSheet` now name
`moppe::spatial::Bundle` directly, and `atelier/bundle.hh` is removed; the
shared header is the only implementation of `Bundle`, `BundleRow`,
`BundleFocus`, and spec-based `get`.
