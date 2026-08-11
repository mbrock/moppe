# Handoff: the grass-radius problem (unsolved)

Written 2026-08-11 for a fresh reviewer. The player's complaint, in
their words, repeated across an entire day of work: **"grass magically
appears at a fixed radius around the camera, like two meters in front
of me."** Their in-game screenshots confirm it. Nothing done today has
fixed their experience, despite many changes that improved carefully
composed captures. This file records everything: architecture, changes,
instruments, failed reasoning, and the current best hypothesis.

## 1. The phenomenon (player-observed, twice screenshotted)

Riding the chase camera through open meadow:

- Dense 3D grass blades exist only in a small belt around the camera.
  In the player's latest screenshot the bike itself (≈5 m ahead of
  camera) already stands on smooth bladeless ground while blades
  surround the near camera — belt radius perhaps 10–30 m.
- Beyond the belt: smooth green painted-looking terrain.
- Riding forward, the belt travels with the camera: grass continuously
  materialises at its leading edge. This is the core complaint.

## 2. Architecture (files)

- `moppe/shaders/metal/undergrowth.metal` — procedural ground flora.
  Object stage walks 0.6 m tiles in a camera window (reach 64 m ×1.7
  set in `metal_renderer.mm` `draw_undergrowth`); mesh stage grows up
  to 32 shoots/tile from habitat fields. Families: grass blades, fern
  rosettes, flower stems+heads. No retained state; world-hash identity.
- `moppe/shaders/metal/grass_medium.h` — shared "medium": habitat →
  leaf_area/cover/tints; per-family resolved fractions (see §5);
  `moppe_sward_ensemble_light` (blade shader's ensemble limit, used by
  terrain for grassy ground colour); flower drift fields.
- `moppe/shaders/metal/terrain.metal` — terrain vertex now also raises
  a "sward shell" (grassy ground displaced up by sward height where
  blades have retired); fragment lights grassy ground with the shared
  ensemble formula + grain cascade + grazing/backlit trade.
- Uniform lane: `u.lod.x` = undergrowth window reach (cost bound).
  `u.temporal.y` = scene input pixel height (both terrain+undergrowth).

## 3. Everything changed today (chronological, all on master)

- b6c46d9 family table (ferns as rosettes, flowers in drifts)
- ffb6ebc flowery meadow tuning
- 5aa549c per-family LOD ladder; window→cost bound (reach ×1.7);
  head footprint floor; stem floor; head roll
- 5974011 mid-field coverage conservation (sqrt count curve, tuft
  widening ×4 cap, exposure settling toward field mean)
- f2125f7 MOPPE_CAPTURE_AUX (motion+depth dumps); temporal instrument
- 55db01c MOPPE_GLIDE (bare moving camera); isolation lessons
- 512548b MOPPE_LOOK (render one frame from explicit camera pose)
- 4904bfe material-continuity strip tool
- 50857f0 terrain grassy ground lit as blade ensemble limit
  (`moppe_sward_ensemble_light`), soil texture demoted to luma detail
- 3194fa9 grazing-incidence darkening; grain cascade to all distances
- e93cf51 sward shell (terrain vertex displacement = sward height ×
  complement of blade resolved fraction) + backlit trade (grazing
  occlusion yields to transmission toward sun)

UNCOMMITTED in worktree: `moppe_sward_standing` helper added to
grass_medium.h (world-distance floor, see §6) — **not yet wired into
undergrowth budgets or the shell**.

## 4. Instruments built (all in repo)

- `tools/frame-texture-profile OUT LABEL=GAZDIR...` — texture energy vs
  ground distance from grass-gradient frames; `--ride` mode warps
  consecutive frames through dumped motion vectors (residual = change
  motion can't explain, binned by true depth); `--strip D1,D2,..` cuts
  same-material patches at chosen depths from one frame.
- `MOPPE_CAPTURE_AUX=1` — every screenshot also dumps motion.rg16f,
  depth.r32f, matrix sidecar.
- `MOPPE_LOOK="ex ey ez sx sy sz [fov]"` (gazetteer mode) — one settled
  frame from an explicit camera. THE observation primitive.
- `MOPPE_GLIDE=<shot> MOPPE_GLIDE_FRAMES=N MOPPE_GLIDE_SPEED=v` —
  consecutive frames from a translating camera, no HUD/vehicle/wind.
- Canonical benchmark scene (docs/ideal-dream-graphics.org GFX-042b):
  `MOPPE_GRASS_LAB=1 MOPPE_UPLIFT_YEARS=0 MOPPE_TERRAIN_PROFILE=smoke
  MOPPE_LOOK="1718.75 54.7 39.06 1790 50.5 108 68"
  tools/capture-terrain-gazetteer OUT`

## 5. The key mechanism and the key numbers

Blade geometry is priced by projected blade width:
`blade_px = 0.018 m × focal_px / distance`,
`focal_px = |proj[1][1]| × 0.5 × scene_pixel_height`.
Budget ∝ `sqrt(smoothstep(0.16, 0.95, blade_px))`; full density needs
blade_px ≥ 0.95; zero at ≤ 0.16. The sward shell (e93cf51) rises as
`1 − sqrt(resolved)`.

Consequence table (riding FOV ≈ 68°, proj[1][1]≈1.48):

| scene height | full-density ends | geometry ends |
|---|---|---|
| 1537 px (my fullscreen captures) | ≈21 m | ≈128 m |
| 900 px | ≈13 m | ≈75 m |
| ≈600 px (plausible windowed play) | ≈8 m | ≈50 m |

**Every distance in the system scales with scene resolution.** All of
today's "verified" improvements were captured at 1537 px scene height.
The player plays windowed at roughly half that or less. Their belt is
therefore roughly half/third of everything I looked at, and the shell
begins rising almost at the bike. This was never reproduced in any
capture — the single biggest evaluation failure of the day.

## 6. Current best hypothesis + proposed (half-applied) fix

Hypothesis: the complaint is exactly the resolution-scaled belt. At the
player's scene resolution, full density ends ~8 m from camera (the
"two meters in front of me"), geometry ends ~40–50 m, and the new shell
raises smooth ground immediately behind the bike.

Proposed fix (helper exists, unwired): guarantee standing geometry in
WORLD distance; let perceptual math only extend it:

```
moppe_sward_standing(blade_px, d) =
  max( sqrt(moppe_grass_resolved_fraction(blade_px)),
       1 − smoothstep(40, 80, d) )
```

Wire it in three places so belt and shell stay exact complements:
1. undergrowth object stage tile budget (currently
   `sqrt(moppe_grass_resolved_fraction(...))` around line ~250),
2. undergrowth mesh stage grass `resolved_feature` (same expression),
3. terrain `terrain_sward_shell` (`standing = ...` same expression).
Flowers likely need an analogous floor (their heads currently die by
head_px, also resolution-scaled).

Risks: more shoots at low resolutions (cost: undergrowth was ~3.7 ms
median in a quick windowed benchmark at capture res; low-res guarantees
add more); subpixel blades at low res → temporal shimmer (mitigated by
existing tuft widening ×4 + exposure settling, but verify with the
glide instrument AT LOW RESOLUTION, e.g. MOPPE_RENDERSCALE=0.35).

Alternative/complementary levers a reviewer should weigh:
- widen blades more aggressively at low res (raise the ×4 widen cap so
  coverage holds where count can't),
- scene-resolution-aware belt: guarantee in *fraction of screen depth*
  rather than fixed metres,
- make near-full-density plateau explicit (e.g. full to 25 m always),
- verify the player's actual scene resolution first (startup log line
  "render targets: ... scene=WxH" — ask them to paste it).

## 7. Evaluation traps that burned this session (all real, all mine)

1. Elevated/aerial framings hide near-field failures; saddle height
   (1.5 m) buries the lens; chase height (~2.5–3 m) is the honest one.
2. Cross-lit and truncated-sightline framings hide the far-field.
   Backlit + long flat sightline (canonical scene) is the honest one.
3. Texture statistics measure contrast, not quality (the pop-in band
   outscored its fix). Motion residuals cancel constant state errors.
   The naive wide look precedes instruments (docs/working-practices.md).
4. Demo-ride captures are contaminated (HUD popups, dust, bike, trail,
   glare) and ride-judge keeps every 2nd frame (motion vectors then
   under-compensate by half).
5. **Never evaluated at the player's resolution** — the trap that
   survived everything above. Reproduce with MOPPE_RENDERSCALE≈0.35 or
   a matching --window-size before believing any capture.

## 8. Also open (recorded, unrelated to the radius)

- GFX-044a: lattice-aligned pale dabs on real-world hillsides; magenta
  tests exclude crown proxies and broadleaf foliage; next suspect wood
  organs (bark tint matches). docs/ideal-dream-graphics.org.
- GFX-044: forest tint band on terrain is a separately-authored colour
  (same class of bug the sward had).
- Grain cascade octaves show as "mowing stripe" banding at oblique
  angles (value noise anisotropic under foreshortening).
- Flower heads read as paper confetti near; drift heads end at a soft
  line (head window), wash continues.
- docs/ensemble-continuity.md holds the theory (LOD as moment-matching
  between effective theories); it is good doctrine but note that all
  of its "verified" claims were verified at capture resolution only.

## 9. Fastest path to seeing the player's problem yourself

```
cmake -B build -G Ninja && cmake --build build
# their conditions, approximately:
MOPPE_RENDERSCALE=0.35 MOPPE_GRASS_LAB=1 MOPPE_UPLIFT_YEARS=0 \
MOPPE_TERRAIN_PROFILE=smoke \
MOPPE_LOOK="1718.75 54.7 39.06 1790 50.5 108 68" \
  ./tools/capture-terrain-gazetteer /tmp/lowres-canonical
# compare against the same command without MOPPE_RENDERSCALE.
```
If the low-res frame shows the belt collapsing toward the camera and
the shell rising close behind it, the hypothesis in §6 is confirmed and
the world-distance floor (or a better idea) is the fix.
