# RFC-006: Continuous river alignments, not raster carves

- Status: Accepted; rendering phase implemented 2026-07-16
- Area: terrain representation (simulation + rendering + physics)
- Interacts with: RFC-007 (waterline), RFC-008 (tessellation), RFC-013
  (reconstruction discipline); flagship application of the "field, not
  raster" thesis

## Problem

Rivers are where the terrain lattice is most visible and most harmful.
Raster carving quantized every cross-section to cells, while the watercourse
painter had to reverse-engineer what the carve meant. Banks became
staircases, widths jumped in cell increments, and downstream systems could
not reason about a reach more precisely than its D8 cells even though the
network already knew its order, area, slope, and junctions.

## Current situation

- Orogeny creates the terrain and valleys; channel carving and the droplet
  simulator have been removed from the program language and build.
- Every `RiverReach` owns a dense, unwrapped `RiverAlignment`, derived with
  the same damped cubic-Hermite discipline used by trail alignments.
- Alignment samples carry contributing area, slope, waterfall state,
  standing-water blend, water level, local distance, and a flow coordinate
  continuous through confluences.
- `game::RiverSurface` builds a seven-row ribbon directly from that value.
  `paint_watercourses` paints only standing water and mouth currents.
- Physics continues to sample the unmodified orogeny terrain. The river is
  presently a water-surface feature, not a terrain collision feature.

## Proposal

Represent each river as a **vector feature evaluated at sample time**:
per reach, one shared continuous alignment, physical width/depth laws, and
an explicit cross-section. Running-water rendering samples this value at its
own density instead of recovering a curve from a raster.

If rideable channels later require a shaped bed, add an optional analytic
terrain composite around the same alignment:

    z(x) = z_base(x) - depth(s) * profile(d(x) / halfwidth(s))

where `d` is distance to the alignment, `s` its arc position, and `profile`
is a bounded bed shape. This composite is deliberately deferred: the current
orogeny terrain already supplies valleys, and adding a second collision-height
truth is only justified when gameplay needs it.

Evaluation strategies:

- **CPU (future traversal/physics)**: a spatial hash of alignment segments;
  a query visits only nearby reaches and returns arc, cross-river offset,
  width, depth, and flow direction. The value is suitable for a raft route
  without changing today's motorcycle collision surface.
- **GPU (future terrain shaping)**: bake a compact
  channel-parameter raster at 2-4x lattice resolution -- signed distance
  to nearest centerline, arc-interpolated halfwidth and depth (3-4
  channels of RG16F/RGBA16F) -- refreshed only when the network changes.
  The *shape* stays analytic; only the addressing is precomputed.  True
  per-vertex segment lists are a later option.

## Consequences

- Smooth curves and sub-cell widths are independent of terrain resolution.
- Multi-row cross-sections provide stable depth, bank feathering, and normal
  orientation rather than one flat quad across the whole river.
- Flow phase is continuous across confluences, and the ribbon fades beneath
  the standing-water surface at mouths.
- One alignment can later support rendering, flow sampling, camera placement,
  audio, navigation, or raft traversal without reinterpreting D8 cells.
- The painter's bank probing, dry-reach stamps, raster carve, and droplet
  machinery are deleted.

## Risks and alternatives

- Cubic smoothing must not cut visibly outside the routed valley. Tangents are
  damped and the alignment stays a reading; regression captures cover rivers,
  confluences, mouths, waterfalls, and lakes.
- Separate reaches overlap geometrically at junctions. Their global arc
  coordinate removes animation seams, while soft cross-section edges hide
  harmless coverage overlap. A future junction patch is possible if close
  cameras expose a silhouette crease.
- Derived surface height is bank-limited and monotonically clamped downstream,
  but it cannot manufacture a physically correct pool/cascade profile from an
  under-resolved valley. A hydraulic surface solve remains future work.

## Implementation sketch

1. **Done:** extract shared dense alignments and globally continuous flow
   distance from the `RiverNetwork`.
2. **Done:** render seven-row cross-sections with physical width/depth laws,
   bank-limited levels, mouth fading, and curve-oriented two-phase advection.
3. **Done:** simplify the painter to standing water plus mouth currents and
   retire raster carving/droplets.
4. **Later, gameplay-driven:** add segment lookup and a river-following frame
   for traversal. Only then decide whether a collision-bed composite is
   necessary.
