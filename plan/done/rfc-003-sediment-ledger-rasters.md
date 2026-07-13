# RFC-003: Keep the sediment ledger as per-cell rasters

- Status: Draft
- Area: terrain simulation -> rendering handoff
- Interacts with: RFC-008 (consumes it), RFC-006, terrain materials

## Problem

The world knows exactly where it was gullied and where sediment settled --
and throws that knowledge away.  Droplet erosion computes signed per-cell
height deltas every batch, commits them, and keeps only global totals.
Materials and surface detail then have to *guess* geology from altitude
and slope, when the simulation had the true answer in hand.

## Current situation

- `RandomHeightMap::erode_hydraulically` (`moppe/map/generate.cc:554`)
  accumulates a sparse `changes[]` buffer plus a `touched` list per batch,
  commits, and clears.  `HydraulicErosionReport` retains only global sums
  (eroded, deposited, discarded, termination counts).
- `erode_analytically` computes `current[] - initial[]` per cell for its
  report (`moppe/terrain/analytical_erosion.cc:221`), then discards the
  field.
- The terrain shader blends grass/dirt/rock/snow by altitude and slope,
  plus moisture darkening; nothing distinguishes a fresh erosion cut from
  a depositional fan.
- `ideas/geometry-from-fields.md` proposes a "field atlas" of per-cell
  control channels; this RFC supplies its two most physically honest
  channels for free.

## Proposal

Accumulate two lifetime rasters during every erosive transform:

    total_eroded[cell]    += max(0, -delta)
    total_deposited[cell] += max(0,  delta)

split at commit time from the very `changes[]` values already computed.
Store them beside the heightmap (same `Array2D<float>` shape), thread them
through `TerrainEvaluator` checkpoints like the height data, and upload as
one RG16F texture at world handoff.

Consumers, in order of value:

1. **Terrain materials**: deposited -> smooth pale alluvium and fan
   material in valley floors; eroded -> raw dirt / banded rock on gully
   walls and fresh cuts.  A two-channel texture sample added to the
   existing splat logic.
2. **Detail displacement** (RFC-008): rough, flow-aligned high-frequency
   relief where eroded; silky low-frequency where deposited.
3. **Lab readings**: `Eroded` and `Deposited` overlays in the Map Readings
   window -- the existing R32F overlay path (`set_terrain_overlay`) needs
   no renderer changes at all.
4. Later: moisture and vegetation density modifiers (deposits hold water).

## Consequences

- The cheapest high-coherence visual upgrade available: the data already
  exists every run; the change is two adds in the commit loop and one
  texture upload.
- Materials become *consequences of process* rather than decorated noise
  -- the through-line of `ideas/geometry-from-fields.md`.
- The ledger becomes spatially auditable: the Lab can finally show not
  just how much sediment moved but where.

## Risks and alternatives

- Memory: two float rasters at 2049^2 is ~33 MB CPU-side during
  generation; the uploaded RG16F texture is ~17 MB.  Acceptable; could be
  RG8 after normalization if it ever matters.
- Checkpoint size grows; the Lab's exact-replay property must include the
  ledger (restore both rasters at stage inputs).  Straightforward since
  checkpoints already snapshot height + RNG state
  (`map::TerrainCheckpoint`).
- Alternative considered: recompute a proxy from curvature at load time.
  Rejected -- the true ledger is available, and curvature cannot separate
  a noise-born hollow from a carved one.

## Implementation sketch

1. Add the two accumulation arrays to `RandomHeightMap`, filled in the
   batch commit loop and in the analytical pass epilogue; a reset hook at
   program start.
2. Extend `TerrainCheckpoint` and evaluator replay.
3. `Renderer::set_terrain_geology(std::span<const float> rg)` (or fold
   into the planned field atlas); sample in `terrain.metal`'s fragment
   material blend.
4. Two new Lab overlay modes reusing `TerrainOverlayRamp::Heat` and
   `Diverging`.
