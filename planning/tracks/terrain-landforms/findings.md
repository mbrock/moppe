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

## TER-011 after standing-water deposition

### Decision

Do not select the visually strongest 10 m2 result. It is only 1.68 cells at
2048 resolution and lies below one 23.8 m2 cell at 1024, so it repeats the
resolution coincidence already rejected after the first matrix. Return
TER-011 to backlog behind explicit outlet erosion.

### Optimized matrix

The renewed seed-123 matrix used the merged Apple-SIMD `Vec3` path, cache
schema 10, fresh terrain caches, full fixed gazetteers, spectra, and verified
finished-world cache hits. All six worlds completed in 113--121 seconds.

| Reading | 1 m2 | 7 m2 | 10 m2 | 16 m2 | 25 m2 | 100 m2 |
| --- | ---: | ---: | ---: | ---: | ---: | ---: |
| Relief (m) | 199.6 | 207.3 | 214.0 | 225.8 | 241.4 | 292.0 |
| Median slope (deg) | 17.5 | 15.1 | 12.6 | 13.0 | 15.8 | 23.4 |
| P90 slope (deg) | 39.9 | 38.8 | 37.9 | 41.9 | 49.7 | 62.4 |
| Land <= 10 deg | 26.7% | 33.4% | 41.3% | 41.1% | 35.6% | 26.5% |
| Connected gentle land (km2) | 2.53 | 2.73 | 3.26 | 2.55 | 1.98 | 1.49 |
| River length (km) | 34.9 | 33.5 | 31.9 | 30.3 | 25.4 | 22.5 |
| Inland water (km2) | 1.06 | 1.39 | 1.85 | 2.37 | 3.10 | 3.73 |
| Spectral excess (dex) | .444 | .397 | .357 | .489 | .644 | .612 |

TER-022 prevents the immediate catastrophic collapse seen in the earlier
25 m2 world: all candidates now retain substantial connected gentle land and
complete the trail gate. But the response beyond the one-cell sweet spot is
still wrong. Water area rises monotonically, the confluence target disappears
at every threshold from 7 m2 upward, river length falls, and upper-tail slope
returns. The 25 and 100 m2 fixed views again show abrupt spillway walls and
fluted mountain faces.

The mechanism is direct rather than conjectural, but the relevant identities
must remain distinct. `LakeCensus::outlet_cell` is the final submerged cell and
should remain depositional. Its `spill_cell` is the dry sill immediately
downstream. That sill already uses the conservative cover-first router, but
the ordinary catchment threshold can suppress its stream-power incision even
though overflow has already concentrated there. TER-023 should give only
designated spill cells full channel share and leave the submerged bed
depositional.

Decision artifacts are under
`/tmp/moppe-channel-standing-water.4vpeRS/matrix`.

## TER-023: designated spillway incision is too narrow

The synthetic hypothesis was sound: marking each census `spill_cell` as
concentrated overflow lets that dry sill bypass channel-head suppression,
lowers it through the existing cover-first sediment ledger, and leaves both
the submerged `outlet_cell` and unrelated lake bed unchanged. The optimized
25 m2 Play A/B nevertheless rejects it as the missing world process.

| Reading | Standing water | Spillway override |
| --- | ---: | ---: |
| Land relief | 241.44 m | 241.38 m |
| Median slope | 15.84 deg | 16.03 deg |
| P90 slope | 49.70 deg | 49.73 deg |
| Connected gentle land | 1.977 km2 | 1.992 km2 |
| River length | 25.41 km | 25.06 km |
| Inland water | 3.097 km2 | 3.065 km2 |
| Spectral excess | +0.644 dex | +0.647 dex |

The result still loses the confluence target and retains the same fluted
mountain walls. The implementation was removed. The broader discontinuity is
that channel share multiplies both the implicit incision velocity and solid
transport capacity down to zero. Below a physical channel head the model has
linear creep but no rainfall-driven diffuse wash at all. TER-024 should make a
small conservative wash share explicit, with the channel threshold controlling
only the concentrated increment.

Decision artifacts are under
`/tmp/moppe-channel-spillway.M7TsXP/matrix/channel-25`.

## TER-024: diffuse stream power is still stream power

The experiment retained an explicit fraction of both implicit incision and
transport capacity below the channel head. Zero reproduced the TER-022
threshold result exactly; 5%, 10%, and 20% were bounded, cover-first, and
recipe-specific. All four optimized 2048-square worlds completed in 114--127
seconds and reloaded their finished-world caches.

| Reading | 0% | 5% | 10% | 20% |
| --- | ---: | ---: | ---: | ---: |
| Relief (m) | 241.4 | 238.0 | 235.1 | 228.8 |
| Median slope (deg) | 15.84 | 15.97 | 15.74 | 15.54 |
| P90 slope (deg) | 49.70 | 48.88 | 48.09 | 47.15 |
| Land <= 10 deg | 35.6% | 35.0% | 35.3% | 35.0% |
| Connected gentle land (km2) | 1.98 | 1.87 | 1.49 | 1.51 |
| River length (km) | 25.4 | 23.2 | 27.2 | 28.3 |
| Inland water (km2) | 3.10 | 3.13 | 2.84 | 2.64 |
| Spectral excess (dex) | .644 | .634 | .638 | .627 |

The weak relief response is real, but it does not create the missing
geography. Connected rolling ground shrinks, spectral evidence barely moves,
and the fixed eroded-slope and highland views retain the same close parallel
fluting. The 20% candidate also loses the confluence target. The implementation
was removed: scaling stream power down still lets unchannelized catchments cut
bedrock and therefore cannot establish the requested process distinction.

The next bounded hypothesis is TER-025. The existing conservative hillslope
flux is linear and has only a 14 m two-million-year smoothing length. A bounded
critical-gradient multiplier can selectively accelerate cover-first movement
on steep faces while leaving subcritical rolling ground and channel initiation
semantically distinct.

Decision artifacts are under `/tmp/moppe-diffuse-wash.aUfftR`.

## TER-025: bounded critical-gradient hillslopes

The accepted mechanism leaves the linear face flux exact below half a typed
critical gradient, then smoothly raises its diffusivity to a bounded
multiplier. The active factor participates in the explicit stability bound;
every posting remains equal-and-opposite, cover-first, and part of the same
solid-volume ledger. Multiplier one is the old implementation exactly.

At a 0.8 critical gradient and 25 m2 channel head, the optimized matrix was:

| Reading | 1x | 2x | 4x |
| --- | ---: | ---: | ---: |
| Relief (m) | 241.4 | 242.9 | 218.9 |
| Median slope (deg) | 15.84 | 15.52 | 15.31 |
| P90 slope (deg) | 49.70 | 45.44 | 34.16 |
| P99 slope (deg) | 68.81 | 73.23 | 56.15 |
| Land <= 20 deg | 59.9% | 63.2% | 64.6% |
| River length (km) | 25.4 | 23.5 | 22.2 |
| Inland water (km2) | 3.10 | 2.92 | 2.76 |
| Spectral excess (dex) | .644 | .552 | .416 |
| Generation (s) | 120 | 126 | 134 |

The 2x case merely moves the failure around; its P99 tail worsens and its fixed
eroded-slope view contains sharper pinnacles. The 4x case materially rounds
the world, retains every capture target, and costs fourteen seconds. This
closes the hillslope-process gate without granting below-channel stream power.

Decision artifacts are under `/tmp/moppe-critical-hillslope.x8Cjln`.

## TER-011: a physical channel head after nonlinear hillslopes

With the 0.8/4 hillslope candidate, 100 and 400 m2 channel heads both recover
all seventeen targets that the earlier linear-hillslope worlds lost. The 400
m2 stress case is still wrong: relief reaches 305 m, P90 slope 52 degrees, and
the fixed coast shows sheer walls. The viable 100 m2 world led to one focused
refinement, lowering the critical gradient to 0.6 so transport begins to
accelerate at a 0.3 face gradient while remaining bounded at four.

| Reading | 2048 | 1024 |
| --- | ---: | ---: |
| Cell area | 5.96 m2 | 23.84 m2 |
| Channel head | 100 m2 | 100 m2 |
| Relief (m) | 257.4 | 263.4 |
| Median slope (deg) | 19.68 | 19.28 |
| P90 slope (deg) | 45.45 | 37.93 |
| Land <= 20 deg | 51.2% | 52.6% |
| Connected <= 20 deg (km2) | 5.71 | 9.63 |
| River length (km) | 22.27 | 19.57 |
| Inland water (km2) | 2.66 | 1.97 |
| Spectral excess (dex) | .713 | .476 |
| Generation (s) | 135 | 36 |

This is the first selected channel scale that represents multiple cells at
both resolutions. The 2048 world resolves every fixed target. The 1024 world
keeps river and mouth targets but has no selected confluence despite its broad
rolling geography. TER-030 owns that remaining river-scale discrepancy; the
channel head will not be lowered back to a resolution coincidence to hide it.

The selected values are calibration inputs, not Play defaults. TER-040 remains
the only item allowed to change those defaults after the multi-seed gate.

Decision artifacts are under `/tmp/moppe-hillslope-refined.0DLaGE`,
`/tmp/moppe-hillslope-1024.euWbKA`, and
`/tmp/moppe-channel-critical.76wl4R`.

## TER-030: physical river extraction and trunk scale

The 1024 confluence failure came from the presentation threshold rather than
the drainage topology. `visible_river_minimum_area` formerly asked when the
water-width law reached two source cells. That selected about 0.174 km2 at
2048 and 0.694 km2 at 1024, so the lower-resolution finished world discarded
tributaries four times larger before water selection and painting.

The accepted threshold is the catchment that produces a physical five-metre
water width: 173,611 m2 at every resolution. The same contributing-area
coordinate drives the established depositional footprint:

| River class | Catchment | Water width | Water depth | Depositional floor |
| --- | ---: | ---: | ---: | ---: |
| Visible lower boundary | 0.174 km2 | 5.0 m | 0.63 m | 22.7 m |
| Seed-123 trunk | 13--14 km2 | 24.0 m | 2.5 m | about 150 m |

The lower member reads as a substantial stream and the capped trunk as a
small motorcycle-scale river, which is honest for 22 km2 of land inside a
five-kilometre periodic world. The ratio between water and alluvial floor
grows only modestly, rather than assigning a broad floodplain to a painted
rivulet or forcing water to fill the whole valley.

Re-analyzing the accepted 1024 terrain raises extracted river length from
19.57 to 36.18 km and restores the missing confluence. It yields 710 reaches,
a 1.617 km/km2 drainage density, and all 17 fixed views. The 2048 terrain has
733 reaches, 21.34 km of river, a 0.984 km/km2 density, a 13.07 km2 maximum
catchment, and all 17 views. Network length is not resolution-converged
because the underlying terrain drainage differs, but physical extraction no
longer injects an additional fourfold scale change. The stream, river,
confluence, and mouth views retain a believable widening hierarchy at both
resolutions.

Cache schema 12 invalidates version-11 finished worlds because river topology
is stored in the cache. Decision artifacts are under
`/tmp/moppe-river-1024.koPI8c` and `/tmp/moppe-river-2048.B3UBjA`.
