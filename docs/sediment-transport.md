# Sediment transport

The stream-power solve now proposes detachment; it no longer deletes that
material from the world. A second pass walks the same D-infinity drainage DAG
from sources to outlets and routes a single solid sediment volume.

For each non-ocean cell in one geological step:

```text
available = incoming + detached
excess    = max(0, available - transport capacity)
deposited = min(excess, local aggradation limit)
outgoing  = available - deposited
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
detached = deposited + exported to ocean + balance residual
```

The residual is carried as a signed physical volume and tested at zero. The
terrain stores mobile sediment thickness as well as cumulative geological
detachment and deposition thickness. Later incision removes mobile sediment
before it reaches the underlying surface. Trail cut and fill remains in the
trail network's `earthwork_delta_m`; it does not rewrite geological history.

## Capacity used by the first model

The backward-Euler stream-power result supplies potential detachment. Its
local transport capacity uses that volume as the one-cell calibration, then
extends the stream-power catchment response from `A^m` to `A^1`. This lets a
trunk channel carry the aggregate supply of its unit catchments when slope is
maintained. Where slope and potential incision collapse, capacity collapses
too and the excess is deposited.

This is deliberately a small first model. It has one solid material, no grain
classes, density, porosity, compaction, suspension, or dissolved load. Ocean
export is outside the modeled land surface rather than an offshore deposit.
Lakes currently receive sediment at their routed cells rather than spreading
it over a delta or lake bed.

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
