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

## Conservation boundary

The fluvial detachment and transport pass is conservative. The existing
Laplacian hillslope diffusion pass still changes elevation after that ledger
and does not yet route a face-by-face sediment flux or update mobile sediment.
It must not be described as part of the conservative sediment budget. A later
replacement should post equal and opposite solid-volume transfers across
cell faces and feed the same stored sediment thickness.
