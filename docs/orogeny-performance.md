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
On macOS, add `--backend cpu,metal` to compare the conventional Metal compute
prototype with the CPU reference. Metal rows include mean, p99, p99.9, maximum,
and cells-over-1-metre height error against an untimed CPU result.
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

## Exact CPU checkpoint

The first optimization stage precomputes the D-infinity stencil geometry,
uses a lean wet-routing result, reuses the drainage graph's topological order,
and retains per-step workspace. Final float hashes stayed identical to the
initial baseline. The table is the median across seeds 123 and 731 and three
repeats per seed:

| Unique grid | Steps | D8 | D-infinity |
|---:|---:|---:|---:|
| 256² | 4 | 70.0 ms | 106.8 ms |
| 256² | 20 | 338.5 ms | 584.7 ms |
| 512² | 4 | 266.4 ms | 426.9 ms |
| 512² | 20 | 1,427.0 ms | 2,450.4 ms |
| 1024² | 4 | 1,209.3 ms | 1,718.4 ms |
| 1024² | 20 | 5,998.2 ms | 9,584.9 ms |

The largest case is about 41% faster for D-infinity and 11% faster for D8 than
the initial directional baseline.

## Conventional Metal prototype

The Metal backend accelerates only cell-local D-infinity route selection. It
uses conventional compute pipelines and persistent `MTLStorageModeShared`
buffers, so it does not depend on Metal 4. Priority-flood, wet-route fallback,
flow accumulation, and the implicit incision solve remain on the CPU. Invalid
or non-downhill GPU routes are rejected before the shared CPU stages run.

On an idle Apple M2, the median of three seed-123 runs was:

| Unique grid | Steps | CPU | Metal | Change |
|---:|---:|---:|---:|---:|
| 256² | 4 | 98.5 ms | 68.1 ms | -31% |
| 256² | 20 | 526.0 ms | 349.8 ms | -34% |
| 512² | 4 | 377.2 ms | 269.7 ms | -29% |
| 512² | 20 | 2,152.4 ms | 1,421.3 ms | -34% |
| 1024² | 4 | 1,554.6 ms | 1,086.8 ms | -30% |
| 1024² | 20 | 9,269.1 ms | 6,771.7 ms | -27% |

Metal and CPU float arithmetic do not select identical routes at every near
tie. The following are the worst observed metrics at each size and step count
over seeds 123 and 731. The affected-cell percentage uses unique cells.

| Unique grid | Steps | Mean | p99 | p99.9 | Maximum | Cells > 1 m |
|---:|---:|---:|---:|---:|---:|---:|
| 256² | 4 | 0.000 m | 0.000 m | 0.004 m | 0.592 m | 0 |
| 256² | 20 | 0.011 m | 0.053 m | 2.737 m | 18.908 m | 172 (0.26%) |
| 512² | 4 | 0.000 m | 0.000 m | 0.003 m | 1.487 m | 3 (<0.01%) |
| 512² | 20 | 0.003 m | 0.020 m | 0.888 m | 11.537 m | 219 (0.08%) |
| 1024² | 4 | 0.000 m | 0.000 m | 0.006 m | 1.580 m | 2 (<0.01%) |
| 1024² | 20 | 0.009 m | 0.109 m | 1.541 m | 37.390 m | 1,609 (0.15%) |

The prototype is therefore not bitwise deterministic with the portable CPU
path. It can be enabled in the macOS game and Terrain Lab with
`MOPPE_METAL_OROGENY=1`; CPU remains the default and the only iOS path.

## Loading-screen result

`MOPPE_LOADING_BENCHMARK=1` reports frame pacing while the orogeny transform
runs. A forced-cache-miss, research-profile run at 1025 samples on the same M2
gave:

| Backend | Evolution window | Frames | Mean frame | Frames > 20 ms |
|---|---:|---:|---:|---:|
| CPU | 78.1 s | 3,279 | 23.8 ms | 1,357 (41.4%) |
| Metal | 134.4 s | 5,775 | 23.3 ms | 2,274 (39.4%) |

The full transform and render workload contend for the same GPU. Metal was
27–34% faster in the isolated complete-transform benchmark but 72% slower in
this animated-loading run, without a meaningful frame-time improvement. That
is why the prototype is opt-in rather than the production default.

## Remaining GPU candidates

- Hillslope diffusion is a regular stencil and would be a good GPU kernel when
  enabled, but the shipped evolution has zero diffusivity, so it contributes
  no time today.
- Report reductions are parallelizable but account for too little of the
  measured profile to justify another CPU/GPU boundary.
- Flow accumulation and the backward-Euler incision solve follow a topological
  graph. A GPU implementation needs a levelized or work-queue algorithm and a
  separate determinism contract; simply porting the loops is unlikely to pay.
- Priority-flood is the next meaningful CPU target (about 17% of the original
  large-case profile), but its global heap ordering makes it a poor first Metal
  kernel. A CPU queue/radix redesign should be evaluated before a GPU version.

The next Metal experiment should first remove loading-render contention—for
example by throttling preview rendering or explicitly scheduling compute—then
rerun both the transform matrix and loading benchmark. Moving more stages to
Metal is not justified until that end-to-end result is positive.
