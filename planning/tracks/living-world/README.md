# Living world

This is the executable track for
[RFC-0003](../../rfcs/0003-a-world-that-reads-as-alive.md). It spends the
fields the generator already computes: first on what the ground says, then on
how surfaces move, then on giving water kinds.

```mermaid
flowchart LR
  subgraph ground["1. The ground says where you are"]
    LIV001["LIV-001: relief-graded material"]
    LIV002["LIV-002: wetness index"]
    LIV003["LIV-003: cover from habitat"]
    LIV004["LIV-004: species and vigor"]
    LIV005["LIV-005: measure the corrugation"]
  end
  subgraph motion["2. Surfaces move"]
    LIV010["LIV-010: wind hierarchy"]
    LIV011["LIV-011: per-crown phase and tilt"]
    LIV012["LIV-012: forest LOD handoff"]
    LIV013["LIV-013: the bike's wake"]
    LIV014["LIV-014: flow detail without pulsing"]
    LIV015["LIV-015: halftone foam"]
  end
  subgraph kinds["3. Water acquires kinds"]
    LIV020["LIV-020: junctions as a field"]
    LIV021["LIV-021: lips and cascades"]
    LIV022["LIV-022: the falling sheet"]
    LIV023["LIV-023: spend the waterline"]
    LIV024["LIV-024: water the rider can touch"]
    LIV025["LIV-025: lakes in the browser"]
  end
  LIV001 --> LIV002 --> LIV003 --> LIV004
  LIV001 --> LIV005
  LIV010 --> LIV011 --> LIV012
  LIV011 --> LIV013
  LIV014 --> LIV015 --> LIV020 --> LIV021 --> LIV022
  LIV023 --> LIV024
```

Movement 1 is the readout the world was already owed. Movement 2 is the
cheapest aliveness in the whole library — most of its items are a day. Movement
3 closes the water TODOs the archived notebook has carried since July, and its
first item is expected to make rivers *cheaper*, not dearer.

Movement 4 — the merge tree as the one hydrological structure, storms as a
display of the drainage graph, and a wear field the ground keeps — is named in
the RFC and deliberately not decomposed here. It becomes its own RFC once
movement 3 has proven the field-first rendering discipline.
