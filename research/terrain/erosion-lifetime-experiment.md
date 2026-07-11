# Droplet lifetime and sediment conservation experiment

Date: 2026-07-11

## Question

The historical droplet algorithm stopped every drop after 64 one-cell steps
while evaporating only 1.5% of its water per step. Did that short causal radius
prevent drainage integration, and how much sediment disappeared when a live
drop hit the cap?

The experiment used seed 1783728698, a periodic 513-square map representing
5 km by 5 km by 650 m, batch size 1, and the canonical geological source plus
normalization and the 1.15 power curve. The uneroded reference had 929 sinks,
12,026 cells above the 64-cell stream threshold, a maximum contributing area
of 4,930 cells, and a longest receiver path of 75 cells.

## Lifetime sweep: 300,000 droplets

| Steps | Runtime | Sinks | Stream cells | Max area | Longest path | Lost sediment |
| ---: | ---: | ---: | ---: | ---: | ---: | ---: |
| 64 | 1.38 s | 2,395 | 19,197 | 4,422 | 150 | 23.39% |
| 128 | 2.81 s | 2,764 | 18,747 | 2,692 | 141 | 7.24% |
| 256 | 5.49 s | 2,641 | 20,649 | 3,312 | 142 | 0.73% |
| 305 | 6.17 s | 2,571 | 20,589 | 3,992 | 167 | 0.34% |
| 512 | 10.12 s | 2,264 | 22,305 | 3,953 | 163 | 0.013% |

All drops reached the cap; none stopped on a numerically flat gradient. At 64
steps each retained 38.0% water. At 305 they retained 0.996%, matching the
predicted natural 1% lifetime.

Longer life therefore repairs the sediment leak and roughly doubles drainage
path length relative to the uneroded source. It does **not** by itself repair
the sink explosion. At a similar compute budget, 1.2 million drops times 64
steps produced 2,170 sinks and a longest path of 160; 300,000 times 256
produced 2,641 sinks and a path of 142. The strong prediction that lifetime
alone dominates an equivalent number of short-lived drops was falsified.

## Natural death and settlement

With 300,000 droplets, a 1% water cutoff, a 512-step safety cap, and explicit
settlement at death:

- every drop stopped at step 305 because of water;
- eroded and deposited totals both measured 10,149.4 normalized height units;
- discarded sediment was exactly zero;
- the result had 2,586 sinks, 21,139 stream cells, maximum area 4,078, and a
  longest path of 151.

Settlement closes the ledger without producing a catastrophic new dam signal,
but it also does not cure the sinks. It is retained as a correctness fix.

## Falsified erosion-footprint hypothesis

A temporary experimental branch spread each erosion event over circular
radius-2 and radius-3 brushes while leaving trajectories and deposition
unchanged. At 100,000 naturally terminated droplets:

| Radius | Runtime | Sinks | Stream cells | Longest path |
| ---: | ---: | ---: | ---: | ---: |
| 1 | 2.41 s | 1,949 | 19,890 | 121 |
| 2 | 2.92 s | 2,204 | 18,835 | 135 |
| 3 | 3.94 s | 2,364 | 17,565 | 131 |

Widening the footprint made the census worse and weakened the stream network.
The experiment was removed rather than retained as an ornamental parameter.

## Decision

The canonical profile now spends approximately the same step budget more
honestly: 300,000 droplets, a 1% water cutoff (natural life about 305 steps),
a 512-step safety cap, batch size 256, and settlement of residual sediment.
The legacy 64-step/discard defaults remain available for exact replay.

The remaining sink population is not a lifetime or simple footprint problem.
The next analysis boundary should represent standing water explicitly: a
priority-flood water surface seeded from the sea, first as a lake-depth
reading and then as depression-aware routing. That separates native basins,
erosion-created pits, and deposition dams before changing erosion again.
