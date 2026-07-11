# First finite-time stream-power comparison

Date: 2026-07-11

This experiment tests the first Moppe implementation of the finite-time
`n = 1` stream-power solution described by Tzathas, Gailleton, Steer, and
Cordonnier (2024). It is deliberately a comparison of modules, not a claim
that the paper's full multigrid method is complete.

## Configuration

- resolution: 257 square, periodic duplicated seam
- world extent: 5000 m x 5000 m, 650 m vertical scale
- seed: 123
- source: combined geological field, normalize, power 1.15
- analytical age: 200,000 years
- uplift: 0 m/year (erosion post-process)
- erodibility `k`: 2e-5
- drainage exponent `m`: 0.4
- routing: four relaxed fixed-point passes at 0.5 relaxation
- droplets: 30,000, batch 256, maximum lifetime 512, settle at water cutoff
- talus: two passes at 0.003
- visible-channel threshold: 1,024 source cells

Command:

```sh
./build/terrain-stream-power-experiment \
  257 123 200000 4 30000 /tmp/moppe-stream-final
```

## Results

| Mode | Runtime | Dry sinks | Channel cells | Longest path | Puddles | Ponds | Lakes | Inland water |
|---|---:|---:|---:|---:|---:|---:|---:|---:|
| Source | 0 ms | 515 | 1 | 43 | 107 | 145 | 21 | 140.0 Mm3 |
| Analytical | 80 ms | 2,285 | 0 | 28 | 729 | 628 | 12 | 51.9 Mm3 |
| Analytical + talus | 77 ms | 538 | 1 | 42 | 177 | 143 | 13 | 52.0 Mm3 |
| Droplets | 809 ms | 416 | 132 | 89 | 214 | 46 | 6 | 20.4 Mm3 |
| Analytical + droplets | 880 ms | 434 | 129 | 97 | 231 | 32 | 5 | 19.4 Mm3 |
| Analytical + droplets + talus | 886 ms | 303 | 112 | 86 | 123 | 12 | 5 | 19.2 Mm3 |

The analytical stage lowered 881.6 million cubic metres, with a mean absolute
change of 35.3 m and maximum change of 346.3 m. With zero uplift, its reported
raised volume is exactly zero. The droplet sediment ledger closes in both
droplet modes.

## Reading

The global pass is materially different from droplets: it imprints a dense,
catchment-scale dendritic structure in roughly one tenth of the droplet
runtime. It is not independently usable yet. A fixed drainage solution creates
sharp cell-scale discontinuities where the predicted elevation and the next
routing partition disagree; the analytical-only sink count makes that failure
plain. Talus relaxation removes most of those artifacts, while droplets add
local channel texture and further reorganize drainage.

The integrated result is the useful result in this first slice: compared with
droplets alone, it has fewer dry sinks (303 vs. 416) and ponds (12 vs. 46),
while retaining a substantial persistent network (112 cells vs. 132). That is
enough evidence to continue the analytical direction, but not enough to make
it the default world recipe.

A second seed used by the droplet-lifetime study (`1783728698`) shows the same
integrated direction rather than a seed-123 accident. Droplets alone produced
416 dry sinks, 78 channel cells, and 40 ponds; analytical plus droplets plus
talus produced 288 sinks, 139 channel cells, and 7 ponds. The analytical-only
failure also repeated (2,070 sinks), reinforcing that the global stage and its
hillslope/detail finishing must be evaluated as a pipeline.

## Next experiment

Implement the paper's coarse-to-fine routing iteration and explicit hillslope
correction, then repeat this table for several fixed seeds at 257 and 1025.
The acceptance question is whether the global stage retains its dendritic
structure without depending on droplet/talus cleanup to repair thousands of
one-cell sinks.
