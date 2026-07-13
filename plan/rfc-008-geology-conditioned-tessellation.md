# RFC-008: Near-field tessellation with geology-conditioned detail

- Status: Draft
- Area: rendering (terrain geometry)
- Interacts with: RFC-003 (its amplitude inputs), RFC-006 (channel
  banks), RFC-007 (waterline snap); extends item 5 of
  `ideas/geometry-from-fields.md`

## Problem

Within arm's reach of the bike the terrain is still the quarter-cell
Catmull-Rom reconstruction of 2.44 m samples: smooth, but featureless
below the lattice scale.  Banks, channel lips, ridgelines, and slopes
have no metre-scale relief of their own, and the splat textures carry
all close-range character alone.  Generic detail noise would add bumps,
but bumps that ignore the simulation read as texture, not terrain.

## Current situation

- The near field renders through the Subdivided LOD: bounded Catmull-Rom
  heights and surface-derived normals in `terrain.metal`, morphing back
  to the authoritative lattice by `LOD_MORPH_START[0]` (64 cells,
  `moppe/game/terrain.cc`).
- Physics stays on the authoritative bilinear surface; the render
  surface is already allowed to differ within each source cell's corner
  range -- there is precedent for a bounded divergence envelope.
- Sub-metre shading detail exists only as pebble-scale bump in shallow
  beds and triplanar cliff strata (`docs/renderer-design.md`).
- RFC-003 gives per-cell eroded/deposited rasters; drainage supplies
  flow direction; moisture exists as a raster already
  (`set_terrain_moisture`).

## Proposal

Tessellate the near field (~60 m) with edge factors from projected edge
length, and displace the new vertices by a **detail spectrum conditioned
on the simulation's own fields**:

- *Where eroded* (RFC-003): high-frequency, anisotropic relief --
  rills and gully micro-ridges elongated **along the drainage flow
  direction**, so runnels visibly point downhill because the drainage
  says so.
- *Where deposited*: low-amplitude, low-frequency undulation -- smooth
  alluvium and fan surfaces.
- *Steep + dry*: blocky rock character aligned to the triplanar strata.
- *Wet margins* (moisture / RFC-007 distance): soft, slumped soil.

Constraints that keep it well behaved:

- **Bounded displacement**: total detail amplitude clamped to a stated
  envelope (order +-0.3 m), the same philosophy as the Catmull-Rom
  corner bound -- the render surface may never invent geometry physics
  could care about.  Suspension absorbs the visual difference; blob
  shadows and wheel contact continue to use the authoritative map.
- **Fade before the morph**: detail amplitude reaches zero before the
  Subdivided->Native morph band, so the LOD hand-off stays exact.
- **Deterministic**: displacement is a pure function of world position
  and the field textures -- no stored geometry, nothing to stale, per
  the `geometry-from-fields` premise.

Mechanically: Metal tessellation (works on the iPhone target) or, on
Apple silicon, denser emission in a mesh-shader object/mesh pair like
grass and near-water already use -- whichever measures better; the
displacement logic is identical.

## Consequences

- The ground gains metre-scale structure that *agrees with its own
  history*: gullies where erosion carved, silk where it deposited.
  Pairs with parallax pebbles (ideas item 3): tessellation carries the
  metre scale, parallax the centimetre scale.
- Channel lips and banks (RFC-006) acquire real curvature exactly where
  the eye lands when riding a river line.

## Risks and alternatives

- Vertex-load: near-field tiles at quarter-cell spacing are already
  ~10^5 vertices; factor-4 tessellation multiplies that.  Budget via the
  object stage (distance-scaled factors), and let the graphics benchmark
  cube (docs/game-state.md) referee.
- Physics divergence complaints (visual hollow the wheel ignores): the
  envelope keeps it below suspension amplitude; if it ever matters, the
  displacement function is CPU-evaluable by construction (same fields,
  same hash) and could join the physics composite selectively.
- Alternative: bake a displacement raster at 4x lattice.  Loses the
  per-frame adaptivity and memory-free property; keep as fallback if
  shader ALU measures badly.

## Implementation sketch

1. Flat tessellation of the near field with zero displacement -- verify
   crack-free morph and cost.
2. Displacement function: amplitude/character from (eroded, deposited,
   slope, moisture), anisotropy from flow direction; share the hash and
   value-noise utilities grass already uses.
3. Waterline snap (RFC-007) and channel-lip handling (RFC-006).
4. Benchmark + capture sweep; tune per graphics preset (a `low` preset
   simply skips tessellation).
