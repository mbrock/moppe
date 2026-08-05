# Sediment transport

The stream-power solve now proposes detachment; it no longer deletes that
material from the world. A second pass walks the same D-infinity drainage DAG
from sources to outlets and routes a single solid sediment volume.

For each non-ocean cell in one geological step, incoming load occupies the
available transport capacity first. Any spare capacity entrains stored mobile
cover and only then detaches the bedrock allowed by the stream-power solve:

```text
spare             = max(0, capacity - incoming)
entrained cover   = min(stored cover, spare)
bedrock detached  = min(potential incision, spare - entrained cover)
available         = incoming + entrained cover + bedrock detached
deposited         = min(max(0, available - capacity), aggradation limit)
outgoing          = available - deposited
```

Incoming material consumes transport capacity before the cell may detach more
of its own surface. Material above both capacity and the local aggradation
limit continues downstream instead of forming a one-cell sediment tower. The
default limit is a 0.01 mm/yr deposition rate, or 0.5 m in a 50,000-year step.
A non-ocean sink retains all incoming material. An ocean cell records incoming
material as export. Fractional routes post the final arc as the exact
remainder, rather than rounding every arc independently, so each step has the
explicit ledger:

```text
entrained cover + bedrock detached
  = deposited + exported to ocean + balance residual
```

The residual is carried as a signed physical volume and tested at zero. The
terrain stores mobile sediment thickness as well as cumulative geological
detachment and deposition thickness. Later incision removes mobile sediment
before it reaches the underlying surface. Trail cut and fill remains in the
trail network's `earthwork_delta_m`; it does not rewrite geological history.

## Valley-width deposition

Routing still decides the exact centerline volume that leaves the moving load.
A separate conservative placement stage decides which ground cells receive
that volume. Its physical full width is:

```text
clamp(6 m + 0.04 * sqrt(contributing area), 6 m, 160 m)
```

The channel tangent orients a short cross-valley footprint. Local cells above
the channel by more than `1 m + 0.08 * width` are treated as valley walls and
excluded. Within the remaining footprint, sediment raises the lowest cells
toward a common floor elevation before higher cells receive anything. The
last lateral posting receives the exact volume remainder, just as the final
downstream arc does, so spreading changes geography without changing the
solid ledger.

Width depends on square metres rather than a number of raster cells. A coarse
grid resolves a narrow headwater footprint with fewer samples, but it does
not ask for a physically wider valley. The short along-channel radius follows
the source cell's physical diagonal; overlapping source footprints join into
continuous downstream floors over geological steps.

## Discharge-based transport capacity

The backward-Euler stream-power result supplies only the upper bound on new
bedrock detachment. Transport capacity is independent of that result:

```text
capacity = duration * contributing area * runoff
           * sediment concentration at unit slope
           * local slope * channel share
```

Every factor retains physical units through the calculation. Capacity scales
linearly with geological step duration, so changing the time discretization
does not silently change its interpretation. The Play calibration uses 1 m/yr
of runoff and a dimensionless concentration of 0.00002 at unit slope. The
concentration is an effective bulk-solid calibration rather than a claim
about one measured river's instantaneous suspended load.

A seed-123 capacity matrix selected 0.00002 at the knee between two failure
regimes. Lower capacities let cover protect the already narrow ridges and
leave median slopes above 32 degrees. A concentration of 0.00004 collapses
relief to 121 m and joins 6.95 km2 of land below ten degrees. The selected
world retains 198 m of relief while exposing connected river valleys and
depositional plains. `--sediment-concentration` remains available for
controlled experiments and participates in both terrain and finished-world
cache identity.

This is deliberately a small first model. It has one solid material, no grain
classes, density, porosity, compaction, suspension, or dissolved load. Ocean
export is outside the modeled land surface rather than an offshore deposit.
Lakes currently receive sediment through local valley footprints rather than
spreading it over a delta or lake bed.

Fluvial incision already has a typed physical channel-initiation area and a
smooth fourfold transition around it. The default remains 1 m2 for now. A
same-seed 1--1200 m2 matrix found that physically resolved thresholds expose
perched basins and sheer channel walls before cover feedback and distributed
valley deposition exist. The experiment is available through
`--channel-initiation-area`; it is not yet a selected process calibration.

## Conservative hillslope transport

Hillslope creep is now the second conservative solid pass. For each cardinal
cell face and stable internal sweep it evaluates the linear diffusive volume:

```text
V = diffusivity * sweep duration * face width / face run * height difference
```

The higher cell receives a `-V` posting and the lower cell receives `+V` from
that one calculation. Periodic east and south faces enumerate the complete
lattice without independently rounding two directions of the same face.
Faces touching an ocean or other fixed base-level cell are explicitly
no-flux; hillslope creep neither changes that boundary nor invents an
unreported coastal export.

Each geological interval divides into the same explicit-stability sweeps the
former Laplacian required. Existing mobile cover supplies a cell's outgoing
postings first. Only an outgoing remainder beyond that cover detaches bedrock;
all incoming solid becomes mobile cover at its destination. Cumulative eroded
and deposited thickness therefore includes hillslope work, and the report
retains hillslope transfer, bedrock detachment, sweep count, and its signed
zero-sum residual separately.

## Conservation boundary

Fluvial routing and hillslope transport now share one solid-volume ledger.
The only geological material leaving it is explicit fluvial ocean export;
fixed hillslope faces are no-flux. Trail cut and fill remains a separately
owned construction ledger. Offshore deposition and distributed lake storage
remain later work.
