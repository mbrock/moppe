# RFC-013: A discretization is a lattice plus a reconstruction

- Status: Draft
- Area: semantic foundations (raster <-> continuous boundary)
- Interacts with: RFC-006, RFC-011, RFC-012; explains rather than
  changes RFC-005/007/008

## Problem

The codebase holds one raster and at least four private opinions about
what continuous or combinatorial object it stands for:

1. **Physics heights** reconstruct bilinearly
   (`HeightMap::interpolated_height`, `moppe/map/generate.hh:137`) --
   but **physics normals** bilinearly blend the *precomputed vertex
   normal map* (`interpolated_normal`), which is not the normal of the
   bilinear surface.  Height and normal come from two different
   surfaces.
2. **River and trail geometry** query the bilinear height surface while
   constructing continuous alignments and cross sections.
3. **The renderer's near field** reconstructs with bounded Catmull-Rom,
   clamped to each source cell's corner range so it cannot contradict
   view 1 by more than a cell's spread (`terrain.metal:28`); coarser
   LODs reconstruct as triangulated lattices with a specific diagonal
   rule (RFC-010).
4. **Drainage, flood, and the census** reconstruct nothing: they read
   the samples as a combinatorial D8 structure in which only ordering
   and adjacency survive.

Each choice is locally defensible.  None is *named*.  Their agreements
are enforced by scattered conventions (the Catmull-Rom bound, the seam
synchronization) and their disagreements (physics normal vs bilinear
gradient) are discoverable only by archaeology. The torus also stores a
duplicated seam that every mutation must manually re-synchronize
(`synchronize_periodic_edges`).

## Proposal

### 1. Reconstruction as a first-class value

    Reconstruction = Bilinear
                   | BoundedCatmullRom
                   | TriangulatedLattice { diagonal_rule }
                   | D8   -- combinatorial: no surface, only receivers

and a sampled view that pairs storage with its interpretation:

    Sampled<R> = { raster, TerrainGrid, Reconstruction }

Consumers request `height_at`, `gradient_at`, `normal_at` *through the
view*; which surface answers is part of the type, not the call site.
The known cross-view invariants become stated properties with tests:
the Catmull-Rom bound ("never outside the cell's corner range") and the
physics/geometry height agreement (both `Bilinear` -- after which the
physics-normal inconsistency in problem 1 is a *documented decision or
a fixed bug*, not an accident).

### 2. The gather/scatter adjunction, stated once

Any future particle/raster exchange should pair sampling (gather with
bilinear weights) and deposition (scatter with the same weights). Scatter is
the transpose of gather, and conservation holds because the weights sum to
one. Define both operators next to the `Reconstruction` they belong to so
spray or sediment plumes do not re-derive the pair privately.

### 3. Storage conventions inside the view

Duplicated-seam vs unique-torus storage becomes a property of the view,
with mutation APIs that maintain the seam invariant automatically --
`synchronize_periodic_edges` stops being a call sites must remember and
becomes a postcondition call sites cannot forget.

## Consequences

- Agreements and disagreements between the world's views become
  propositions: testable, greppable, teachable.
- New consumers (RFC-006's composite view, RFC-008's displacement
  envelope) have a vocabulary to declare what surface they extend and
  by how much.
- The seam-invariant bug class is closed structurally.

## Risks and alternatives

- This is refactor-philosophy, adopted incrementally: begin by
  *documenting* the four views in one place with tests, introduce
  `Sampled<R>` for new code (RFC-006 is the natural first client), and
  migrate old call sites opportunistically.  A big-bang port is neither
  needed nor wise.
- Cost of abstraction in hot paths: the view dispatch must compile away
  (templates or static polymorphism), since `interpolated_height` is an
  inner loop.

## Implementation sketch

1. A `docs/` note (or promotion of this RFC) cataloguing the existing
   reconstructions with their invariants + a test asserting the
   Catmull-Rom bound and measuring the physics-normal divergence.
2. `Reconstruction` + `Sampled<R>` types in `moppe/terrain/`; port
   `TerrainView` first.
3. Seam-maintaining mutation API; delete bare
   `synchronize_periodic_edges` calls.
