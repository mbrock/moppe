+++
id = "TER-022"
title = "Distribute lake sediment and build mouth deltas"
rfc = "RFC-0005"
track = "terrain-landforms"
status = "done"
depends_on = ["TER-021"]
order = 70
areas = ["terrain", "lakes", "sediment"]
+++
# Distribute lake sediment and build mouth deltas

## Outcome

Sediment entering standing water is distributed over an accommodation-aware
lake or coastal footprint, with preferential deposition at mouths and explicit
remaining export.

## Acceptance

- Lake deposition is not concentrated at one routed cell.
- Basin fill can alter later drainage without creating an instantaneous dam.
- Mouth captures show a depositional transition into standing water.
- Lake storage plus downstream or ocean export closes the sediment ledger.

## Evidence

The drainage DAG now gives each inland water body one shared storage budget
equal to its available water-column accommodation, limited by the per-step
aggradation rate. Incoming load fills that budget before continuing toward
the outlet. A second conservative placement stage distributes the stored
volume across the body's eligible cells. Coastal loads occupy a bounded,
downstream-widening fan and any load that cannot fit is explicitly exported.

Synthetic tests prove that a six-cubic-metre load occupies all nine cells of
a test lake without crossing its water surface, that the resulting bed changes
the next standing-water analysis, and that overlapping mouth footprints obey
per-cell capacity while storage plus export equals the incoming load. The
complete 250-test suite passes.

The optimized seed-123 Play run retained 199.2 m of relief while lowering
median land slope from 25.5 to 17.2 degrees. Land at or below ten degrees grew
from 10.4% to 27.3%, and its largest connected region grew from 0.423 to
2.50 km2. The river-mouth view shows a broad wet depositional approach into
the sea. Fresh generation took 115 seconds after replacing a domain-sized
per-mouth scratch scan with compact footprints, versus 113 seconds for
TER-021; cache schema 10 loaded on the immediate verification launch. Full
evidence is in `../findings.md`.
