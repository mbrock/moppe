# Current-engine refactoring

This is the executable track for [RFC-0001](../../rfcs/0001-current-engine-refactoring.md).
It is a dependency graph, not a release calendar. Work can happen in parallel
when nodes are independent, but an item becomes `ready` only when its declared
dependencies are `done`.

The first item intentionally repairs the complexity instrument. The earlier
report showed real cyclomatic concentration, but unity builds currently make
its cognitive column and scan scope unreliable. We should not steer a large
refactor with a crooked compass.

```mermaid
flowchart LR
  ENG001["ENG-001: trustworthy complexity report"]
  ENG002["ENG-002: characterize seams"]
  ENG010["ENG-010: one Bundle"]
  ENG011["ENG-011: SurfaceAtlas"]
  ENG012["ENG-012: presentation bridge"]
  ENG020["ENG-020: local transform validation"]
  ENG021["ENG-021: WorldRecipe"]
  ENG022["ENG-022: GeneratedWorld"]
  ENG023["ENG-023: async world handoff"]
  ENG030["ENG-030: InputFrame"]
  ENG031["ENG-031: GameSession"]
  ENG032["ENG-032: public advance"]
  ENG033["ENG-033: replay benchmark"]
  ENG040["ENG-040: FrameView"]
  ENG041["ENG-041: scene presentation"]
  ENG042["ENG-042: terrain-lab model"]
  ENG043["ENG-043: Metal passes"]
  ENG050["ENG-050: target graph"]
  ENG001 --> ENG002
  ENG002 --> ENG010
  ENG010 --> ENG011 --> ENG012
  ENG002 --> ENG020 --> ENG021 --> ENG022 --> ENG023
  ENG002 --> ENG030 --> ENG031 --> ENG032 --> ENG033
  ENG023 --> ENG031
  ENG031 --> ENG040 --> ENG041 --> ENG043 --> ENG050
  ENG012 --> ENG041
  ENG020 --> ENG042 --> ENG050
  ENG033 --> ENG050
```

Run `make plan-graph` to render this exact graph from the work-item metadata.
The checked-in view above is a compact map for browsing; the item files are
authoritative.
