# Engine consolidation

This is the executable track for
[RFC-0002](../../rfcs/0002-engine-consolidation-and-world-relations.md).
It closes the successful but partially documented terrain harmonization,
harvests the narrow useful result of the workshops, and establishes lake
identity as the first production graph domain.

```mermaid
flowchart LR
  ENG060["ENG-060: publish current engine truth"]
  ENG061["ENG-061: direct water upload"]
  ENG062["ENG-062: retire numerical-exit helpers"]
  ENG063["ENG-063: restore the product workflow"]
  ENG070["ENG-070: specify the lake domain"]
  ENG071["ENG-071: use the lake domain"]
  ENG060 --> ENG061 --> ENG062 --> ENG063 --> ENG070 --> ENG071
```

The sequence is intentionally narrow. It does not reopen the completed
RFC-0001 ownership work, and it does not make the workshop implementations
dependencies of the game.
