# Uplift-driven orogeny calibration

Date: 2026-07-13

This experiment calibrates the opt-in `OrogenyEvolution` source introduced by
RFC-001. Unlike the earlier analytical erosion comparison, relief begins as a
20 m shallow continent around sea level. The geological recipe supplies a
bounded spatial uplift pattern; mountain relief must emerge from uplift,
stream-power incision, and hillslope diffusion.

## Configuration

- world extent: 11 km x 11 km, 650 m vertical scale
- source coastline: relative elevation 0.5
- sea level: 50 / 650 relative elevation
- initial relief: 20 m
- maximum uplift: 0.001 m/year
- stream-power erodibility `K`: 2e-5
- drainage exponent `m`: 0.4
- hillslope diffusivity `D`: 0.0001 m2/year
- geological step: 50,000 years
- routing: priority flood and wet drainage recomputed every step

The fixed-seed Research command is:

```sh
./build/terrain-pipeline-demo /tmp/moppe-orogeny.png \
  257 123 combined \
  orogeny=1000000,50000,0.001,2e-5,0.4,0.0001
```

## Duration calibration

Seed 123 at 257 square produced:

| Profile | Duration | Steps | Net raised volume | Final-step mean change | Final-step maximum change |
|---|---:|---:|---:|---:|---:|
| Fast | 200 kyr | 4 | 3.31 km3 | 5.82 m | 30.64 m |
| Play | 500 kyr | 10 | 6.10 km3 | 2.62 m | 23.65 m |
| Research | 1 Myr | 20 | 7.33 km3 | 0.54 m | 20.84 m |
| Extended | 2 Myr | 40 | 7.32 km3 | 0.21 m | 19.17 m |

The 1 Myr Research run prescribed 19.87 km3 of tectonic uplift and removed
12.60 km3 through implicit incision. Its net lowered volume was zero because
the evolving range remained above its shallow initial surface. The separate
process volumes are therefore more informative than net raised/lowered volume
for this coupled model.

Seeds 7 and 4041 gave final-step mean changes of 0.72 m and 0.34 m at 1 Myr.
Their maximum final-step changes were 31.13 m and 14.06 m. This spread argues
for exposing convergence readings rather than claiming one universal stopping
time. Two million years reduced the residual on seed 123 but made almost no
visible difference, so 1 Myr is the Research compromise and the duration
remains editable.

## Rider-resolution cost and reading

A 1025-square, seed-123 Research run took 6.18 seconds on an M2 Pro. The
output showed coherent coast-scale dendritic networks, long divides, and
mountain structure absent from the un-evolved shallow source. Twenty complete
flood/routing refreshes are acceptable for this opt-in research path. Updating
receivers only every k steps would weaken the correctness baseline before a
dynamic merge-tree routing view exists, so that optimization was not adopted.

The visual result at 1 Myr and 2 Myr was nearly identical despite the lower
residual at 2 Myr. The chosen profiles therefore prioritize useful iteration:
four steps for Fast interaction, ten for Play comparison, and twenty for the
Research image. At the time of this experiment the ordinary game world
remained on the established relief-source program. Orogeny was subsequently
selected for ordinary world generation; the earlier program remains available
for explicit pipeline comparisons.

## Acceptance checks

- closed-form one-edge and analytical-profile convergence tests;
- spatially varying uplift, fixed ocean, and depression no-rise tests;
- deterministic repeated output and periodic-seam integration tests;
- interleaved diffusion and process-volume reporting tests;
- full `ctest` pass;
- deterministic Terrain Lab runtime capture with
  `MOPPE_LAB_OROGENY=1 MOPPE_LAB_STAGE=0`.
