# RFC-010: Break the terrain triangulation's diagonal bias

- Status: Draft
- Area: rendering (terrain geometry)
- Interacts with: RFC-005 (shading softens what this fixes structurally)

## Problem

Every terrain cell splits along the same diagonal, and the landscape
shows it.  Ridges and channels that run northeast-southwest are rendered
by triangle edges that follow them; the same features rotated ninety
degrees are rendered *against* the grain and acquire a sawtooth
silhouette and faceted shading.  The artifact is subtle per cell and
pervasive in aggregate: a directional texture laid over the whole world
at Native LOD and coarser, and a systematic asymmetry between
otherwise-equivalent terrain features.

## Current situation

- The index templates use one strip topology whose diagonal runs from
  the bottom-left to the top-right of every cell, documented explicitly
  in `terrain.metal`: "The strip topology uses the bottom-left to
  top-right diagonal" (`terrain_height_on_lattice`,
  `moppe/shaders/metal/terrain.metal:177`).
- Both `terrain_height_on_lattice` and `terrain_normal_on_lattice`
  evaluate the *exact* triangle surface of a coarser level so finer
  levels can morph onto it -- any triangulation change must update the
  shader functions and the index templates in lockstep, or morphing
  breaks.
- Physics is untouched by this RFC: `interpolated_height` is bilinear
  (`moppe/map/generate.hh:137`), not triangulated.
- Five shared index templates cover all chunks
  (`docs/renderer-design.md`); per-chunk index buffers would forfeit
  that sharing.

## Proposal

Alternate the diagonal in a union-jack pattern: cell (i, j) splits
bottom-left/top-right when `(i + j)` is even and top-left/bottom-right
when odd, where i, j are cell coordinates *at that LOD's stride* so the
pattern is self-similar across levels.  This keeps the five shared
index templates (the pattern is position-dependent but chunk-uniform:
chunk origins are multiples of 128, which is even at every stride, so
one template still serves every chunk), removes the global directional
grain, and halves the worst-case fit error against the bilinear surface
since every feature direction now meets aligned diagonals half the time.

Required lockstep changes:

1. Index template generation in the Metal backend's terrain setup.
2. `terrain_height_on_lattice` / `terrain_normal_on_lattice`: a parity
   test selects which triangle pair to evaluate (`f.x + f.y <= 1` vs
   `f.x >= f.y` orientation).
3. The topology overlay (`MOPPE_TERRAIN_TOPOLOGY=1`) so inspection shows
   the real triangles.

## Alternatives considered

- **Data-driven diagonals** (choose per cell the split that best fits
  the four corners -- along the bilinear fold): strictly better fit,
  but requires a per-cell bit and either per-chunk index buffers or
  shader-side triangle selection, breaking template sharing for a
  second-order gain.  Revisit only if union-jack captures still show
  grain on specific features.
- **Delatin-style adaptive triangulation**: an entirely different
  terrain mesh pipeline; out of proportion to the problem given
  vertex-pulling works well.
- **Do nothing and rely on RFC-005**: fragment-rate normals fix the
  *shading* grain but not silhouettes or the near-field surface shape.

## Consequences

- Directional bias disappears from silhouettes and LOD morph targets;
  NE-SW and NW-SE features become statistically equivalent.
- Zero new memory, zero per-frame cost; a one-time index/shader change.
- The Subdivided near field is unaffected (it evaluates the smooth
  Catmull-Rom surface, then morphs onto Native -- whose surface simply
  becomes the union-jack one).

## Implementation sketch

1. Change template generation + the two lattice functions together;
   assert agreement with a CPU reference in `moppe-testbed` (sample
   both surfaces at random points, require exact match).
2. Visual check with the topology overlay at each LOD; morph-band
   inspection while driving toward a chunk boundary.
3. Fixed-seed capture A/B (`tools/capture-game`), looking specifically
   at ridgelines in both diagonal orientations.
