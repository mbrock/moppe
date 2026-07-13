# RFC-005: Fragment-rate normals for distant terrain

- Status: Draft
- Area: rendering (terrain)
- Interacts with: RFC-010 (both touch LOD surfaces), independent otherwise

## Problem

Distant mountains are lit at the resolution of their *geometry* rather
than their *data*.  The terrain vertex shader samples the RG16Snorm
normal texture at vertex rate, so a stride-8 chunk lights from one normal
per 8x8 source cells: far slopes go flat, ridge shading washes out, and
the LOD structure becomes visible as a lighting artifact even though the
full-resolution normal field is sitting in memory.

## Current situation

- `moppe/shaders/metal/terrain.metal`: `terrain_vertex` reads normals per
  vertex (`terrain_read_normal`, `terrain_normal_on_lattice` for coarse
  LODs) and passes an interpolated `normal` varying to the fragment
  stage.  The fragment already receives `grid_coord` -- the authoritative
  source-lattice coordinate -- as a varying, so the address needed for a
  fragment-rate fetch is already there.
- The normal texture is created for integer `read()` access.  Unlike the
  R32F height texture (not filterable before Apple9,
  `docs/renderer-design.md`), RG16Snorm *is* linearly filterable on all
  Apple-family GPUs, so a hardware-sampled fetch is legal.
- The subdivided near field derives its normals analytically from the
  bounded Catmull-Rom surface -- that path is correct and should not
  change.

## Proposal

For Native and coarser LODs, move normal acquisition to the fragment
stage: sample the full-resolution normal texture (linear sampler, y
reconstructed as today) at `grid_coord`, and light with that.  Geometry
LOD and lighting LOD decouple: a stride-8 silhouette carries full 2049^2
shading detail, exactly as a normal-mapped mesh does.

Details:

- Create the normal texture with sample usage and a linear repeat/clamp
  sampler matching the height addressing (`terrain_sample_position`
  periodic wrap becomes sampler-address mode plus explicit wrap of
  `grid_coord` for the torus seam).
- Near field: keep vertex/analytic normals (they morph back onto the
  authoritative lattice already); optionally blend to the fragment sample
  across the Subdivided->Native morph band using the existing morph
  factor so there is no visible hand-off.
- The shadow, haze, and splat math are unchanged -- they already consume
  a world-space normal; only its provenance moves.
- Terrain Lab preview (`derive_normals` path) keeps its height-derived
  normals in the vertex stage; the fragment path applies only to the
  gameplay normal texture.

## Consequences

- Probably the highest look-per-effort change available in the renderer:
  one texture sample moves from VS to FS, and distant terrain gains full
  lighting detail at every LOD.
- Fewer visible LOD transitions (shading no longer snaps with stride),
  which also softens the impact of RFC-010's triangulation changes.
- Slight fragment-cost increase on terrain pixels (one RG16 sample plus
  a sqrt for y).  Terrain fragments already take 4 splat samples and a
  5-tap shadow kernel; the benchmark harness
  (`--graphics-benchmark`, docs/game-state.md) can quantify the delta
  precisely.

## Risks and alternatives

- Bandwidth on iPhone-class GPUs: measure; if needed, sample a
  half-resolution mip for the farthest LODs -- still far better than
  stride-8 vertex normals.
- Alternative: prebuilt normal mips with silhouette-preserving filtering.
  More machinery, weaker result; hardware trilinear on the full-res
  texture already provides the sensible prefilter.
- Specular aliasing at distance (full-detail normals under a moving
  camera): mitigate with the standard roughness-from-normal-variance trick
  (Toksvig) using the mip chain, if it shows.

## Implementation sketch

1. Texture usage + sampler change in `set_terrain`
   (`moppe/render/metal/metal_renderer.mm:1125`).
2. `terrain.metal`: fragment-side sample behind a function constant;
   vertex path retained for near field and Lab preview.
3. A/B captures via `tools/capture-game` at fixed seed; benchmark run for
   the cost delta; check the torus seam and Cover View tiling.
