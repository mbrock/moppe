# RFC-009: Relief readings -- cavity ambient occlusion and horizon shadows

- Status: Draft
- Area: rendering (terrain lighting), with a possible shadow-pass
  retirement
- Interacts with: RFC-005 (both are lighting-detail moves), Lab preview
  shadow path

## Problem

Two depth cues are missing that heightfields can compute almost for
free.  First, ambient light is a constant hemisphere fill: valleys,
hollows, and gullies receive the same ambient as exposed divides, so
relief flattens wherever the sun doesn't reach.  Second, terrain
self-shadowing depends entirely on the 4096^2 shadow map, which is
weakest exactly where terrain shadows matter most -- long, grazing-angle
shadows at low sun -- and costs a full depth render plus 5-tap PCF.

## Current situation

- `FrameParams.ambient` drives a uniform cool-sky/warm-ground hemisphere
  in the shaders (`docs/renderer-design.md`); no positional variation.
- The shadow map is terrain-only, rendered once per world
  (`render_terrain_shadow`, `moppe/game/terrain.cc:128`), Depth16 with
  hardware PCF and slope-scaled bias; the Lab maintains a 1024^2 preview
  variant with crossfade.
- Critically: **the sun's azimuth is a world constant**
  (`SUN_AZIMUTH = 0.8f`, `moppe/game/game.cc:58`); only its elevation
  varies (`sun_height` setting / `MOPPE_SUNHEIGHT`).  Sky, light, shadow,
  and ocean glint all share this one direction by design.

## Proposal

Two precomputed rasters, both honest *readings* of the heightfield in
the sense of `docs/terrain-expressions.md`:

### 1. Cavity / openness map

Multi-scale curvature (Laplacian at 2-3 radii, or topographic openness):
negative values in hollows and gullies, positive on crests and divides.
Bake once per world into an R8 raster (~4 MB at 2049^2); sample it in
the terrain (and optionally uber) fragment shaders to modulate the
**ambient term only** -- hollows darken, divides brighten, direct sun
untouched so cast shadows keep their shape.  This is the single
cheapest way to make relief legible in flat light, and it composes with
RFC-003's rasters (gullies are both eroded *and* concave).

### 2. Horizon-angle map for the fixed sun azimuth

Because azimuth never changes, terrain self-shadowing for *any time of
day* is one scalar per cell: the elevation angle of the horizon toward
azimuth 0.8.  Precompute it with a single directional max-scan sweep
(O(n) per row along the sun direction), store R16 (~8 MB), and shade

    lit = smoothstep(horizon - eps, horizon + eps, sun_elevation)

in the fragment stage.  This gives soft-edged, bias-free terrain shadows
that are *exact* for the heightfield at every sun height -- including
the grazing angles where the shadow map aliases -- with zero per-frame
render cost.

Consequences worth naming: the gameplay 4096^2 shadow pass becomes
redundant for terrain-on-terrain shadowing, and since it is terrain-only
today, it could be retired entirely (objects already use blob shadows;
the uber shader would sample the horizon map for objects standing on
terrain, which is one texture read).  The Lab's preview-shadow crossfade
machinery maps directly onto horizon-map rebuilds at preview quality.

## Risks and alternatives

- Sun azimuth must remain a constant; if it ever animates, K azimuth
  bins (8-16 at half resolution) restore generality at modest cost.
  State the constraint in the code where `SUN_AZIMUTH` lives.
- Horizon shadows are heightfield-exact but object-blind: a mountain
  shadows the bike (via the uber sample) but the bike shadows nothing
  new -- unchanged from today, where the map is terrain-only anyway.
- Penumbra width `eps` is art-directable but constant; a
  distance-to-occluder refinement exists (store horizon distance too)
  if softer long shadows are wanted later.
- Cavity AO can double-darken with shadowed areas; keep it in the
  ambient term only and its magnitude modest (0.2-0.3).

## Implementation sketch

1. Cavity raster at world handoff (few-line stencil pass, threads or
   GPU); fragment-side ambient modulation; Lab overlay to inspect it.
2. Horizon scan (rotate-scan or DDA per row along azimuth), R16 upload;
   shader term behind a graphics feature flag so the benchmark cube can
   price it against the shadow map.
3. If parity holds at all sun heights in captures: drop the gameplay
   shadow pass, keep the Lab preview path on horizon rebuilds, and
   reclaim the pass from the frame (`MOPPE_PROFILE_GPU` before/after).
