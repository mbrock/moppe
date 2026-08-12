# Forest LOD: continuous organisms into a continuous stand

Status: implementation record of the conifer and stand LOD system in
`moppe/shaders/metal/forest.metal` and
`moppe/shaders/metal/forest_canopy.metal` as of August 2026, and of the
principles and instruments that shaped it. The population argument and
remaining work are in
[forest density and aggregates](forest-density-and-aggregates.md).

## The bet

No individual tree mesh exists in CPU or GPU memory. Every frame, the object
stage chooses each organism's detail from its projected size and the mesh
stage grows boughs, tufts, and blades from a seed (Kuth et al. 2025,
"Real-Time GPU Tree Generation", HPG; Sheaf `#EDURTK`, notes `#6S29CJ`). The
memory saving is the headline but not the point: because nothing is retained,
detail can be exactly right for the current frame and vary continuously.

Individual identity is not continuous below a few crown pixels, however. A
retained RGBA moment texture therefore records the closure, height interval,
and moisture of the actual forest population. A second texture partitions the
same conserved optical depth into four crown-height strata. A separate mesh
stage evaluates those strata as a finite population volume. The two paths
overlap in projected crown space; this is one population changing
representation, not a second forest behind the first.

The costs are structural: individual generation is paid every frame even when
the camera is still, stability of every generation input becomes a discipline
instead of a given, and ray tracing would need per-frame
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
- Tiers: below sixteen threshold *crown* pixels one part draws a solid crown
  proxy; total tree height no longer keeps a six-pixel-wide spruce in a
  seven-meshlet bough assembly. Above that, the
  the bundled band packs four three-tuft boughs per meshlet, with each
  coarse tuft a single tented quad (two triangles spanning the blade
  fan's splay -- a flat quad in the bough plane disappears edge-on); the
  hero band pays one thirteen-tuft fan bough per meshlet. Hero tuft width is
  branchlet-scale rather than inheriting the bundled tier's metre-wide
  coverage compensation; that distinction is what lets a walker see through
  a crown instead of encountering a ceiling of triangular shelves. Both
  representation switches align with ramp saturation.
- Individual retirement is based on projected *crown width*, not full tree
  height. Stand response and identity transfer share one broad
  eight-to-thirty-two-pixel interval, but identity transfers only where a
  24-metre filtered closure says the organisms actually form a stand. The old
  later retirement left solid cone proxies in front of an already complete
  population field. Foliage contracts toward the crown top and wood
  toward the root as that transfer proceeds; open woodland keeps its real
  silhouettes through their final seed-staggered four-to-5.2-pixel fade.
  Thus sparse trees never dissolve into a false sheet and a closed stand does
  not retain a forest of unrepeatable spikes.
- The stand object stage samples actual terrain height before projecting a
  patch. Its LOD distance is three-dimensional, so a glider above a stand and
  a walker beside it do not receive different rules disguised as the same
  distance. Horizontal distance only bounds the finite work window.
- Aggregate geometry is a world-stable nested population lattice inside
  24-metre dispatch patches. Each tree contributes separately normalized
  Gaussian footprints to four height strata: broad lower boughs, two middle
  bands, and a narrow leader. Their areas sum to the same projected crown area
  that produced closure. A four-metre child partition is authoritative while
  its cells exceed 3.5 scene pixels; between 3.5 and 2 pixels, all thirty-six
  children and all nine eight-metre parents coexist; below 2 pixels, only the
  parent partition remains. The handoff allocates retained optical depth
  before Beer--Lambert extinction rather than alpha-fading two independently
  complete forests. Parent and child sample explicit texture mip levels at
  their own footprint.
- Each cell renders as one soft camera-facing ellipsoid section (two
  triangles), with previous-frame camera orientation carried into its motion
  vector. This is not a connected roof: grazing views receive finite crown
  mass and parallax instead of a horizontal shelf, while top-down gaps remain
  real. World-cell jitter breaks the visible sampling lattice without making
  the result camera-relative. Both complete partitions fit in one bounded
  meshlet per crown stratum. Within each 64-patch object group, the object
  stage preserves input-grid order and submits all surviving patches in one
  stratum before the next; local translucent blend order therefore does not
  change merely because another patch enters the frustum. The 24-metre
  stand-support question remains independent of dispatch topology. The same
  retained population field also drives terrain and undergrowth closure.
- Before dispatch, the renderer conservatively filters the retained periodic
  population against the actual three-dimensional frustum and the earliest
  four-crown-pixel retirement bound. It sends compact projected-error records
  in eight front-to-back depth bins. The GPU retains the seeded retirement and
  exact organ schedule; CPU filtering only removes object groups that must
  produce no geometry. This is camera-orientation and altitude independent,
  not a ground-radius LOD.
- `forest_bough_slot` is total over any rank: bundling rounds the
  scheduled range past the sixty-three real slots, and an out-of-range
  read there once rasterized as screen-sized garbage triangles.
- Shadow representation follows the map's resolvable scale. The whole-world
  map still draws one sealed crown envelope per tree because a bough is below
  one of its texels. In the camera-local map, a conifer instead casts one
  broad, stable sample bough per whorl plus its trunk prism. Those nine boughs
  are the first nine ranks of the visible organism, not an unrelated cutout.
  A sealed local cone made a dense but visibly porous spruce stand an opaque
  wall to the sun and blacked out its navigable interior. Broadleafs retain
  their crown envelope until they have a resolved lobe shadow construction.
  Reception in the forest fragment shader still uses a depth margin of
  several metres so the tree's own coarse proxy does not black out its crown.

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
- `MOPPE_GLIDE=<gazetteer-shot>` records every consecutive frame of a bare
  translating camera in the actual world renderer. It is the isolation tool
  for aerial handoff; `moppe-testbed` is not vegetation evidence.
- `MOPPE_RIDE_CAPTURE_DIR` records consecutive gameplay frames;
  `tools/ride-judge` encodes them to video and, with `GEMINI_API_KEY`,
  asks a video-capable model for a 1-5 temporal-stability rating.
- **Epistemics: still frames cannot verify temporal behaviour.** Settled
  captures kept "confirming" fixes the rider immediately refuted; the
  half-precision garbage was invisible in every still. Arithmetic in
  perceived units (fractions of frame height) plus video or a human ride
  are the only valid judges of motion. The atlas catches structural
  regressions in one glance; an FPS counter happily measures corruption.

## Performance state (August 2026, M2 Pro)

Keep two output paths distinct. The earlier implementation sequence was
measured at 1280x720, native linear output and 4x MSAA: the forest fell from
5.8932 ms to 4.6528 ms and then 3.9211 ms median as crown retirement and the
first aggregate arrived. Its 14.807 ms all-features median remains historical
evidence for those changes, not a current baseline.

The current standard 32-case partition is the ordinary temporal path: a
2560x1600 drawable reconstructed from a 1280x800, single-sample scene. On the
v9 fast-profile seed-123 world (102,047 retained individuals), the first full
run exposed a 20.3002 ms median forest block and 34.753 ms all-features case.
That regression was not accepted. Moving field slopes from fragment to shared
aggregate vertices, making far structure crown-relative, filtering conservative
3D
candidate records before dispatch, and drawing eight front-to-back bins gives
the final full run:

- forest block: 9.7954 ms median, 10.0582 ms mean, 12.8311 ms p95;
- all features: 24.642 ms median, 25.495 ms p95; and
- 16 of 32 configurations still miss 60 Hz by median.

This was a 52-percent reduction from the rejected run while keeping the denser
population and first continuous optical stand. The four-stratum population
volume was then measured, not assumed. Its first closed-octahedron prototype
regressed the forest block to 14.5577 ms median. Replacing that eight-triangle,
front-and-back raster domain with one two-triangle ellipsoid section per cell
recovered the cost:

- forest block: 11.0599 ms median, 11.4177 ms mean, 14.3039 ms p95;
- all features: 25.624 ms median, 27.009 ms p95; and
- 16 of 32 configurations still miss 60 Hz by median.

The volume therefore costs 1.2645 ms median over the one-layer stand while
removing its horizontal-shelf failure. It is not a solved frame budget.
The first spatial-hierarchy experiment then tried 4-, 8-, and 16-metre cells.
It was rejected: at the renderer's current 2.4 km reach an eight-metre carrier
is still about 2.4 scene pixels wide, so the last rung added work and temporal
change before projected error justified it. A 4-to-8-metre hierarchy is the
accepted current tree. The parent and complete child partitions are
coalesced into one meshlet during transition rather than launching a second
mesh workgroup per stratum. The final full run measured:

- forest block: 11.1703 ms median, 11.7140 ms mean, 14.6704 ms p95;
- all features: 26.059 ms median, 27.077 ms p95; and
- 16 of 32 configurations still miss 60 Hz by median.

Against the fixed four-metre volume this is a 0.1104 ms median and 0.3665 ms
p95 forest-block increase, not a speedup claim. It bounds distant work while
preserving the image and leaves the real frame-budget problem visible.
Remaining performance work starts with a real trace of the surviving
individual pass: register pressure/occupancy, half-precision *arithmetic*
inside fragment shaders (not interfaces), hero-bough coalescing once the
varyings question is settled under the debugger, and per-bough back-side
culling. The host currently builds that conservative set by scanning every
retained individual; replace that scan with world-space bins or a hierarchy if
CPU sampling shows it matters, without weakening the camera-independent
visibility rule. The interaction with undergrowth also needs direct
attribution. A rate-limited auto-LOD governor is useful only after
the representation is visually coherent; it cannot repair a bad handoff.

The camera-local conifer shadow correction was separately attributed with
Metal 4 pass counters at the 1600x900 forest site, with bloom, auto exposure,
lens flare, light shafts, and motion blur disabled. Over the forest-on
configurations of the short 32-case attribution run, the accepted nine-bough
form measured 0.104 ms median / 0.106 ms p95 in the shadow pass. The old
sealed crowns measured 0.127 / 0.130 ms. An eighteen-bough experiment measured
0.184 / 0.329 ms and was rejected despite looking similar. These short runs
attribute the shadow construction; they do not replace the standard frame
benchmark above.

The ordinary temporal 32-case benchmark was then repeated in full. The forest
block measured 11.344 ms median / 14.6927 ms p95 and the all-features case
26.1484 / 26.9706 ms. Relative to the preceding nested-population checkpoint,
that is +0.1737 ms forest median, +0.0223 ms forest p95, +0.0894 ms
all-features median, and -0.1064 ms all-features p95: effectively the same
frame-budget problem, not a performance win. Sixteen of thirty-two
configurations still miss 60 Hz by median.
