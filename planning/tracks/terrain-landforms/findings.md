# Terrain-landform findings

## TER-002: finite-uplift matrix

### Decision

Keep **500,000 years of uplift inside the two-million-year Play evolution**.
It is the forcing carried into hillslope and channel process work. This is not
a claim that its current slopes or rivers are finished; it is the middle
forcing that preserves mountain massifs without spending the whole world on
mountains.

### Controlled comparison

The checked-in `tools/terrain-forcing-matrix` command generated seed 123 at
the ordinary 2048-square Play resolution with uplift active for 250, 500, and
750 ky. Extent, water datum, two-million-year evolution, 50-ky step, sediment
law, renderer, 1600x900 window, high graphics, and 18 settling frames were
fixed. Each run used a fresh terrain cache, saved a recipe-specific finished
world, produced sixteen frozen gazetteer views, and then loaded that exact
finished-world cache on a second launch.

Artifacts from the decision run are under
`/tmp/moppe-forcing.7LSCig/matrix`. Reproduce them with:

```sh
make moppe
tools/terrain-forcing-matrix /tmp/moppe-forcing-matrix
```

| Reading | 250 ky | 500 ky | 750 ky |
| --- | ---: | ---: | ---: |
| Land relief | 134.8 m | 268.5 m | 406.7 m |
| Median land elevation | 100.3 m | 151.5 m | 204.8 m |
| Median slope | 12.3 deg | 22.9 deg | 31.7 deg |
| 90th-percentile slope | 20.5 deg | 36.7 deg | 48.4 deg |
| Land at or below 5 deg | 8.71% | 2.18% | 1.02% |
| Land at or below 10 deg | 35.36% | 9.96% | 4.37% |
| Largest connected land at or below 10 deg | 5.22 km2 | 0.90 km2 | 0.25 km2 |
| Visible river length | 43.50 km | 44.54 km | 44.23 km |
| Largest visible catchment | 13.94 km2 | 13.97 km2 | 13.99 km2 |
| Cumulative eroded solid | 600.4 Mm3 | 1,075.5 Mm3 | 1,474.2 Mm3 |
| Cumulative deposited solid | 78.5 Mm3 | 89.5 Mm3 | 94.2 Mm3 |
| Inferred ocean export | 521.9 Mm3 | 986.0 Mm3 | 1,380.0 Mm3 |
| Full generation and gazetteer process | 104 s | 108 s | 102 s |

### Interpretation

The visible river network hardly changes in total length or largest catchment.
The experiment therefore isolates relief rather than accidentally selecting a
different drainage scale. Sediment production rises strongly with forcing,
while deposition rises only modestly; the present transport law exports most
of the extra eroded volume rather than building broader lowlands. That is
evidence for the later cover, valley-width, and lake-deposition items.

At 250 ky the landscape supplies abundant connected rolling land, but its
gazetteer is dominated by subdued swells and loses convincing mountain groups.
At 750 ky the relief exceeds the world's declared 320 m vertical extent,
nearly half the land is steeper than 31.7 degrees, and steep enclosing walls
again dominate the freshwater and aerial views. The selected 500-ky world has
distinct massifs, forested ridges, meadows, and basin-scale separation. It
also has 2.3 times the low-gradient fraction and 3.5 times the largest
connected low-gradient region of the 750-ky world.

Fine parallel fluting remains conspicuous at 500 ky. That defect did not
respond to river-network scale in this matrix and now passes cleanly to
TER-010: conservative hillslope sediment transport.

## TER-010: conservative hillslope sediment

The elevation-only Laplacian is gone. The same linear-creep calibration now
moves an explicit solid volume once across every cardinal face, with equal and
opposite cell postings, stable internal sweeps, and no flux across fixed
base-level faces. A source consumes mobile cover before the remainder is
reported as bedrock detachment. These transfers join the cumulative eroded and
deposited material histories and the signed sediment residual.

Synthetic peaks round, synthetic basins fill, fixed boundaries do not absorb
material, long geological intervals remain finite, and both surface volume
and mobile-plus-detached solid close. The selected seed-123 Play world then
completed at 2048-square resolution and produced all sixteen gazetteer views
in 96 seconds. Its finished-world cache hit on the next launch.

This item deliberately preserves the accepted terrain shape. Against the
TER-002 500-ky baseline, relief changed from 268.53 to 269.23 m, median slope
from 22.94 to 22.89 degrees, land below ten degrees from 9.96% to 10.07%, and
the largest connected low-gradient region from 0.896 to 0.907 km2. In return,
1.39 billion m3 of formerly implicit internal smoothing appears as matching
additional erosion and deposition; inferred ocean export changes by only
0.15%. The fine fluting remains, now ready to be addressed by physical channel
initiation rather than by an unaccounted elevation filter.

Artifacts are under `/tmp/moppe-hillslope.UGoY4S/gazetteer`.

## TER-011 investigation: channel scale depends on depositional landforms

### Decision

Do not select a new Play channel-initiation area yet. Keep the existing 1 m2
default while TER-020 and TER-021 add cover-aware transport and valley-floor
deposition, then rerun this matrix. A 7 m2 candidate looks attractive at the
2048 Play resolution, but it is approximately one 5.96 m2 grid cell and is
therefore a resolution coincidence rather than a defensible physical channel
head. At 1024 resolution one cell is 23.84 m2 and the same threshold again
applies full fluvial incision everywhere.

### Controlled comparison

`--channel-initiation-area` now enters the immutable recipe and both cache
identities in square metres. Every gazetteer also retains the completed
float32 elevation field; `tools/terrain-spectrum` measures its 10--200 m
radial spectrum. `tools/terrain-channel-matrix` generated optimized seed-123
Play worlds at 1, 4, 7, 10, 16, 25, 100, 400, and 1200 m2 with 500 ky uplift,
fixed high-quality renderer settings, and immediate recipe-specific cache-hit
checks.

| Reading | 1 m2 | 7 m2 | 10 m2 | 25 m2 | 1200 m2 |
| --- | ---: | ---: | ---: | ---: | ---: |
| Median slope | 22.89 deg | 19.89 deg | 16.96 deg | 15.45 deg | 16.47 deg |
| Land at or below 10 deg | 10.07% | 14.89% | 21.60% | 30.06% | 26.51% |
| Largest connected land at or below 10 deg | 0.91 km2 | 1.31 km2 | 1.53 km2 | 0.80 km2 | 0.51 km2 |
| Visible river length | 44.33 km | 40.95 km | 39.63 km | 24.22 km | 20.90 km |
| Inland water area | 0.09 km2 | 0.20 km2 | 0.49 km2 | 2.06 km2 | 2.66 km2 |
| Spectral peak excess | +0.178 dex | +0.140 dex | +0.149 dex | +0.515 dex | +0.183 dex |

The 4 m2 result is nearly the baseline. Seven square metres improves slopes,
connected gentle land, and the spectral reading while retaining 92% of the
visible river length and the full headwater--confluence--mouth view set. At
10 m2 wet cells are already 54% above baseline. From 25 m2 upward, fixed views
show broad waterlogged basins, perched waterfalls, ribbed remnants, and sheer
channel or coastal walls; river length roughly halves. These are not merely
the historically reported dull hillsides.

The threshold is removing the small-catchment incision that currently serves
as a basin-drainage and valley-making mechanism. Conservative linear creep
cannot replace that work over two million years, while one-cell fluvial
deposition cannot build a receiving valley floor. Choosing 7 m2 would hide
that missing process at one resolution. The dependency graph now puts
cover-aware transport and valley-width deposition before TER-011 instead.

Decision artifacts are under `/tmp/moppe-channel.I3IBmK/matrix` and
`/tmp/moppe-channel-fine.SPtKxx/matrix`.

## TER-020: cover-aware discharge capacity

### Decision

Use an effective sediment concentration of **0.00002 at unit slope** with the
default 1 m/yr runoff. This is the knee of the first cover-aware transport
matrix, not the final valley-shape calibration. It gives the next item enough
deposition to spread into valley floors without erasing the selected tectonic
relief first.

### Model and proof

Incoming sediment occupies the typed transport capacity first. Spare capacity
entrains mobile cover before the stream-power result may detach bedrock. The
capacity is duration times contributing area times runoff times effective
sediment concentration times slope times channel share. It therefore has
cubic-metre units and scales linearly with the duration represented by a
geological step.

Synthetic tests cover the exact capacity arithmetic, doubled-time scaling,
cover-first entrainment, bedrock protection under thick cover, deposition
under falling capacity, and closure of the solid-volume ledger. Runoff and
concentration enter the immutable recipe, terrain cache, finished-world cache,
and landscape summary. Finished cache schema version 8 excludes worlds made
by the former potential-incision-scaled law.

### Controlled comparison

`tools/terrain-transport-matrix` generated optimized seed-123 Play worlds at
0.000005, 0.00001, 0.00002, and 0.00004. Uplift remained active for 500 ky of
the two-million-year evolution; channel initiation remained at 1 m2. Every
candidate used a fresh terrain cache, produced its frozen gazetteer and
spectrum, saved a recipe-specific finished world, and loaded that exact cache
on a second launch.

| Reading | 0.000005 | 0.00001 | **0.00002** | 0.00004 |
| --- | ---: | ---: | ---: | ---: |
| Land relief | 307.3 m | 269.6 m | **197.7 m** | 121.2 m |
| Median slope | 34.2 deg | 32.7 deg | **25.4 deg** | 13.5 deg |
| 90th-percentile slope | 47.7 deg | 44.9 deg | **39.2 deg** | 29.0 deg |
| Land at or below 10 deg | 3.49% | 3.68% | **9.69%** | 38.20% |
| Visible river length | 35.90 km | 40.03 km | **38.91 km** | 41.17 km |
| Inland water area | 0.468 km2 | 0.481 km2 | **0.494 km2** | 0.653 km2 |
| Mobile sediment | 52.1 Mm3 | 40.5 Mm3 | **24.7 Mm3** | 13.4 Mm3 |
| Inferred bedrock detached | 0.91 Bm3 | 1.43 Bm3 | **2.14 Bm3** | 2.80 Bm3 |
| Inferred ocean export | 0.85 Bm3 | 1.39 Bm3 | **2.11 Bm3** | 2.79 Bm3 |

At the two lower capacities, deposited cover protects the narrow dissected
surface faster than rivers can rework it. Relief and median slope are as high
as or higher than the TER-010 world, and the gazetteers retain steep parallel
ridges. At 0.00004, the landscape jumps to 38.2% low-gradient land and a
6.95 km2 connected gentle region; local and aerial views are dominated by a
wet low plateau. The selected 0.00002 result retains visible uplands and
mountain groups while introducing river corridors, local rolling ground, and
broad depositional receiving surfaces.

The matrix also exposed and fixed a topology-boundary defect in watercourse
painting: a bank sample just below a periodic seam could round to exactly one
full lattice width. Watercourse ground sampling now uses the shared continuous
wrap operation, with a regression test for that floating-point boundary.

Decision artifacts are under `/tmp/moppe-transport.vFm51d/matrix`.

## TER-021: valley-width deposition

### Decision

Place routed deposition across a local alluvial footprint whose full width is
`clamp(6 m + 0.04 * sqrt(area), 6 m, 160 m)`. Keep the routing graph and its
capacity ledger one-dimensional. Lateral placement is a second operation that
changes destinations, never the volume routed out of suspension.

The footprint follows the remembered channel tangent for one short physical
cell segment. A local wall threshold of `1 m + 0.08 * width` prevents the
posting from painting sediment indiscriminately up valley sides. Eligible
cells are ordered by their current surface elevation and filled toward one
common level, which makes a slope break grow a floor rather than a centerline
berm or a fixed raster-width terrace.

### Proof and optimized result

Synthetic tests establish three gates: the width law grows continuously from
6.04 m at 1 m2 to 10 m at 10,000 m2 and 46 m at 1 km2; all lateral postings
sum exactly to the centerline source; and a flat-bottomed cross-section rises
uniformly across its three-cell bottom without depositing on the walls.

The first optimized seed-123 Play acceptance run held the TER-020 calibration,
500-ky uplift, two-million-year evolution, renderer, and capture settings
fixed. World cache schema 9 excludes the old one-cell geography. The run made
all sixteen frozen views in 113 seconds, compared with 111 seconds for the
matching TER-020 candidate, and its exact finished-world cache loaded on the
next launch.

| Reading | Centerline | Valley width |
| --- | ---: | ---: |
| Land relief | 197.65 m | 194.63 m |
| Median slope | 25.44 deg | 25.49 deg |
| 90th-percentile slope | 39.20 deg | 39.58 deg |
| Land at or below 10 deg | 9.69% | 10.36% |
| Largest connected land at or below 10 deg | 0.138 km2 | 0.423 km2 |
| Visible river length | 38.91 km | 36.55 km |
| Inland water area | 0.494 km2 | 0.598 km2 |
| Mobile sediment | 24.65 Mm3 | 24.99 Mm3 |
| Inferred bedrock detached | 2.136 Bm3 | 2.156 Bm3 |
| Inferred ocean export | 2.111 Bm3 | 2.131 Bm3 |

The near-constant slope percentiles and three-metre relief change show that
the result is not another global smoothing pass. The threefold growth in the
largest connected gentle region is local rearrangement into joined receiving
floors. River, confluence, mouth, lake, coast, and aerial views remain present;
the freshwater approaches visibly occupy broader ground while mountain groups
remain intact.

Decision artifacts are under `/tmp/moppe-valley.Z16cEc/gazetteer`.

## TER-011 revisit: physical thresholds still expose undrained basins

### Decision

Do not select a physical channel-initiation threshold after TER-021. Keep the
1 m2 all-cell fallback in Play while TER-022 adds standing-water accommodation
and mouth deposition. This is not completion of TER-011: the selected default
still lies below one 5.96 m2 Play cell and therefore does not establish the
RFC's channel/hillslope distinction.

### Optimized coarse matrix

The resumable `tools/terrain-channel-matrix` ran the optimized RelWithDebInfo
build (`-O2 -g -DNDEBUG`) at 1, 25, 100, 400, and 1200 m2 after both
cover-aware transport and valley-width deposition. Each completed candidate
used a fresh terrain cache, made a frozen gazetteer and spectrum, saved a
recipe-specific finished world, and loaded it on the verification launch. The
tool now records generation failures and continues later anchors instead of
discarding a long experiment.

| Reading | 1 m2 | 25 m2 | 100 m2 | 1200 m2 |
| --- | ---: | ---: | ---: | ---: |
| Land relief | 194.6 m | 222.9 m | 266.5 m | 332.8 m |
| Median slope | 25.5 deg | 47.8 deg | 70.0 deg | 48.0 deg |
| 90th-percentile slope | 39.6 deg | 64.8 deg | 79.4 deg | 80.9 deg |
| Land at or below 10 deg | 10.36% | 4.50% | 1.09% | 11.83% |
| Largest connected gentle land | 0.423 km2 | 0.016 km2 | 0.0005 km2 | 0.095 km2 |
| Visible river length | 36.55 km | 28.20 km | 7.69 km | 3.30 km |
| Inland water area | 0.598 km2 | 1.594 km2 | 3.296 km2 | 0.988 km2 |
| Spectral peak excess | +0.463 dex | +0.684 dex | +0.717 dex | +0.312 dex |

The 25 m2 contact sheet is already dominated by parallel fins, perched water,
and abrupt coastal faces. At 100 m2 the fixed cameras frequently sit inside
near-vertical walls, the confluence view disappears, and forest
representatives fall from 64,090 to 9,839. The 400 m2 terrain fails the normal
world gate because no complete home-base expedition circuit exists. The
1200 m2 non-monotonic low-gradient fraction comes from isolated plateaus above
sheer walls, not rideable rolling geography.

Cover feedback and lateral deposition therefore did not replace the process
being removed: below-threshold incision still drains local basins and cuts
their outlets. TER-022 must let standing water retain incoming solid over its
available bed and evolve mouths/outlets before TER-011 can be tried honestly
again.

Decision artifacts are under `/tmp/moppe-channel-cover.nU3Bp4/matrix`.

## TER-022: standing-water storage and mouth deposition

### Decision

Give every censused inland body one shared storage budget equal to the sum of
its cell accommodation, bounded by the existing per-step aggradation limit.
An entering load consumes that budget once for the body and carries any excess
along the already-solved drainage DAG. Distribute retained load across the
eligible lake bed in proportion to local capacity, so the body fills as a
surface rather than growing a routed-cell tower.

At the coast, retain only the volume admitted by a meter-scaled mouth
footprint. Place it in a tangent-oriented fan that widens downstream, enforce
the accommodation and per-step limit globally across overlapping fans, and
record everything that does not fit as ocean export. Flood and drainage are
recomputed after every geological step, so accumulated fill changes later
water bodies and outlet routes without any one step crossing the water
surface. World cache schema 10 excludes the former standing-water geography.

### Synthetic proof

The routing fixture sends ten cubic metres through a four-cubic-metre lake
budget and a three-cubic-metre mouth budget, retaining seven and exporting
three with zero ledger residual. A finite nine-cell lake receives six cubic
metres over all nine cells, stays below its flood surface, and has less water
depth on the next analysis. A deliberately overloaded coastal source spreads
over multiple ocean cells, never exceeds any one-cell capacity, and explicitly
exports the remainder. The optimized build passes all 250 tests.

### Optimized Play result

The seed-123 acceptance held the TER-020 concentration, 500-ky uplift,
two-million-year duration, one-square-metre channel fallback, renderer, and
capture settings fixed. The finished 16 MB elevation field was byte-identical
before and after compacting the mouth scratch storage. The compact fresh run
made all seventeen gazetteer views in 115 seconds, compared with 113 seconds
for TER-021, and the version-10 finished-world cache loaded on the next launch.

| Reading | TER-021 | Standing water |
| --- | ---: | ---: |
| Land relief | 194.63 m | 199.16 m |
| Median slope | 25.49 deg | 17.22 deg |
| 90th-percentile slope | 39.58 deg | 40.13 deg |
| Land at or below 10 deg | 10.36% | 27.28% |
| Largest connected gentle land | 0.423 km2 | 2.496 km2 |
| Visible river length | 36.55 km | 34.58 km |
| Inland water area | 0.598 km2 | 1.041 km2 |
| Mobile sediment | 24.99 Mm3 | 31.67 Mm3 |
| Inferred bedrock detached | 2.156 Bm3 | 1.309 Bm3 |
| Inferred ocean export | 2.131 Bm3 | 1.278 Bm3 |
| Spectral peak excess | +0.463 dex | +0.435 dex |

This is the first process addition in the track that changes the global
landscape class rather than merely broadening a local floor: the fixed views
now include extensive rolling lowlands, while mountain groups and roughly
two-hundred-metre relief remain. River, confluence, lake, and mouth targets all
resolve. The mouth view shows a shallow vegetated approach entering the sea;
the lake overview also exposes many small satellite basins, which should be
watched during the renewed channel matrix rather than hidden by a threshold
change.

Decision artifacts are under
`/tmp/moppe-standing-water-perf.7FQs7m/gazetteer`.
