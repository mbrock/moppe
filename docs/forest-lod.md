# Forest LOD: the continuous-assembly design

Status: implementation record of the conifer LOD system in
`moppe/shaders/metal/forest.metal` as of August 2026, and of the principles
and instruments that shaped it. The forward-looking plan is in
[forest density and aggregates](forest-density-and-aggregates.md).

## The bet

No tree mesh exists in CPU or GPU memory. Every frame, the object stage
chooses each organism's detail from its projected size and the mesh stage
grows boughs, tufts, and blades from a seed (Kuth et al. 2025, "Real-Time
GPU Tree Generation", HPG; Sheaf `#EDURTK`, notes `#6S29CJ`). The memory
saving is the headline but not the point: because nothing is retained,
detail can be exactly right for the current frame and vary continuously --
the whole temporal-stability design below is only expressible because no
baked LOD meshes exist. The costs are structural: generation is paid every
frame even when the camera is still, stability of every generation input
becomes a discipline instead of a given, and ray tracing would need per-frame
acceleration-structure rebuilds.

## Principles, each learned the hard way

1. **Transitions finish while the tree is small in the frame.** The bough
   ramp saturates by ninety projected scene pixels -- about a tenth of the
   frame's height at the game lens. Perception sets this point, not
   geometry: the rider's eye is exquisitely sensitive to elements appearing
   on a tree large enough to watch. When the ramp saturated at three
   hundred scene pixels, crowns were still assembling while filling a third
   of the screen, and the forest read as morphing. Beware the unit trap
   that hid this: LOD thresholds are computed in *scene* pixels, which at
   the default half render scale understate perceived size by 2x.

2. **Completion distance must be size-invariant.** A pixel ramp alone makes
   completion distance proportional to tree height, so saplings finish
   growing only when the walker stands beside them. A short tree's measure
   is boosted (`forest_bough_measure`, factor `clamp(22.5/h, 1, 4.5)`) so
   every tree completes near the same distance.

3. **Conservation of foliage.** A crown must never thin with distance;
   distance changes representation, never mass. Removed boughs move their
   area into survivors (`sqrt(63/count)` widening, at full strength at
   every count -- it converges to one at the full complement, so it needs
   no fade for near stability). The bundled tier's three-tuft boughs widen
   by `sqrt(13/3)` to stand in for thirteen-tuft ones. The floor of
   twenty-one boughs keeps the sparsest assembly reading as a small solid
   tree rather than a pole with stubs, and the floor complement stands at
   full growth (a permanently half-grown bough leaks mass).

4. **Fill evenly; the silhouette is sacred.** The first nine ranks sketch
   the whole silhouette, one bough per whorl bottom-to-top. Later ranks add
   one spoke per whorl per round, top whorl first, with consecutive
   arrivals in a whorl landing on opposite azimuths. Bottom-up fill read as
   the tree growing taller against the sky; consecutive spokes read as a
   lopsided branch; region-by-region completion reads as construction.

5. **Growth is imperceptible per frame.** Boughs grow in from nothing over
   a count window scaled by their size (large low boughs over many ranks,
   small high ones over few), and a per-individual threshold
   (`forest_lod_threshold`) staggers every boundary so a stand never
   crosses one on the same frame.

## Mechanisms

- Projected size comes from the centre's clamped view depth, not endpoint
  projection (endpoints collapse discontinuously when root or tip crosses
  the camera plane). The frustum bound must contain the whole organism
  (`0.55h + 1.4 crown`), or passing trees vanish while filling the screen.
- Tiers: below ~14 threshold pixels one part draws a solid crown proxy;
  the bundled band packs four three-tuft boughs per meshlet, with each
  coarse tuft a single tented quad (two triangles spanning the blade
  fan's splay -- a flat quad in the bough plane disappears edge-on); the
  hero band pays one thirteen-tuft fan bough per meshlet. Both
  representation switches align with ramp saturation.
- `forest_bough_slot` is total over any rank: bundling rounds the
  scheduled range past the sixty-three real slots, and an out-of-range
  read there once rasterized as screen-sized garbage triangles.
- The shadow pass draws only crown proxies plus trunk prisms; reception in
  the fragment shader uses a depth margin of several metres so the tree's
  own coarse proxy does not black out its crown.

## Constraints discovered

- **Metal's 16 KB mesh-output ceiling faults silently.** Two hero boughs
  per meshlet (234 vertices at 80-byte varyings) corrupted the pass with
  no error. Packing varyings to half precision *also* produced garbage
  triangles in motion -- plausibly an interpolant-layout disagreement on
  three-component half vectors -- and still frames never showed it. Hero
  coalescing and varyings packing return only under the Metal debugger,
  not by struct arithmetic.
- Xcode's GPU capture layer crashes the MetalFX temporal scaler under
  Metal 4; the generated scheme disables capture injection, and real GPU
  captures should run with `--upscaling linear` (the exact non-MetalFX
  fallback). `MOPPE_METAL_CAPTURE(_START,_FRAMES)` captures a trace from a
  chosen gameplay frame. Shader sources and line tables are embedded in
  the metallib for source-level attribution.

## Instruments, and what each can and cannot verify

- `moppe-tree-studio`: render(plane + tree). Solo specimen
  (`MOPPE_STUDIO_SOLO`), interactive dolly/orbit (W/S/A/D, P screenshots),
  deterministic dolly ladder (`MOPPE_STUDIO_DOLLY`).
- `tools/tree-lod-atlas`: the dolly cropped to constant apparent size, so
  the only thing changing between tiles is the LOD decision itself.
- In-game: `F` walks, `P` captures to `screenshots/run-<timestamp>/`.
- `MOPPE_RIDE_CAPTURE_DIR` records consecutive gameplay frames;
  `tools/ride-judge` encodes them to video and, with `GEMINI_API_KEY`,
  asks a video-capable model for a 1-5 temporal-stability rating.
- **Epistemics: still frames cannot verify temporal behaviour.** Settled
  captures kept "confirming" fixes the rider immediately refuted; the
  half-precision garbage was invisible in every still. Arithmetic in
  perceived units (fractions of frame height) plus video or a human ride
  are the only valid judges of motion. The atlas catches structural
  regressions in one glance; an FPS counter happily measures corruption.

## Performance state (August 2026, M2 Pro, windowed 2560x1600)

GPU frame ~16.5 ms at the 16.7 ms budget: scene pass ~10.3 ms (terrain +
forest + actors -- the only content-scaled cost), MetalFX temporal upscale
~4.4 ms (fixed), everything else ~1.3 ms. Toggleable effects are all under
0.7 ms. The rider sees 50-59 FPS in tree-heavy views: frames are not
skipped, presents slip to later vsync slots. Ranked levers, none yet done:
register pressure/occupancy in the forest mesh stage, half-precision
*arithmetic* inside fragment shaders (interior math only -- not
interfaces), the upscaler's fixed cost, hero-bough coalescing once the
varyings question is settled under the debugger, per-bough back-side
culling, and a Kuth-style rate-limited auto-LOD governor to hold 60 by
adaptation rather than heroics.
