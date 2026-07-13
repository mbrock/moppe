# Orogeny water-depth distribution

Date: 2026-07-13

## Question

After selecting uplift-driven Orogeny for ordinary worlds, how much standing
water does it produce, and how deep are the sea, lakes, ponds, and rejected
puddles? This also tests whether the renderer's conspicuous shoreline motion
corresponds to a physically broad shallow-water band.

## Method

`terrain-water-depth-experiment` generates complete Orogeny heightfields at
the game's physical extent of 5 km by 5 km by 320 m and analyzes the same
priority-flood surface and lake census used by gameplay. Depth observations
are area-weighted terrain cells. `Sea`, `Lake`, and `Pond` are permanent water;
`Puddle` bodies fail at least one permanence threshold and are not painted into
the gameplay water surface.

The main sample used twelve consecutive seeds at the actual Fast resolution
and duration:

```sh
./build/terrain-water-depth-experiment 1025 12 123 fast
```

One full Play-resolution world checked that the calibrated source remains
bathymetric through the longer, higher-resolution evolution:

```sh
./build/terrain-water-depth-experiment 2049 1 123 play
```

## Failure and calibration

The first implementation fixed every ocean terrain cell at the water-surface
elevation on every geological step. It consequently produced no positive-depth
Sea cells. Preserving the source bed exposed the other half of the failure:
the symmetric source used a coastline threshold of `0.5` and only 20 m of
relief, yielding an ocean over 49.6% of the world with a mean depth of 1.28 m.

Land and submarine relief are now separate source parameters. The calibrated
Fast source uses a `0.4` coastline threshold, 20 m of emergent seed relief, and
240 m of bathymetric relief. The threshold controls area without requiring
mountains in the initial condition; the bathymetric scale deepens only the
submerged side. Ocean cells remain fixed erosion outlets at their original bed
elevation instead of being lifted to sea level.

## Fast-profile results

| Class | Bodies | Mean world area | Mean depth | P10 | Median | P90 | Maximum |
| --- | ---: | ---: | ---: | ---: | ---: | ---: | ---: | ---: |
| Sea | 12 | 5.65% | 9.46 m | 1.59 m | 8.18 m | 19.57 m | 35.82 m |
| Lake | 82 | 8.51% | 8.07 m | 0.81 m | 5.48 m | 19.83 m | 45.20 m |
| Pond | 315 | 1.07% | 1.35 m | 0.12 m | 0.89 m | 3.23 m | 11.68 m |
| Puddle | 32,228 | 0.50% | 0.27 m | 0.02 m | 0.13 m | 0.68 m | 4.16 m |

Permanent standing water covers about 15.2% of a world on average, compared
with the naive half-world ocean. The global ocean itself covers 5.7%; enclosed
basins account for most of the remaining permanent water. The sea is no longer
a thin sheet: 6.1% of its cells are at most 1 m deep, 12.6% are at most 2 m,
and 31.4% are at most 5 m.

## Shoreline decision

The renderer already carries a per-body wave factor: Sea 1.0, Lake 0.10, Pond
0.04, and Puddle 0.0. Shoreline swash now uses that factor and has a 3 cm
one-sided amplitude instead of 22 cm. Maximum apparent retreat falls from
44 cm for every standing body to about 6 cm for the sea, 6 mm for a lake, and
2.4 mm for a pond. The extra phase-dependent opacity term was removed so the
edge fades from effective water depth only.
