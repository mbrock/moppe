# RFC-007: Waterline-conforming geometry

- Status: Draft
- Area: rendering (water + terrain)
- Interacts with: RFC-006 (channel banks), RFC-008 (tessellation snap);
  promotes item 4 of `ideas/geometry-from-fields.md`

## Problem

The shoreline is where the lattice shows most.  Water renders on grids
whose fragments discard where dry, so the wet/dry boundary is resolved
per fragment against bilinear sheets: the visible edge is a sawtooth at
lattice scale, softened by swash and foam shading but never actually
straightened.  Terrain has no crease at the waterline at all.  Every
shore -- sea, lake, and river bank -- advertises the grid.

## Current situation

- The coarse ocean grid and the Metal3 near-field mesh pipeline both
  discard dry fragments; the mesh path probes 15x15-cell tiles for
  wetness exactly (water-minus-ground is bilinear per cell, so corner
  probes cannot miss -- `docs/renderer-design.md`), but emitted lattices
  still end at cell boundaries.
- The water surface sheet is `ground` in dry cells and stamped levels in
  wet ones (`paint_watercourses`), so `w - z` is a well-defined bilinear
  function per cell whose zero set *is* the shoreline.
- Standing water is static per world (Lab edits aside), so the curve
  need not be recomputed per frame.

## Proposal

Extract the waterline as geometry, once per world, and make both water
and terrain conform to it.

1. **Extraction.**  Marching squares on `w - z` over the terrain lattice.
   Within a cell the zero set of a bilinear function is a hyperbola
   branch; one or two straight segments per boundary cell approximate it
   to well under a centimetre at 2.44 m cells.  Deterministic
   (cell-index-ordered), cheap, done at world handoff next to the sheet
   painting.
2. **Water mesh conformance.**  Boundary cells in the near-field water
   pipeline emit clipped polygons whose outer vertices sit *on* the
   extracted segments instead of full lattice quads plus discard.  The
   coarse horizon grid keeps fragment discard -- beyond a few hundred
   metres the sawtooth subtends less than a pixel.
3. **Terrain crease.**  Where RFC-008 tessellates the near field, snap
   the nearest generated vertices to the same curve so the bank meets
   the water in an actual crease; the wet-darkening band in the terrain
   shader keys off true distance-to-waterline instead of
   `|ground - level|`.
4. **The shoreline ribbon.**  The extracted polyline is also the ideal
   carrier for the fine foam/wet-line band -- a strip extruded along the
   curve, replacing the current purely procedural shore band where the
   camera is close.

## Consequences

- Kills the single most lattice-revealing artifact class in one move,
  for every water body at once: no more sawtooth edges, no hovering
  water slivers at steep banks, no z-fighting where sheet meets ground.
- Perceived quality tier changes disproportionately: shorelines are
  high-contrast, high-attention boundaries, and they are everywhere.
- The curve is a reusable *reading* (RFC-013 vocabulary): gameplay
  (spawn sites, HUD hints), audio (shore lapping), and the Lab's lake
  census presentation can all consume it.

## Risks and alternatives

- Lab interactivity: terrain edits invalidate the curve.  Re-extract at
  preview cadence (it is O(boundary cells)) or accept fragment-discard
  edges inside the Lab, where inspection, not polish, is the point.
- Waves move the instantaneous waterline; the extracted curve is the
  *still* line.  Wave amplitude already fades at shore precisely so the
  edge stays put (`ocean.metal` shore band), so the still line is the
  right anchor; the swash animation continues to live in shading.
- Alternative considered: screen-space edge refinement.  Rejected --
  view-dependent, unstable in motion, and blind to the terrain crease.

## Implementation sketch

1. `terrain::extract_waterline(flood, census, sheets)` returning ordered
   polylines tagged by body id; unit tests on synthetic bilinear cases.
2. Boundary-cell clipping in the mesh-stage water path (the object stage
   already classifies tiles; add a "mixed" tile class).
3. Wet-band distance input + RFC-008 vertex snapping.
4. Shore ribbon strip with the existing advected foam material;
   `tools/capture-water` comparisons at all five feature cameras.
