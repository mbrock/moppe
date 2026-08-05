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
