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
2. **The droplet simulation** reconstructs bilinearly *with the exact
   analytic gradient of the bilinear patch*
   (`moppe/map/generate.cc:386`) -- a third, internally consistent view.
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
synchronization) and their disagreements (physics normal vs droplet
gradient) are discoverable only by archaeology.  Two adjacent facts
share the problem: the torus is stored with a duplicated seam that every
mutation must manually re-synchronize (`synchronize_periodic_edges`),
and the droplet model's constants live in undeclared lattice units.

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
physics/droplet gradient agreement (both `Bilinear` -- after which the
physics-normal inconsistency in problem 1 is a *documented decision or
a fixed bug*, not an accident).

### 2. The gather/scatter adjunction, stated once

The droplet code independently implements sampling (gather with
bilinear weights) and deposition (scatter with the same weights).  That
pairing -- scatter is the transpose of gather, conservation holds
because the weights sum to one -- is the definition of a consistent
particle/raster exchange.  Define both operators next to the
`Reconstruction` they belong to; the droplet kernel, RFC-003's ledger,
and any future particle system (spray, sediment plumes) use the pair
instead of re-deriving it.

### 3. Storage conventions inside the view

Duplicated-seam vs unique-torus storage becomes a property of the view,
with mutation APIs that maintain the seam invariant automatically --
`synchronize_periodic_edges` stops being a call sites must remember and
becomes a postcondition call sites cannot forget.

### 4. Name the lattice-unit regime

The droplet model's dynamics run in *lattice units*: horizontal steps
of one cell, heights in normalized relief, and constants
(`gravity = 4`, `capacity_k = 4`, ...) that silently bake in the aspect
ratio between cell spacing and height scale.  Consequence: the model is
not grid-convergent -- the same physical world at 1025^2 vs 2049^2
erodes differently, which is exactly why generation profiles pair
resolution with droplet count as tuned bundles
(`docs/terrain-expressions.md`).  This RFC does not metricate the
droplet model (its constants are art direction as much as physics); it
makes the regime *declared*: define `lattice_length` (= spacing) and
`relief_unit` (= height scale) as scaled units attached to the
discretization, type the droplet constants in them, and state the
conversion law.  "Erosion depends on resolution" then graduates from
folklore to a property with a formula attached -- and RFC-001's
grid-convergent stream power becomes the documented escape hatch.

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
  (templates or static polymorphism), since `interpolated_height` and
  the droplet sampler are inner loops.

## Implementation sketch

1. A `docs/` note (or promotion of this RFC) cataloguing the existing
   reconstructions with their invariants + a test asserting the
   Catmull-Rom bound and measuring the physics-normal divergence.
2. `Reconstruction` + `Sampled<R>` types in `moppe/terrain/`; port the
   droplet sampler and `TerrainView` first.
3. Seam-maintaining mutation API; delete bare
   `synchronize_periodic_edges` calls.
4. Lattice-unit declarations beside the droplet constants; profile
   documentation updated to state the bundle rationale.
