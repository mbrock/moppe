# Orogeny performance

Orogeny performance is measured at the complete transform boundary: procedural
source materialization is excluded, while drainage analysis, stream-power
evolution, diffusion, reporting, and the final heightmap update are included.
Every timing row also records a hash of the exact final float samples so that a
performance comparison exposes numerical changes.

Build and run the default matrix with:

```sh
cmake --build build --target terrain-orogeny-benchmark
tools/orogeny-benchmark /tmp/orogeny.csv --skip-build
```

The default matrix covers 257, 513, and 1025 samples per side; seeds 123 and
731; 4 and 20 geological steps; D8 and D-infinity routing; and three repeats.
Use smaller development sweeps when iterating, for example:

```sh
tools/orogeny-benchmark /tmp/orogeny-quick.csv --skip-build \
  --resolutions 257,513 --seeds 123 --steps 4,20 --repeats 1
```

Compare elapsed time only between equivalent build configurations and idle
hosts. A final-height hash change is not automatically a regression, but it
requires an explicit terrain-quality and determinism review rather than being
accepted as a performance-only change.

## Initial CPU baseline

The baseline below was captured from a RelWithDebInfo build on 2026-07-18 with
seed 123. It is a directional local baseline, not a cross-machine target.

| Unique grid | Steps | D8 | D-infinity |
|---:|---:|---:|---:|
| 256² | 4 | 75.7 ms | 135.9 ms |
| 256² | 20 | 359.9 ms | 769.8 ms |
| 512² | 4 | 299.6 ms | 572.9 ms |
| 512² | 20 | 1,538.9 ms | 3,144.0 ms |
| 1024² | 4 | 1,272.9 ms | 2,158.1 ms |
| 1024² | 20 | 6,747.8 ms | 16,236.6 ms |

A time sample of the 1024², 20-step D-infinity case attributed approximately
76% of runtime to fractional drainage, 17% to priority-flood, 3% to lake
census, and 4% to the downstream incision solve. The proportions are useful
for ordering work; the CSV matrix remains the acceptance measurement.

## Metal staging rule

Metal work begins with the cell-local D-infinity route selection kernel. The
CPU implementation remains the portable reference, and a Metal path is kept
only if it matches the accepted numerical contract and improves complete
transform latency rather than merely reporting a fast isolated kernel. GPU
flow accumulation, implicit incision, and priority-flood require separate
determinism and algorithm reviews because their graph dependencies do not map
directly to independent compute threads.
