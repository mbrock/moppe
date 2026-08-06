# Moppe rendering & platform architecture

Status: current Metal/backend implementation record. This document preserves
the port's technical decisions and implementation detail; the
[engine atlas](engine-atlas.md) is the current map of source ownership, state,
and CMake targets. A playable browser backend now implements the same renderer
contract through WebGPU; see [WebAssembly and WebGPU](web.md). Android remains
a future possibility.

## Port goals and retained constraints

1. Render through Metal on macOS and iOS with one shared game codebase.
2. Keep the game's look and feel: same pass order, same haze/lighting math,
   same HUD, same physics.
3. Abstract the renderer and platform behind small, game-shaped interfaces so
   a WebGPU (or other) backend is an additive job, not a rewrite.
4. Refactor as we go: split the 4000-line main.cc into modules, remove dead
   code, de-boost, kill hidden global state where cheap.
5. Modernize where it pays: vertex-pulled terrain from a height texture
   (replaces ~100 MB of CPU-built triangle-strip soup), an explicit post chain,
   background-thread world generation with a loading
   screen (required on iOS anyway).

## Review amendments (adopted)

A three-lens adversarial review (Metal correctness, architecture, iOS/build)
amended the first draft. The deltas, now integrated below, were:

- Shadow map is **Depth16Unorm**, not Depth32F: D32F lacks the Filter
  capability on Apple-family GPUs, so linear compare samplers (hardware PCF)
  would silently degrade on every iPhone. D16 filters+compares on Apple3+
  and Mac; ortho light depth over ~19 km makes D16 steps (~0.3 m) two orders
  below the tuned shader bias. The compare sampler is declared **in MSL**
  (constexpr sampler with compare_func), not via descriptor — descriptor-side
  compareFunction is Apple3+ and fails validation in the simulator.
- The shadow pass keeps **conventional Z** (clear 1.0, less-equal) — only
  the scene pass is reversed-Z; they share nothing but the light matrix.
  The Metal bias matrix is x: 0.5/+0.5, y: **-0.5**/+0.5 (texture-space
  Y flip vs GL), z: identity. glPolygonOffset(2,2) becomes
  setDepthBias(2, 2, clamp) on the shadow encoder.
- The height texture is accessed **exclusively via texture read()** at
  integer coords (R32F is not linearly filterable before Apple9); the
  RG16Snorm normal texture likewise reads at vertex rate.
- **All texture uploads go through a staging buffer + Metal 4 compute-encoder
  copy** — the destination textures and static geometry stay private on TBDR
  devices. Uniforms, transient DrawLists, chunk records, and emissions share a
  growable arena in each of three shared-event-paced frame slots.
- Scene-pass MSAA color/depth are **memoryless** on Apple-family GPUs
  (private on the simulator), storeAction resolve / depth dontCare. The
  present pass is **not** MSAA (it composites an already-resolved quad +
  HUD; revisit only if HUD edge aliasing annoys).
- Sky far-plane depth comes from the **vertex stage** (clip z = 0 under
  reversed-Z), never a fragment [[depth]] output, to keep early-z rejection
  of the expensive cloud shader on TBDR.
- Scene and post targets are **RGBA16Float**. macOS presents through an
  extended-linear sRGB RGBA16Float drawable and requests live EDR headroom;
  the filmic SDR grade stays below 1.0 while scene highlights can exceed it.
  iOS keeps an 8-bit SDR drawable.
- macOS frame pacing uses **CAMetalDisplayLink** on the main run loop. At 90 Hz
  or above, supported Metal 4 hardware alternates a MetalFX-generated midpoint
  with the stored real frame, so simulation and full world rendering run at
  half the display cadence while every display-link drawable is presented.
  Lower-refresh and unsupported configurations render directly on every
  update. MetalKit remains a platform view/input convenience, while drawable
  pacing, interpolation policy, frame delta, and EDR headroom cross explicit
  host/backend functions.
- One metallib **per SDK** (macosx / iphoneos / iphonesimulator) via
  xcrun -sdk. A macOS Command Line Tools build without the offline Metal
  compiler bundles combined MSL source for runtime compilation instead.
- MeshBuilder emits 32-bit indices when a mesh exceeds 65,535 vertices
  (the 301² ocean grid does); 0xFFFF/0xFFFFFFFF are reserved strip-restart
  values in Metal.
- `DrawList` gains `mult(const Mat4&)` (the bike orients itself with a
  glMultMatrixf basis frame today) and mesh draws take a per-draw model
  matrix; the sky pipeline takes the camera position as a uniform.
- `FrameEnv` carries **sun diffuse, sun specular, and ambient colors**.
  The sun color follows its physical elevation, from warm horizon light to
  soft ivory daylight. Ambient drives a shader-side hemisphere fill (cool
  sky above, warm ground bounce below), keeping gameplay silhouettes readable
  while the one directional sun remains the source of cast shadows.
- The per-frame view matrix composes the **camera-shake rotation** before
  FrameEnv is built; FrameEnv exposes the resulting right/up/forward basis,
  which replaces Dust's GL_MODELVIEW_MATRIX readback for billboards.
- The chase target uses a responsive spring while the camera body is lightly
  underdamped. A dense terrain-corridor test raises the eye immediately over
  slopes and ridges; the spring owns the gentler descent afterward.
- The uber shader's haze uses the terrain's **distance term only** — the
  valley-mist term stays terrain-exclusive, because the whole city sits at
  exactly the mist's full-strength altitude (H_CITY = 45) and would change
  atmosphere noticeably.
- HUD pipeline: **cull none** (the y-down ortho flips winding; this exact
  bug is documented at main.cc:3182). HUD coordinates are view **points**.
  On macOS the default drawable uses the view's full backing-pixel extent up
  to a 4.2 MP area cap; `--drawable-scale` overrides that policy, while
  `--render-scale` or `MOPPE_RENDERSCALE` chooses the 3D scene fraction that
  MetalFX reconstructs into it. `--scene-megapixels` optionally caps that
  scene area on desktop, and `--msaa` selects 1x, 2x, or 4x scene
  multisampling before pipeline creation. Safe-area insets offset HUD anchors
  and touch zones on iOS, and "Times" maps to "Times New Roman" on iOS.
- Underwater + motion blur no longer alias one texture (the GL build's
  shared m_blur_tex made submerged ghosts zoom the *current* frame); the
  port keeps an independent prevFrame. Divergence is deliberate.
- The mid-flight build policy was **copy-alongside**: game/ modules were new
  files; main.cc and mov/vehicle.cc kept their GL code compiling under scons
  until the step-4 cutover deleted GL in one commit.
- Async world generation: `setup()` returns fast; generation runs on a
  QoS-userInitiated background queue behind a loading screen (both OSes);
  buffer/texture creation is thread-safe in Metal, drawable access is not;
  the one-time shadow pass runs after handoff. Monotonic clocks only
  (CACurrentMediaTime / steady_clock — never gettimeofday).
- iOS input additions: mount/dismount button, contextual "ride again"
  button on the game-over screen, quit routed through the platform layer
  (no exit() on iOS), multipleTouchEnabled = YES, touchesCancelled
  clears every analog axis and held action (focus loss similarly releases
  keys on macOS).
- iOS bundle: UILaunchStoryboardName (else the app letterboxes at legacy
  resolution), orientation keys; speech shim holds one static
  AVSpeechSynthesizer (a local one deallocates mid-utterance).
- Tick policy: one tick per draw callback with actual dt clamped to 0.05 s
  (the physics is already variable-dt). macOS follows the active screen's
  maximum refresh rate with display sync enabled; iOS requests 60 FPS.

## Current module layout

The engine atlas gives the ownership map; this source-oriented view points to
the Metal implementation without treating one directory as one layer:

| Location | Current responsibility |
| --- | --- |
| `moppe/spatial/` | Header-only finite typed bundles, sampling, and optional local operations. |
| `moppe/terrain/` | Direct finite geology, programs, recipes, and terrain/hydrology algorithms. |
| `moppe/map/` | Concrete surface geometry, derived readings, water storage, and evaluator bridges over the terrain domain. |
| `moppe/mov/` | Vehicle and glider simulation. |
| `moppe/game/` | World owner/model, session, frame snapshot, focused presentation, and host composition; its files span several engine domains. |
| `moppe/render/` | Portable game-shaped renderer API, `DrawList`, text, and Metal/WebGPU backends. |
| `moppe/shaders/metal/` | SDK-specific Metal shader sources built into `moppe.metallib`. |
| `moppe/platform/` | Apple services plus macOS, iOS, and browser hosts. |

## Renderer API shape

Not a general RHI. It exposes exactly what the game needs; backends implement
this, not Vulkan-esque generality. Three tiers:

### 1. Retained resources

- `Texture` — 2D, formats: RGBA8, R32F, RG16S(norm), BGRA8 (render targets),
  Depth32F. Mip generation on request. Sampler state fixed per texture
  (repeat/clamp, filter, anisotropy, optional depth-compare).
- `Mesh` — immutable vertex (+ optional index) buffer with a list of
  `(DrawState, range)` runs. Built by `MeshBuilder`. Replaces display lists;
  used for city sectors, sky dome, ocean grid, and solid primitives.
- Terrain is special-cased (see below), not a Mesh.
- Terrain inspection can bind one generic R32F surface overlay with a value
  range, opacity, and palette. The renderer does not know whether its values
  mean slope, drainage area, basins, sinks, or stage differences.

### 2. DrawList — the immediate-mode layer

Replaces glBegin/glEnd + matrix stack + glColor + glutSolid*:

    dl.push(); dl.translate(v); dl.rotate_deg(a, axis); dl.scale(v);
    dl.color(r,g,b,a); dl.lit(true/false); dl.texture(tex or nullptr);
    dl.state(DrawState{...});             // blend, depth write, cull
    dl.begin(Prim::Quads); dl.vertex(v); dl.normal(n); dl.uv(u,v); dl.end();
    dl.cube(0.5); dl.sphere(r, 10, 8); dl.cone(...); dl.torus(...);
    dl.pop();

Implementation: vertices are transformed to **world space on the CPU at record
time** (normals by inverse-transpose upper-3x3, matching fixed-function GL +
GL_NORMALIZE) and appended to one interleaved streaming buffer. Consecutive
vertices with equal state coalesce into a single draw. Quads/quad-strips/
polygons/triangle-fans are triangulated at record time; lines become thin
camera-facing quads (only the HUD uses lines). Per-frame the whole list is a
handful of draw calls on one dynamic buffer (double/triple-buffered).

Vertex format (interleaved, 40 B): float3 pos, float3 normal, float2 uv,
u8x4 color, u8x4 flags (x: lit, y: fogged, z: wind bend, w: foliage
flutter). The uber shader also uses the flutter weight as material identity:
thin foliage receives directional sun transmission while opaque wood and
ordinary props remain on the normal diffuse/specular path.

`MeshBuilder` records through the same API but bakes to an immutable Mesh.
State changes inside a bake (e.g. unlit lamp glow spheres) become run
boundaries.

### 3. Fixed passes and backend ownership

Fixed pass structure per frame, expressed as explicit API on `Renderer`:

    shadow pass (once per world)  → 4096² Depth16, terrain + forest proxies
    scene pass
       spatial/linear: memoryless MSAA → sceneA, reversed-Z depth
       temporal: jittered 1x sceneA + persistent reversed-Z depth +
                 RG16F motion + R8 reactive mask
       terrain → sky → forest assemblies → undergrowth → immediate world
       draw list (stars, wildlife, fish, vehicles, walker, people, cars,
       blob shadows) → water (sea, lakes, and painted rivers) → dust
    reconstruction (when scene < drawable)
       MetalFX temporal: color/depth/motion/exposure/reactive → native HDR
       MetalFX spatial: linear HDR scene → native-size RGBA16F
       linear fallback: one exact bilinear enlargement → native-size HDR
    post passes (native-size ping-pong as needed)
       underwater grade (when camera submerged)
       motion-blur ghosts: current += 3 zoomed alpha quads of prevFrame
       blit current → prevFrame (feedback persists across frames)
    present pass: final treatment + HUD
       direct: reconstructed scene → drawable
       frame interpolation: tone-mapped HUD-free color + current UI composite
          → MetalFX midpoint → current display-link drawable
          next display-link drawable ← stored current UI composite

The Metal implementation realizes this as a fixed, concrete encoding path,
not as a generic render graph. `MetalRenderer` remains the small facade for
the game-shaped `Renderer` interface. Its private resource owners make the
otherwise long-lived Metal state visible at the right lifetime:

| Owner | Lifetime and contents |
| --- | --- |
| `MetalTerrainResources` | A completed world: terrain topology/index templates, current and prior height/normal textures, material and presentation rasters, inspection overlay, and the terrain shadow/light transition state. |
| `MetalWaterResources` | A completed world: the ocean grid, horizontal-water levels, current/flow fields, and water-specific presentation state. Water borrows the terrain domain; it does not duplicate terrain ownership. |
| `MetalFrameTargets` | The renderer target configuration: scene color/depth, temporal motion/reactive inputs, native post ping-pong, previous-frame feedback, bloom, probe/exposure, optional temporal/spatial MetalFX scaler, frame interpolator, per-flight color/UI/output textures, and their shared fence. It recreates these on target-size or policy changes and owns both reconstruction and interpolation history validity. |
| `MetalFrameEncoding` | One drawable frame: current Metal 4 command buffer, reusable command-allocator ring, shared completion event, drawable, frame parameters, argument tables, selected frame arena, current scene target, counter-heap timestamp spans, and capture bookkeeping. It owns no retained world texture. |

Concrete Terrain, Water, Scene, Post, and HUD pass operations receive only
the owners and pipeline state they read or write. Terrain and Water both
borrow their retained resources plus `MetalFrameTargets` and
`MetalFrameEncoding`; Scene owns sky, immediate world geometry, and effects;
Post owns the explicit ping-pong/feedback sequence; HUD owns the final
composite and 2D list. This is deliberately a set of named, game-specific
operations rather than a scheduler or dependency-graph abstraction.

Terrain, Water, and Scene still share one lazy scene render encoder. The
encoder begins with the selected scene attachments, preserves their
depth/stencil ordering across those operations, and closes exactly once before
Reconstruction. Separate names therefore do not imply separate Metal encoders or a
change to the established scene order. The HUD operation remains responsible
for the final composite even when its 2D list is empty.

`MetalRenderer` also remains the frame-lifecycle coordinator: it records one
reusable command buffer, submits it with Metal 4 commit feedback, signals the
shared completion event and drawable, brackets ready-world Metal capture,
performs screenshots, and collects counter-heap timing. Concrete passes encode
work under that lifecycle; they do not independently submit, capture, or time.

### Metal 4 execution contract

The Apple backend intentionally has no pre-Metal-4 path. Apple deployment
targets are 26.0 and offline shaders compile as `metal4.0`. The execution
model follows Apple's Metal 4 core guidance and WWDC25 sessions:

Apple's 26.5 Simulator SDK omits the Metal 4 core Objective-C API. Simulator
projects still compile, but launch fails with a direct explanation; Moppe does
not retain a second legacy Metal transport that behaves differently from its
device backend. Validate rendering on macOS or a Metal 4 iPhone/Apple TV.

- One reusable `MTL4CommandBuffer` records each frame with one command
  allocator per in-flight slot. A shared event paces reuse; queue-level
  drawable waiting/signaling replaces command-buffer presentation and
  completion semaphores.
- Four stage-specific argument tables replace per-draw `set*Bytes`, buffer,
  texture, and sampler calls. The CPU writes stable GPU addresses and resource
  IDs; each frame arena preserves them until its slot completes and adds
  resident spill allocations only for exceptional frames.
- A sampler descriptor is a mutable recipe for the filtering, mip, and
  addressing behavior of the immutable sampler state created from it. Every
  sampler state placed in a Metal 4 argument table is created with
  [`supportArgumentBuffers`](https://developer.apple.com/documentation/metal/mtlsamplerdescriptor/supportargumentbuffers)
  enabled. Apple documents this flag as permitting the resulting sampler to
  be referenced by resource ID from an argument buffer; Moppe likewise writes
  that ID into its argument table. Without the flag, ordinary rendering worked
  but Xcode Metal capture replay consistently segfaulted; enabling it made
  replay succeed. That last observation establishes the required debugger
  contract, not the debugger's internal cause.
- One explicit residency set contains retained textures, private static
  geometry, frame arenas, and targets. The `CAMetalLayer` residency set is
  attached separately. Target replacement waits for prior frame use before
  old allocations leave residency.
- Named Metal 4 barriers express render-to-render and render-to-copy
  dependencies. Upload, readback, feedback-copy, and mip generation use
  `MTL4ComputeCommandEncoder` blit-stage operations.
- Commit feedback supplies whole-frame GPU time and errors. Optional
  `MTL4CounterHeap` timestamps retain game-pass labels without depending on
  the legacy Tracy Metal integration.
- On supported macOS and iOS devices, one renderer-owned `MTL4Compiler`
  creates the requested temporal or spatial MetalFX scaler at
  target-configuration lifetime. Its reported texture usages shape the scene
  and output allocations, one fence synchronizes the untracked scaler work,
  and the scaler encodes into the frame's existing command buffer.

Primary references are Apple's
[Metal 4 core API overview](https://developer.apple.com/documentation/metal/understanding-the-metal-4-core-api),
[Go small with Metal 4](https://developer.apple.com/videos/play/wwdc2025/205/),
and [Discover Metal 4](https://developer.apple.com/videos/play/wwdc2025/254/).

Conventions: reversed-Z (clear 0, GREATER_EQUAL; near 0.5, far 9000/30000
demands it), Metal [0,1] clip z, non-sRGB formats everywhere to preserve the
original gamma-space look. The sky shader forces depth to the far plane
(z = 0 under reversed-Z) and tests against cleared depth, so terrain still
occludes the expensive cloud shader.

Horizontal water is one continuous `terrain::WaterSheets` field. Standing
bodies first contribute their flood level and body-scale wave amplitude. The
dense `RiverAlignment` trajectories then paint a shallow bank-constrained
level and planar current into that same field. Their level profile is monotone
downstream, headwaters taper in width and depth, and all tributaries share the
outgoing reach's junction level. Overlapping current stamps blend vectors, so
the confluence owns one surface and one smoothly turning flow basis rather
than several reach meshes fighting at the same pixels. Traversed channel-like
pools retain their flood level but receive running-water amplitude and flow.

The Metal near-water mesh reconstructs the signed water-minus-ground field
cell by cell. It creates exact edge intersections, applies an asymptotic
decider to ambiguous saddle cases, and triangulates only the wet polygon.
Bends, junctions, pool transitions, and mouths are therefore unions in one
field: there are no reach caps, overlapping alpha edges, or dry vertices moved
while their old triangles remain attached. The fragment shader derives an
anisotropic texture frame from the current vector. Its two advected phases
stretch across the channel and travel along it; because junction currents
already blend, the shading turns through a confluence without a texture seam.
Depth drives absorption and clarity while current drives aligned normal
detail, subtle trough contrast, and rapid churn. The optical blend is split
into the terms it actually owns. Air-to-water Fresnel with an IOR of 1.333
reflects the procedural sky and supplies the GGX sun glint. Per-channel
Beer--Lambert extinction determines how much of the already-rendered bed
survives, and only the removed light becomes depth-colored in-scattering.
Normal-incidence shallows are therefore mostly bed, deep water retains a
colored body, and grazing water becomes a mirror without a hand-tuned
reflection cap. Turbidity shortens the extinction path but does not erase the
surface Fresnel film. Standard alpha blending can express those three terms
because the shader normalizes its surface radiance by the coverage left after
bed transmission. This is optical transmission, not displaced screen-space
refraction; the latter still requires an opaque-scene sample.

The procedural environment contains sky but no terrain. That omission is
mostly honest for lakes and the ocean, but it made a little shaded stream
reflect a bright open horizon where the real ray would meet its banks, reeds,
or canopy. Running-water identity and shallow depth now select a restrained
green/earth bank-radiance proxy at grazing reflection angles. The physical
Fresnel term is unchanged; the approximation changes the environment being
reflected, and a spatially coherent branch keeps the work off standing-water
fragments. This is not screen-space reflection and does not claim to reproduce
a particular tree or bank silhouette. The identical 64-configuration GPU cube
measured a 7.143 ms overall median after this pass versus 7.163 ms at the prior
water checkpoint. Its isolated ocean estimate moved by 0.052 ms while the
paired-frame spread remained over one millisecond, evidence of parity rather
than a measurable regression.

The macOS-only reflection atelier remains outside this composition.
`MOPPE_WATER_REFLECTION_SIGNAL=/tmp/signal.png` allocates quarter-linear-
resolution targets per in-flight slot, rerasterizes the same standing-water
surface into float world origins and shared optical normals, and traces the
bounded Goal 0 terrain proxy. Raw radiance, hit normal, hit distance, and
input/visibility/hit validity remain separate textures. The command
`tools/capture-water-reflection-signal /tmp/signal.png` writes an untouched
lake scene, a six-panel diagnostic, and a text report. Running water,
composition, denoising, temporal history, and scene instances are excluded.
This is a measured representation proof, not a graphics setting.

A height field cannot express vertical water. `WaterfallSurface` therefore
builds only a small explicit curtain for each selected hydrological
nickpoint. The curtain follows the lip-to-foot direction, accelerates
ballistically down the drop, widens toward the plunge pool, and uses the
flowing-water shader's falling detail. Its cost is 216 vertices per waterfall,
independent of river-alignment length. Falling spans are omitted from the
horizontal field, so the curtain bridges lip and foot instead of overlapping a
sloped water ramp. This is the complete representation split: one field for
all horizontal sea, lake, pool, and river water; one primitive for vertical
nickpoints.

Feature-targeted visual checks use
`tools/capture-water /tmp/water.png FEATURE`, where `FEATURE` is `stream`,
`river`, `confluence`, `mouth`, `waterfall`, or `lake`. The selector consumes
the same hydrology data, logs the chosen cell and score, uses a fixed clean
camera, and still runs the empty HUD pass because that pass performs the final
post-chain composite into the screenshot drawable. The stream view targets a
root reach and looks upstream so its source or lake handoff remains in frame.

`tools/water-benchmark` builds a multi-seed gallery around these inspection
cameras, normalizes output dimensions, and records missing features and capture
metadata. See `docs/water-benchmarks.md` for the corpus and review rubric.

## Terrain: vertex pulling (the big modernization)

Today: regenerate() builds ~8.4 M vertices of non-indexed strip soup (fine +
4x-decimated LOD, ~100 MB in 4 VBOs), rebuilt on every world change.

New: heights live in an R32F 2049² texture; per-vertex normals (the same ones
the CPU computes for physics) live in an RG16Snorm texture (y reconstructed).
The terrain vertex shader derives grid (x,z) from the index-buffer value and
a per-chunk origin, samples height, and computes world position — no vertex
buffers at all. Five shared index templates cover a smoothly reconstructed
513×513 near grid at quarter-source-cell spacing, followed by native 129×129
and stride-2/4/8 grids. The near field uses bounded Catmull-Rom heights and
surface-derived normals, then morphs back to the authoritative terrain. Each
finer level morphs onto the exact triangle surface of its parent before the
chunk changes LOD, avoiding pops and boundary cracks without skirts. Per
frame the CPU culls the same 128×128-cell chunks (distance + conservative
behind-camera tests) and issues at most one tiny indexed draw per visible
chunk. World regeneration = re-upload two textures.

Press `G` to toggle the terrain vertex overlay at runtime, or set
`MOPPE_TERRAIN_TOPOLOGY=1` to start with it enabled. Cyan lines and points are
the actual vertex-pulled triangles and vertices. Amber points are authoritative
height/normal field samples: the rows materialized in the surface bundle. The
subtle background tint identifies changes in geometric LOD. The dense
quarter-cell mesh fades before becoming sub-pixel, while its source samples
remain visible for comparison.

Physics samples the authoritative typed `SurfaceGeometry` bundle through
`spatial::sample<surface_elevation>` and
`spatial::sample<terrain_normal>` (~10 samples/frame).
Rendering and physics share the exact grid
samples; the reconstructed near surface is bounded to each source cell's
corner range but can differ between samples from physics's bilinear surface.
It morphs back before the native LOD. Terrain generation writes the bundle's
elevation column and the renderer uploads that same typed storage.

The splat/shadow/haze fragment shader ports 1:1 from shaders/test.frag with
explicit uniforms (sun dir, fog color/scale, heightScale, seaLevel, texScale,
light matrix, shadow strength) instead of gl_* built-ins. Lighting moves to
world space (the eye-space detour existed only because fixed-function GL
transformed lights by the modelview).

Terrain materials also consume the hydrology's standing-water and moisture
rasters. Submerged beds and damp banks lose diffuse energy and gain a restrained
wet sheen. Close shallow beds and a quieter share of their dry margins resolve
into one stable jittered cellular pebble field: size, aspect, orientation,
mineral albedo, seam, and normal all come from the same stone identity, then
retire before becoming subpixel. Grass responds more quietly to moisture. Its
root is tested against the signed water surface at the actual jittered shoot
position, preventing mixed shoreline tiles from planting blades underwater;
the dry side of the crossing becomes a subtly denser, taller riparian band.
Cliff material uses triplanar projection, a slate/taupe palette, and
world-height strata to avoid stretched red faces. Screen-space world-position
derivatives suppress aggregate, trail-gravel, pebbles, and snow-specular
frequencies as they become subpixel, including nearby ground viewed almost
parallel to its surface. These are shading effects only and do not alter
collision geometry.

The dirt source is a close photograph of loose gravel. Its centimetre-scale
contrast is pre-integrated with an additive mip bias before lighting, so it
supplies material color without turning every trail into a field of isolated
bright and dark grains. Compacted paths then restore deliberate world-scale
structure with lower-contrast coarse aggregate and broader, weaker normal
relief. Their 3.2 m and 0.79 m procedural octaves each retire against their own
pixel footprint; the fine octave is gone before a pixel spans half its
wavelength. This prevents distant crawl and also keeps EDR from preserving
single-pixel gravel peaks as false highlights.

Weather is one lighting system rather than a sky decoration. The game's
bounded cloudiness reading drives both cloud coverage and a broad layer
projected from 420 metres along the sun vector. Its 1.6 by 0.8 metres/second
drift modulates direct sun and specular response consistently on terrain,
world geometry, and water; ambient sky light remains available beneath the
layer. The field uses two warped triangle waves instead of texture traffic or
trigonometric hash noise. This preserved the slow, soft result while reducing
the measured 32-configuration median GPU cost from about 10% in the first
prototype to measurement noise. An order-balanced adjacent A/B against the
untouched checkpoint measured the final renderer 0.13 ms (0.97%) faster across
configuration medians and 0.05 ms faster with every feature enabled; those
small differences are evidence of parity, not a claimed speedup.

Forest cover changes lighting as well as albedo. Explicit nearby crowns still
provide silhouettes, but their existing filtered canopy grain now attenuates
direct and hemisphere light on the forest floor. The signal converges toward
an aggregate response with distance rather than adding subpixel tree or grass
geometry. This is the small Moppe-specific version of Bruneton and Neyret's
near-tree plus distant shader-map decomposition: ground radiance carries
canopy shade as geometry fades (`#5BNBQQ`, `#X9PHPN` in the Sheaf literature
library). Clearings, trails, snow, and submerged ground explicitly remove the
canopy footprint.

The bounded material readings use R16F/RG16F textures and one hardware-linear
sample in the fragment shader. The half-texel coordinate convention makes an
integral terrain site land exactly on its stored reading, while repeat
addressing preserves the periodic seam. Moisture and forest cover are bounded
proportions and narrow to R16F at upload; this halves their storage and
bandwidth without approaching a visible threshold step. Physical elevation
and water level remain R32F and retain explicit four-read interpolation on
Apple GPUs where that format is not filterable. Fragment normals similarly
filter the RG16Snorm x/z pair once and reconstruct y, instead of reconstructing
and mixing four complete normals.

Splat layers are evaluated only where they can contribute. Lowland grass,
paths, and beaches do not fetch the six triplanar cliff samples or three snow
samples after their material weights have reached zero. The branches are
spatially coherent terrain regions rather than per-pixel noise. Terrain
specular is composed after diffuse albedo: stone, snow, and wet-soil glints
therefore retain the sun's neutral color instead of being multiplied by the
ground texture.

Relief below the lattice is one band-limited field, `terrain_relief_gradient`,
shared by the material micro-normal, the pebble bed, and the drainage rills.
It is a four-octave sum of `moppe_value_noise_d` — value noise with an
analytic gradient under a quintic fade, so the slope stays continuous across
lattice cells — evaluated at world-space wavelengths and normalized to unit
RMS slope, which makes each caller's strength a micro-gradient it can state:
0.15 is roughly an eight-degree tilt whatever wavelength carries it. Amplitude
falls with wavelength at the same rate, so every octave contributes the same
slope and the sum is an average rather than a runaway. Each octave retires
once its wavelength approaches the width of a screen pixel, and the pixel
footprint is measured in all three axes rather than across the ground plane,
because a cliff's relief is read on a vertical plane. `terrain_relief_volume`
composes three plane evaluations under the splat triplanar's squared-normal
weights and skips the ones that carry no weight, so ordinary ground pays for
one; `terrain_perturb_normal` applies only the tangential part of the
gradient, so a steep face rolls along itself instead of through itself.
Character comes from the base wavelength — broken stone and fresh cuts
fracture coarser than turf; alluvium answers by losing amplitude rather than
gaining frequency.

This replaced a micro-normal reconstructed from the screen derivatives of the
composed albedo. That signal has no wavelength: its detail sat on the pixel
grid at every distance, so near ground could only ever resolve to a carpet of
one-pixel static, and it crawled whenever the camera moved. Retiring it also
removed the `dfdx`/`dfdy` tangent-frame reconstruction from the fragment
stage, and measured slightly cheaper than what it replaced.

Terrain generation writes directly into the authoritative typed elevation
column before texture upload. The renderer receives physical metre-valued
elevations and normals from the completed surface; it does not execute a
terrain expression graph or own an interactive generation preview.

## Other shader ports

- sky.frag (228 lines, fully procedural) → MSL 1:1; uniforms time, sunHeight,
  cloudiness, sunDir, fogColor.
- ocean.vert/frag → MSL on a regular **indexed** grid mesh; the vertex stage
  samples the
  water surface (RG32F: level plus per-body wave amplitude) for sea, lake,
  pool, and river elevation, and the fragment stage reads a second RG16F sheet
  of current arrows. Waves fade at shore,
  scale with the body's classification so tarns do not heave like the sea,
  and drive the shoreline lap from the same classification: at maximum retreat
  the sea moves about 6 cm, lakes about 6 mm, and ponds about 2 mm. Dry
  fragments are discarded. The grid is indexed because this vertex stage
  reads the water sheet and rides the swell: as plain triangles it spent that
  work 540,000 times over 90,601 distinct corners. It also skips the ten-tap
  shore filter past 900 m, where `ocean_surface_point` has already faded the
  swell to nothing and the fragment ripples take over — the same number out,
  for a grid that spans the whole world and has all but a couple of percent
  of its corners out there. On Metal3 hardware, standing water
  within 700 m additionally renders through a mesh pipeline on the terrain
  sample lattice. The object stage walks 8×8-cell tiles and culls wholly dry
  ones. The mesh stage fits the 9×9 corners and all 144 possible edge
  crossings into one 225-vertex meshlet, then emits at most 256 triangles by
  clipping each bilinear cell to its exact signed shoreline. The coarse grid
  keeps the horizon and standing bodies. Narrow running water fades into the
  terrain's channel-flux reading from 560–700 m and is rejected by the coarse
  pass rather than being widened into grid-sized distant triangles. Dry
  cameras reject the underside of elevated inland sheets; the two-sided sea
  remains available to the underwater pass.
- underwater.vert/frag → fullscreen-triangle post pass.
- Immediate/baked geometry uses one "uber" forward shader: Lambert + modest
  Blinn specular for lit runs, plus the terrain's exact haze formula (fog was
  previously fixed-function GL_EXP2 and absent for some props — unifying on
  the terrain curve makes the world consistent). Foliage is identified by its
  existing flutter weight and transmits warm directional light through
  back-facing leaves, respecting both the real sun direction and terrain
  shadows. Global conifers use three independently rotated crown whorls rather
  than one rigid pyramid.
- HUD uses a separate 2D pipeline (no lighting/fog, y-down ortho, scissor
  none, blend on).
- Dust, spray, smoke, and sparkles are bounded emission events. Metal derives
  deterministic particles from an integer counter hash and expands each live
  emission into billboard quads with a mesh shader (instanced vertex fallback),
  in separate soft-alpha and additive passes. Particle poses are analytic in
  logical time, so replay does not require a mutable particle array or RNG.

## Shadow map

The gameplay technique is one ortho depth render of the terrain from the fixed
sun, 4096² Depth16, hardware PCF via compare sampler, and a 5-tap weighted
kernel with slope-scaled bias in the terrain shader.
`MOPPE_PROFILE_SHADOW=1` prints the GPU time for the pass.

On macOS the Metal performance HUD is disabled by default; set
`MOPPE_METAL_HUD=1` when its frame rate, GPU time, and resource-memory readouts
are useful. `MOPPE_PROFILE_GPU=1` also writes one-second command-buffer GPU
time summaries to stderr, including timestamp-counter spans for the scene,
post effects, bloom, exposure probe, reflection atelier, and present/HUD
encoders. Encoder stages can overlap on tile-based GPUs, so those spans
diagnose expensive work but do not necessarily add up to the command-buffer
duration. The Metal backend converts its counter heap's device-clock ticks
with `queryTimestampFrequency`; they are not nanoseconds. Because Apple GPUs
execute world geometry as one tile render pass, the profiler attributes that
encoder as `scene` rather than claiming misleading draw-level boundaries.
`MOPPE_PROFILE_CPU=1` reports the
effective callback rate and CPU time in the game tick and render call. It also
splits renderer time into render-target maintenance, in-flight command-buffer
waiting, drawable acquisition, and Metal encoding/submission, making a missed
frame deadline distinguishable from compositor or drawable back-pressure.
`MOPPE_PROFILE_GPU_SIMPLE=1` reports only command-buffer GPU time without
injecting the more intrusive per-encoder counter samples.

### Where the water's cost turned out to be

The encoder spans are weak evidence about *which* work is expensive: on an
M2 Pro they arrive as stage boundaries, and a cheap quarter-resolution bloom
chain reports a longer span than the whole scene pass because the span
includes waiting. The `--graphics-benchmark` cube is the instrument that
answers the question, because it toggles one feature against a replayed tape.
Read it as paired runs of the same schedule and compare per configuration;
its ocean-on-minus-ocean-off contrast reproduces to a few tenths of a
millisecond, while its absolute frame time does not. Add
`--benchmark-pass-timing` when the CSV also needs precise
Metal 4 pass columns. That mode may split encoders and is for attribution, not
whole-frame throughput comparisons.

That historical contrast also had two regimes: with the then-named
`small-effects` block off the ocean cost about 12 ms, and with it on about
2 ms. Something else
saturates first in the second regime and the water hides behind it, so a
single averaged marginal cost describes neither.

Successive stubbing found the cost was nowhere it was assumed to be. Gutting
the entire ocean *fragment* shader — waves, ripples, shadow lookup, foam,
reflection, blending — recovered 2.2 of 11.9 ms. Stubbing the ten-tap shore
filter recovered 1.6. Rewriting the object stage's wetness probe to hoist its
integer modulo and leave each row's reads mutually independent recovered
nothing measurable. Disabling the near-water mesh pipeline outright recovered
8.6, and within it the expense was the triangles themselves: tiles a river
merely passes through, emitting full lattices for the fragment stage to
discard.

Two negative results are worth keeping, so they are not rediscovered. MSL
specifies that `discard_fragment()` marks the fragment without ending the
invocation, which suggests that following every rejection with a `return`
should save the rest of a long shader. Measured on this GPU across the whole
cube, it saves nothing — the implementation already stops a quad once all its
lanes are discarded — so the shaders keep the plainer form. And the object
stage's probe loop is not worth micro-optimizing at all: it runs 256 serial
iterations per dry tile and still does not show up.

### Terrain and lens sampling pass

A later paired replay addressed the cost common to every feature
configuration rather than another optional block. Half-format material fields
and fragment normals moved from four explicit reads to one filtered sample;
zero-weight cliff and snow regions stopped fetching those splat layers. The
bloom blur keeps the same nine-tap Gaussian kernel but combines adjacent
weights into five hardware-linear samples. The present pass takes one central
scene sample and confines its two extra red/blue samples to the outer lens,
where lateral chromatic aberration belongs; central scenery stays sharp.

On the local M2 Pro, a deterministic 960x600 high-quality replay with seed 123
and fast terrain used 120 prelude frames, 10 settle frames, and 30 measured
frames for each of the 32 feature configurations. The mean of their median GPU
times fell from 9.317 ms to 7.202 ms, a 2.115 ms (21.8%) reduction. All 32
configuration medians improved; the smallest improvement was 0.858 ms. These
numbers are machine- and schedule-specific, but the across-the-cube result
shows that the gain belongs to the shared terrain/post work rather than one
favorable feature interaction.

The supported `--graphics-quality balanced` preset renders the 3D scene at
two-thirds resolution while retaining every high-quality graphics feature.
It is the normal performance compromise for high-refresh or unusually wide
displays. The `--graphics-quality low` preset remains a deliberately severe
performance baseline: half-resolution 3D scene, no terrain shadows, ocean
surface, decorative particles, motion blur, bloom, exposure probe, or lens
flare. `--graphics-quality high` is the default full presentation. The low
preset retains terrain, vehicles, physics, sky, waterfall curtains, and HUD so
it remains playable while isolating optional rendering cost.

All presets request temporal MetalFX by default. When the scene is smaller than
the drawable and a physical macOS, iOS, or tvOS device supports Metal 4
MetalFX, the renderer reconstructs the linear HDR scene into a native-size
RGBA16F target before the existing tone map, print-like grade, EDR treatment,
lens treatment, and native HUD. `--upscaling linear` retains the former direct
linear sample for fallbacks and exact A/B comparisons. Startup reports
requested and resolved `native | temporal | spatial | linear` modes together
with both dimensions and the fallback reason.

The default temporal request selects the MetalFX temporal path when Metal 4
temporal scaling is available, falls back to spatial when only spatial
MetalFX is available, and finally to linear enlargement. A temporal scene
intentionally uses one raster sample per pixel: the temporal scaler supplies
antialiasing,
while keeping multisampled depth and motion would require costly and ambiguous
resolves. Spatial and linear retain the configured 2x/4x memoryless MSAA path.

Temporal projection jitter is a Halton (2,3) sequence. The raster projection
contains the subpixel offset, while motion is calculated from unjittered
current and previous clip positions and expressed directly in input pixels;
the same offsets are passed to MetalFX. Static terrain, sky rotation, wind,
generated undergrowth, and ballistic dust all produce prior-frame positions.
Moving retained meshes carry stable renderer motion IDs and previous model
matrices. The immediate actor list retains its prior world-space vertex stream
when topology matches; topology changes reject history for that content.

The reactive mask is deliberately material-specific rather than a global
"moving" bit. Opaque terrain has zero reactivity, foliage is mild, generated
undergrowth is moderate, and translucent/appearance-driven ocean, waterfalls,
dust, flames, and first-frame dynamic geometry reject progressively more
history. This lets accurate camera/object vectors do the normal reprojection
work while preventing alpha-blended or procedurally changing pixels from
leaving trails. A private 1x1 R16F texture supplies neutral exposure to
MetalFX. Reconstruction therefore preserves linear HDR pixels, while Moppe's
bloom and present passes own adapted exposure identically for native and
reconstructed scenes.

`Renderer::reconstruct_scene()` is the explicit boundary between world and
screen-space work. Temporal/spatial/linear reconstruction now precedes
underwater grade, motion-blur feedback, bloom, exposure probing, tone mapping,
and native HUD. Camera/world transitions call `reset_temporal_state()`, which
invalidates MetalFX history, prior camera/object/list state, exposure feedback,
and the older gameplay motion-blur history together.

On macOS, `--frame-interpolation on|off` controls MetalFX frame generation;
it defaults off, and ordinary play renders and presents directly at 60 Hz.
Motion blur also defaults off. The default desktop drawable stays at native
backing resolution up to 4.2 megapixels and scales larger surfaces down to that
area; `--drawable-scale` bypasses that automatic cap. The 3D scene defaults to
half the resulting drawable dimensions. An explicit interpolation request uses
the display's high-refresh cadence when MetalFX and temporal reconstruction can
supply the required depth and motion contract. The interpolator runs after
Moppe's native-size tone map. It receives the temporal scaler, current and
previous HUD-free color, reversed-Z depth, input-pixel motion, projection and
jitter, and a UI-composited copy of the current frame. The first frame primes
history. Later render callbacks present the generated midpoint, and the
following display-link callback presents the retained real frame without
rendering the world again. Gameplay simulation remains fixed at 120 Hz.
Startup reports both the render and
presentation cadences, while GPU pass profiling reports frame interpolation
separately from temporal upscaling.

The Apple TV default uses half of UIKit point resolution for the 3D scene.
Temporal MetalFX reconstructs that scene to the native drawable on supported
Apple TV hardware, with spatial and then linear reconstruction as fallbacks.
The present pass and HUD still use the native drawable. Temporal uses its 1x
jittered scene; spatial and linear fallbacks use 2x scene MSAA instead of 4x.
The television preset keeps water, particles, vehicle and star effects, lens
flare, and detailed terrain materials, while disabling
terrain shadows, motion blur, bloom, the exposure probe, and procedural
undergrowth. Those remain available through `--graphics-quality high` or
individual feature overrides. Explicit quality presets remain relative to the
point-resolution baseline, and `MOPPE_RENDERSCALE` remains an absolute
drawable fraction.

Presets resolve into a typed graphics-settings value rather than remaining a
quality-mode branch. Boolean features can then be changed independently with
`--graphics-enable` and `--graphics-disable`; each accepts a comma-separated
list such as `--graphics-quality low --graphics-enable ocean,bloom`. Startup
prints every resolved feature and numeric graphics setting so scripted
performance runs record the actual configuration. The legacy
`MOPPE_RENDERSCALE`, `MOPPE_NOSHADOW`,
`MOPPE_WATERFALL_CURTAINS`, `MOPPE_TERRAIN_TOPOLOGY`, and `MOPPE_SUNHEIGHT`
controls remain supported but are resolved centrally into the same settings.
Each Boolean feature descriptor also records whether it is hot-switchable:
changing a hot feature's stored value is sufficient for the next frame, with
no resource rebuild or renderer-state reset. Ocean, waterfall curtains,
particles, vehicle and star effects, bloom, automatic exposure, and lens flare
are currently hot. Forest is also hot: disabling it skips both its visible
mesh submission and its contribution to terrain shadow passes while retaining
its resident instance plan for later benchmark combinations. The fixed-size
waterfall mesh is prepared once even when its draw is disabled, which lets the
benchmark measure it independently. Terrain shadows and motion blur remain
conservatively marked not hot. The terrain topology overlay is hot and can be
toggled with `G`.
To create a trace for Xcode's Metal debugger, run
with `MOPPE_METAL_CAPTURE=/tmp/moppe.gputrace`; the first 120 frames are
captured after the world is ready by default, or set
`MOPPE_METAL_CAPTURE_FRAMES` to another count. Both capture and the timing
summary deliberately exclude the loading screen.

The light matrices are computed with our own Mat4 (ortho + lookAt), replacing
the "do matrix math on the GL projection stack" trick in shadow.cc. Bias
matrix maps x,y to [0,1]; z is already [0,1] in Metal (adjusted for
reversed-Z).

## Text

`render/text` builds a glyph atlas per font size at startup via a platform
glyph-rasterizer callback (CoreText on Apple — covers Helvetica 10/12/18,
monospace 8x13-alike, Times 24). Draw = textured quads in the HUD list.
WebGPU/Android later plug in stb_truetype/FreeType without touching game code.

## Platform layer

    struct Game {
      virtual void setup(render::Renderer&, int w, int h);
      virtual void resize(int w, int h);
      virtual void tick(seconds_t dt);          // fixed-ish step, ≤ 0.05 s
      virtual void render(render::Frame&);
      virtual void key(KeyCode, bool down);     // autorepeat filtered
      virtual void controls(const ControlState&); // analog steer/drive/boost
    };
    int platform::run(Game&, const Config&);    // per-OS entry
    std::string platform::asset_path(const char* rel);  // bundle-aware
    void platform::say(const char* phrase);     // AVSpeechSynthesizer / no-op

- macOS: NSApplication + MTKView delegate; native fullscreen by default with
  a `--windowed` development override. keyDown/keyUp with isARepeat filtered;
  releases are synthesized on focus loss so throttle can't stick.
- iOS: UIKit + MTKView, CADisplayLink-driven; two floating analog controls
  (left steer + throttle/brake, right continuous boost) plus camera and mount
  corner actions; world generation on a background queue behind a loading
  screen (mandatory — synchronous multi-second setup would trip the watchdog).
- Frame loop: MTKView draw callback accumulates real time and runs tick()
  gated at 1/60 with the same 0.05 s clamp (no more idle busy-spin).
- KeyCode is a proper enum — removes the ASCII/GLUT_KEY_* numeric collision
  hack ('d' == GLUT_KEY_LEFT == 100).
- Vehicle::render's hidden glutGet(GLUT_ELAPSED_TIME) becomes an injected
  time parameter.

## Historical game-code cutover record

The following notes record the port's source-level migration decisions. They
are retained as rationale, not as a current file map.

- main.cc splits into moppe/game/ one file per system; the file-scope mutable
  globals (map_size, water_level, fog_scale, fog color, mode flags) become a
  `WorldParams` + per-frame `FrameEnv` (fog color, sun dir, time, camera)
  passed explicitly.
- Vehicle loses its render()/set_camera()/draw_debug_text() — simulation
  stays put; a game/vehicle_render.cc draws bikes/cars from the public pose
  API via DrawList. Same for Walker.
- Dead code goes: render_vertex_arrays, TerrainRenderer::render_directly,
  gl::FrameBufferObject, InterpolatingHeightMap, Camera (the unused one),
  sky texture fallback, MouseCameraController (wired but its output is never
  read), draw_debug_text trio, GLUT text, GLEW/GLUT/boost vendoring, enet.
- boost → std: mt19937/<random> (terrain-per-seed changes; acceptable — the
  default seed is time(0)), shared_ptr, multi_array → small Array2D,
  operators.hpp → hand-written operators, format → snprintf.

## Build and host targets

CMake is the active build system. The [engine atlas](engine-atlas.md) names
the full target graph; the terminal and common entry points are:

- `moppe` — macOS .app bundle (also runs from the build tree; assets copied
  next to the binary), links Metal/MetalKit/AppKit/AVFoundation.
- `moppe-ios` — iOS app bundle, same sources, UIKit platform layer; built
  with the iOS toolchain (simulator target for CI/verification).
- `moppe-tests` — portable/unit suite, including the scene-facing checks that
  do not require a desktop event loop.
- `terrain-orogeny-benchmark` and water-depth experiments — developer-only
  world/terrain consumers.
- .metal → .metallib via xcrun at build time; metallib + textures + data
  ship as bundle resources; `asset_path()` resolves per platform.

## Historical porting order

The cutover completed in this order. The record remains useful when a current
implementation detail looks intentionally game-shaped rather than generic:

1. Foundation: Mat4, de-boosted math, render API headers, Metal device/
   swapchain/uber pipeline/streaming buffers, mac platform shell, CMake —
   proven with a spinning test scene + HUD text.
2. Terrain (height-texture pipeline + shadow pass), sky, ocean.
3. Game systems, one agent-portable module at a time: vehicle split, walker,
   dust, blob shadow, stars, fish, wildlife, city, HUD, post
   effects, game glue/input.
4. Parity check against the GL build (then still building from scons), then
   delete the GL path, scons, and vendored dependencies; update CLAUDE.md.
5. iOS target: platform/ios, touch controls, loading screen, simulator build.

## Non-goals (for now)

- GPU terrain generation & erosion (noted above).
- Networking (enet was never wired up).
- Changing the art direction, physics feel, or game rules.
