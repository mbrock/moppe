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
731; 4 and 20 geological steps; fractional D-infinity routing; and three
repeats. On macOS, add `--backend cpu,metal` to compare the conventional Metal
compute prototype with the CPU reference. Metal rows include mean, p99, p99.9,
maximum, and cells-over-1-metre height error against an untimed CPU result.
Use smaller development sweeps when iterating, for example:

```sh
tools/orogeny-benchmark /tmp/orogeny-quick.csv --skip-build \
  --resolutions 257,513 --seeds 123 --steps 4,20 --repeats 1
```

Compare elapsed time only between equivalent build configurations and idle
hosts. A final-height hash change is not automatically a regression, but it
requires an explicit terrain-quality and determinism review rather than being
accepted as a performance-only change.

## Historical routing comparison

The baseline below was captured from a RelWithDebInfo build on 2026-07-18 with
seed 123. It predates the removal of the D8 evolution mode and remains only as
a historical record of the decision to standardize on D-infinity.

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

## Historical exact CPU checkpoint

The first optimization stage precomputed the D-infinity stencil geometry,
used a lean wet-routing result, reused the drainage graph's topological order,
and retained per-step workspace. Final float hashes stayed identical to the
initial baseline. This historical table is the median across seeds 123 and
731 and three repeats per seed:

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
path. It can be enabled in the macOS game with `MOPPE_METAL_OROGENY=1`; CPU
remains the default and the only iOS path.

## Loading-screen result

A historical `MOPPE_LOADING_BENCHMARK=1` run (the hook and the loading-screen
terrain preview were later removed) with a forced cache miss at research
profile, 1025 samples, on the same M2 gave:

| Backend | Evolution window | Frames | Mean frame | Frames > 20 ms |
|---|---:|---:|---:|---:|
| CPU | 78.1 s | 3,279 | 23.8 ms | 1,357 (41.4%) |
| Metal | 134.4 s | 5,775 | 23.3 ms | 2,274 (39.4%) |

The full transform and render workload contend for the same GPU. Metal was
27–34% faster in the isolated complete-transform benchmark but 72% slower in
this animated-loading run, without a meaningful frame-time improvement. That
is why the prototype is opt-in rather than the production default.

## Deterministic threads and algebra (2026-10)

With worlds bit-identical across platforms (docs/determinism.md), every
optimization had to keep them so on any thread count. Per stage, 1025
samples, 40 play-profile steps, M2 Pro (10 threads), seconds over the whole
evolution:

| Stage | Before | After | Change |
|---|---:|---:|---|
| D-infinity route selection | 6.17 | 1.04 | threads; one `hypot` per facet, angles only for the winner |
| Accumulation | 3.01 | 2.73 | the routes' unit vectors instead of `cos`/`sin` of their angles |
| Priority-flood | 4.23 | 2.34 | Barnes's pit queue and a radix heap (levels only rise) |
| Lake census | 1.74 | 0.69 | spill walks end below the body; shore sweep through water only |
| Wet routing | 1.95 | 0.33 | eight neighbour distances, not eight per cell; threads |
| Hillslope | 1.97 | 0.52 | face fluxes, then each cell gathers its four; threads |
| Incision, sediment | 4.11 | 4.08 | unchanged: topological walks |

The whole 1025 land went from 25.2 s to 13.5 s, the default 2048 land from
113 s to 56 s on the M2 and from 198 s to 95 s on the i7, with the same
sha256 on both. The census and wet-routing changes are exact; the rest
changed bits once, as `LAND_VERSION` 2.

The topological walks -- accumulation, the incision solve, sediment routing
-- are now most of what remains, each a loop over the drainage order whose
order also sets its float sums. Running them level by level, every cell
pulling from its donors in a fixed order, would let each level's cells run
in parallel; that is the next candidate. The order itself matters for speed:
Kahn's FIFO order and an order by longest-path level each cost as much in
the walks' cache misses as the heap order costs to build.

## Measuring a whole world, and exact savings (2026-10-12)

`MOPPE_FINGERPRINT_COST=1 world-fingerprint 2048 play 123 10` prints, beside
the hashes, what every stage and geological step cost: seconds, memory
resident, and the most there has been. On macOS `sample <pid> 8` then says
where a step's time goes, and `MallocStackLogging=lite` with
`malloc_history <pid> -allBySize` which call sites hold its memory. A
ten-kilometre world at 2048 samples on an M5, before and after changes that
leave every hash the same:

| | Before | After |
|---|---:|---:|
| One geological step | 0.97 s | 0.76 s |
| Whole world, 40 steps and the rest | 52 s | 43.5 s |
| Resident during the evolution | 1.56 GB | 1.43 GB |

The same world at 4096 samples -- the ordinary grain over ten kilometres
-- takes 195 s on the M5 at 3.4 s a step and peaks at 5.0 GB; in the game,
with the renderer beside it, 7.5 GB, which a 16 GB machine holds. On a Ryzen
7950X3D it went from 7 min 23 s to 6 min 42 s and from 5.0 to 4.8 GB: there
a quarter of the time is the kernel's (74 million page faults), because
glibc maps and unmaps every one of a step's large buffers afresh, so
keeping a step's buffers from one step to the next is the next exact
saving on Linux.

- The flow accumulation took a quarter of a step, most of it a binary heap
  whose keys were only ever cell indices. `LowestReady`
  (`terrain/lowest_ready.hh`), a bitmap with a summary word over every
  sixty-four bits, takes the lowest ready cell in the same order.
- The priority-flood and the lake census's shore scan wrapped all eight
  neighbours of every cell with a division and two remainders each; away
  from the seam a neighbour is a fixed step through the lattice.
- Valley deposition allocated two vectors for every source cell.
- A step let its drainage's unused columns (direction, slope, area flux)
  and most of its sediment routing live to the end of the step, and copied
  the channel tangents it could take. Each column of a 4096 world is 67 to
  270 MB.

What a step costs now, by share: flow routing 29% (half of it the
accumulation walk), priority-flood 21%, sediment routing 14%, incision 10%,
lake census 8%, valley deposition 6%, hillslope 5%. A step still holds
about 340 bytes a cell: the long-lived state is some 130 of them, and the
rest is the step's own work -- routes at 28 bytes a cell, sixteen-byte
vectors for directions that have two components, and sediment volumes in
double precision. Narrowing those would change the sums' bits, so it waits
for a change that bumps `LAND_VERSION` anyway.

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
