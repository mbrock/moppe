+++
id = "TER-002"
title = "Choose the tectonic forcing from a same-seed matrix"
rfc = "RFC-0005"
track = "terrain-landforms"
status = "ready"
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
