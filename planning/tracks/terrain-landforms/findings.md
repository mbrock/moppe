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
