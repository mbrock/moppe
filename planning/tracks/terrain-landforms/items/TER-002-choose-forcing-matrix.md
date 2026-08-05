+++
id = "TER-002"
title = "Choose the tectonic forcing from a same-seed matrix"
rfc = "RFC-0005"
track = "terrain-landforms"
status = "done"
depends_on = ["TER-001"]
order = 20
areas = ["terrain", "analysis", "tooling"]
+++
# Choose the tectonic forcing from a same-seed matrix

## Outcome

A checked-in finding chooses the uplift schedule to carry into process-law
work rather than treating geological age as an aesthetic slider.

## Scope

Compare 250,000, 500,000, and 750,000 years of uplift inside the same
two-million-year evolution for seed 123, with fixed extent, resolution, time
step, renderer, and sediment law. Record relief percentiles, slope
distribution, connected low-gradient area, drainage density, water bodies,
sediment budgets, generation time, and frozen gazetteer views.

## Acceptance

- The comparison is reproducible without editing source constants between
  runs.
- The finding selects one schedule and explains the visual and physical
  tradeoff.
- The selected world has both meaningful relief and materially more
  non-alpine land than continuous uplift.

## Evidence

- `--uplift-years` enters the immutable `WorldRecipe`; terrain and finished
  world caches include its exact typed value and reject mismatched recipes.
- Every frozen gazetteer now writes `terrain-summary.csv` from the completed
  world. Its deterministic readings cover relief, slopes, connected
  low-gradient land, river scale, water bodies, and cumulative sediment.
- `tools/terrain-forcing-matrix` ran fresh optimized 2048-square Play worlds
  at 250, 500, and 750 ky for seed 123. All three produced sixteen views and
  passed an immediate recipe-specific cache-hit launch.
- [`findings.md`](../findings.md) records the numerical and visual comparison
  and selects 500 ky. It preserves 268.5 m of relief while providing 2.3 times
  the low-gradient land fraction of 750 ky; 250 ky loses convincing mountain
  massifs.
