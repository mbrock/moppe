+++
id = "ENG-010"
title = "Consolidate the finite typed Bundle abstraction"
rfc = "RFC-0001"
track = "current-engine-refactoring"
status = "ready"
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
