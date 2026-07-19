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
- **All texture uploads go through a staging buffer + blit encoder** — the
  simulator only allows private-storage textures, and it's the right answer
  on TBDR devices anyway. Streaming DrawList memory is a growable per-frame
  pool keyed to a triple-buffer semaphore.
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
- macOS frame pacing uses **CAMetalDisplayLink** on the main run loop and
  renders into the drawable supplied with each update. MTKView remains the
  input/view host and its automatic draw loop is the fallback on systems
  without CAMetalDisplayLink.
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
  bug is documented at main.cc:3182). HUD coordinates are view **points**;
  on macOS the drawable and 3D scene default to one pixel per view point, so
  a 2x Retina screen presents at half its physical width and height. This
  avoids spending four times the fill bandwidth on a sharper HUD and final
  composite. `MOPPE_RENDERSCALE` can reduce the 3D scene further without
  changing the drawable. Safe-area insets offset HUD anchors and touch zones
  on iOS, and "Times" maps to "Times New Roman" on iOS.
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
| `moppe/spatial/` | Header-only finite typed bundles and quantity vocabulary. |
| `moppe/terrain/` | Portable field DAG, programs, recipes, and terrain/hydrology algorithms. |
| `moppe/map/` | Concrete map storage, surface domain/atlas materialization, and evaluator bridges. |
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
u8x4 color, u8x4 flags (x: lit, y: fogged; rest reserved).

`MeshBuilder` records through the same API but bakes to an immutable Mesh.
State changes inside a bake (e.g. unlit lamp glow spheres) become run
boundaries.

### 3. Fixed passes and backend ownership

Fixed pass structure per frame, expressed as explicit API on `Renderer`:

    shadow pass (once per world)  → 4096² Depth16, terrain only
    scene pass  (MSAA 4x → resolve into sceneA, Depth32F reversed-Z)
       terrain → sky → city sectors → immediate world
       draw list (stars, wildlife, fish, vehicles, walker, people, cars,
       blob shadows) → water (sea, lakes, and painted rivers) → dust
    post passes (ping-pong sceneA/sceneB as needed)
       underwater grade (when camera submerged)
       motion-blur ghosts: current += 3 zoomed alpha quads of prevFrame
       blit current → prevFrame (feedback persists across frames)
    present pass (MSAA → drawable): fullscreen quad of final scene + HUD
       draw list (2D ortho, y-down, top-left origin) + text

The Metal implementation realizes this as a fixed, concrete encoding path,
not as a generic render graph. `MetalRenderer` remains the small facade for
the game-shaped `Renderer` interface. Its private resource owners make the
otherwise long-lived Metal state visible at the right lifetime:

| Owner | Lifetime and contents |
| --- | --- |
| `MetalTerrainResources` | A completed world: terrain topology/index templates, current and prior height/normal textures, material and presentation rasters, inspection overlay, and the terrain shadow/light transition state. |
| `MetalWaterResources` | A completed world: the ocean grid, standing-water levels, current/flow fields, and water-specific presentation state. Water borrows the terrain domain; it does not duplicate terrain ownership. |
| `MetalFrameTargets` | The renderer target configuration: MSAA scene color/depth, scene ping-pong textures, previous-frame feedback, bloom, probe, and exposure resources. It recreates these on target-size or quality changes and owns temporal validity separately from a world resource. |
| `MetalFrameEncoding` | One drawable frame: command buffer, drawable, frame parameters and uniforms, the selected in-flight stream slot, current scene target, timestamp spans, and capture bookkeeping. It owns no retained world texture. |

Concrete Terrain, Water, Scene, Post, and HUD pass operations receive only
the owners and pipeline state they read or write. Terrain and Water both
borrow their retained resources plus `MetalFrameTargets` and
`MetalFrameEncoding`; Scene owns sky, immediate world geometry, and effects;
Post owns the explicit ping-pong/feedback sequence; HUD owns the final
composite and 2D list. This is deliberately a set of named, game-specific
operations rather than a scheduler or dependency-graph abstraction.

Terrain, Water, and Scene still share one lazy scene render encoder. The
encoder begins with the scene resolve/depth attachments, preserves their
depth/stencil ordering across those operations, and closes exactly once before
Post. Separate names therefore do not imply separate Metal encoders or a
change to the established scene order. The HUD operation remains responsible
for the final composite even when its 2D list is empty.

`MetalRenderer` also remains the frame-lifecycle coordinator: it begins and
commits the command buffer, attaches the stable GPU timing spans and benchmark
completion handler, brackets ready-world Metal capture, performs screenshots,
and returns the in-flight stream slot only after completion. Concrete passes
encode work under that lifecycle; they do not create command buffers, commit,
or independently manage capture/timing state.

Conventions: reversed-Z (clear 0, GREATER_EQUAL; near 0.5, far 9000/30000
demands it), Metal [0,1] clip z, non-sRGB formats everywhere to preserve the
original gamma-space look. The sky shader forces depth to the far plane
(z = 0 under reversed-Z) and tests against cleared depth, so terrain still
occludes the expensive cloud shader.

Running rivers are explicit meshes built from `RiverAlignment`, a dense
continuous trajectory attached to every topological reach.  Drainage cells
remain the routing authority; a damped cubic Hermite reading removes D8
corners, interpolates area, slope, waterfall, and mouth state, and assigns an
arc coordinate that is continuous through confluences.  Width and depth are
physical functions of contributing area.  Seven vertices across each section
form a soft-edged ribbon, with depth recovered against the unmodified orogeny
heightfield and the derived water profile clamped non-increasing downstream.
At a confluence every tributary blends toward the outgoing tangent and bank
envelope, then terminates on the outgoing reach's exact seven-vertex first
section. A true headwater pinches to a damp point; a lake-fed root extends one
section into the wet outlet. The ribbon similarly dissolves beneath the
standing surface after crossing a mouth.

`river.metal` orients its detail from screen derivatives of that curved mesh,
so normals and foam follow bends rather than world axes.  Two advected phases
reset and hand over out of phase, avoiding the texture stretch and snap of a
single scrolling normal map.  The global arc coordinate keeps phase coherent
at reach joins. Rapid, depth, waterfall, and feather signals arrive in the
vertex color channels. The measured water column drives spectral attenuation,
opacity, and a shallow bank-contact band. Cross-channel position and depth
shape advection speed, approximating a bank-confined velocity profile without
a per-river flow solve. Dry ribbon fragments are discarded before the
first-fragment overlap stencil can mask valid water underneath.

`terrain::paint_watercourses` now reserves the lattice water surface for real
standing bodies.  It only stamps the continuous river current into wet cells
past each mouth, allowing the ocean/lake material and ribbon to meet without
inventing dry-reach water levels or reverse-engineering banks from a raster
carve.  Terrain, drainage, and running-water geometry therefore remain three
explicit readings instead of mutating one another.

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

Physics keeps its authoritative CPU copy (HeightMap::interpolated_height/
normal, ~10 samples/frame). Rendering and physics share the exact grid
samples; the reconstructed near surface is bounded to each source cell's
corner range but can differ between samples from physics's bilinear surface.
It morphs back before the native LOD. Terrain generation bakes into the CPU
heightmap and just re-uploads.

The splat/shadow/haze fragment shader ports 1:1 from shaders/test.frag with
explicit uniforms (sun dir, fog color/scale, heightScale, seaLevel, texScale,
light matrix, shadow strength) instead of gl_* built-ins. Lighting moves to
world space (the eye-space detour existed only because fixed-function GL
transformed lights by the modelview).

Terrain materials also consume the hydrology's standing-water and moisture
rasters. Submerged beds and damp banks lose diffuse energy and gain a restrained
wet sheen; close shallow beds receive procedural pebble-scale bump detail.
Grass responds more quietly to moisture, while cliff material uses triplanar
projection, a slate/taupe palette, and world-height strata to avoid stretched
red faces. Screen-space world-position derivatives suppress aggregate,
micro-normal, pebble, and snow-specular frequencies as they become subpixel,
including nearby ground viewed almost parallel to its surface. These are
shading effects only and do not alter collision geometry.

The pointwise terrain algebra now lowers to a Metal 4 function-stitching graph
and runs in a compute kernel.  Terrain Lab currently reads the result back to
the authoritative CPU heightmap before normalization, erosion, and texture
upload.  Interactive previews derive normals from the height texture, reuse
terrain GPU resources, and morph old and new height textures over 120 ms.
They restore exact CPU normals when leaving the lab.
The next renderer boundary is to keep pointwise results GPU-resident through
global normalization and rendering. Orogeny remains a separate iterative
problem rather than part of the pointwise graph.

## Other shader ports

- sky.frag (228 lines, fully procedural) → MSL 1:1; uniforms time, sunHeight,
  cloudiness, sunDir, fogColor.
- ocean.vert/frag → MSL on a regular grid mesh; the vertex stage samples the
  standing-water surface (RG32F: level plus per-body wave amplitude) for
  ocean and lake elevation, and the fragment stage reads a second RG16F sheet
  of mouth-current arrows. Waves fade at shore,
  scale with the body's classification so tarns do not heave like the sea,
  and drive the shoreline lap from the same classification: at maximum retreat
  the sea moves about 6 cm, lakes about 6 mm, and ponds about 2 mm. Dry
  fragments are discarded. On Metal3 hardware, standing water
  within 700 m additionally renders through a mesh pipeline on the terrain
  sample lattice (object stage walks 15×15-cell tiles, probes every tile
  corner for wetness — exact, since water-minus-ground is bilinear per
  cell, so a river a cell and a half wide cannot slip between probes —
  and culls; mesh stage emits 16×16 lattices sharing ocean_fragment); the
  coarse grid keeps the horizon, both passes discarding on the same radius
  so they partition exactly.
- underwater.vert/frag → fullscreen-triangle post pass.
- Immediate/baked geometry uses one "uber" forward shader: Lambert + modest
  Blinn specular for lit runs, plus the terrain's exact haze formula (fog was
  previously fixed-function GL_EXP2 and absent for some props — unifying on
  the terrain curve makes the world consistent).
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
kernel with slope-scaled bias in the terrain shader. The Terrain Lab uses an
explicit preview quality: 1024² at source stride 2. Lab shadow maps are
double-buffered and their visibility results crossfade on the same 120 ms
clock as heightfield transitions, so a lowering mountain never receives the
stale shadow of its old geometry. `MOPPE_PROFILE_SHADOW=1` prints the GPU time
for the pass.

On macOS the Metal performance HUD is disabled by default; set
`MOPPE_METAL_HUD=1` when its frame rate, GPU time, and resource-memory readouts
are useful. `MOPPE_PROFILE_GPU=1` also writes one-second command-buffer GPU
time summaries to stderr, including timestamp-counter spans for the scene,
post effects, bloom, exposure probe, and present/HUD encoders. Encoder stages
can overlap on tile-based GPUs, so those spans diagnose expensive work but do
not necessarily add up to the command-buffer duration. Devices supporting
draw-boundary timestamps additionally split the scene into terrain, sky,
water and other scene geometry. `MOPPE_PROFILE_CPU=1` reports the
effective callback rate and CPU time in the game tick and render call. It also
splits renderer time into render-target maintenance, in-flight command-buffer
waiting, drawable acquisition, and Metal encoding/submission, making a missed
frame deadline distinguishable from compositor or drawable back-pressure.
`MOPPE_PROFILE_GPU_SIMPLE=1` reports only command-buffer GPU time without
injecting the more intrusive per-encoder counter samples.

The supported `--graphics-quality balanced` preset renders the 3D scene at
two-thirds resolution while retaining every high-quality graphics feature.
It is the normal performance compromise for high-refresh or unusually wide
displays. The `--graphics-quality low` preset remains a deliberately severe
performance baseline: half-resolution 3D scene, no terrain shadows, ocean
surface, decorative particles, motion blur, bloom, exposure probe, or lens
flare. `--graphics-quality high` is the default full presentation. The low
preset retains terrain, vehicles, physics, sky, rivers, and HUD so it remains
playable while isolating optional rendering cost.
Presets resolve into a typed graphics-settings value rather than remaining a
quality-mode branch. Boolean features can then be changed independently with
`--graphics-enable` and `--graphics-disable`; each accepts a comma-separated
list such as `--graphics-quality low --graphics-enable ocean,bloom`. Startup
prints every resolved feature and numeric graphics setting so scripted
performance runs record the actual configuration. The legacy
`MOPPE_RENDERSCALE`, `MOPPE_NOSHADOW`,
`MOPPE_RIVER_RIBBONS`, `MOPPE_TERRAIN_TOPOLOGY`, and `MOPPE_SUNHEIGHT`
controls remain supported but are resolved centrally into the same settings.
Each Boolean feature descriptor also records whether it is hot-switchable:
changing a hot feature's stored value is sufficient for the next frame, with
no resource rebuild or renderer-state reset. Ocean, river ribbons, particles,
vehicle and star effects, bloom, automatic exposure, and lens flare are
currently hot. River meshes are prepared once even when their draw is
disabled, which lets the benchmark measure them independently. Terrain shadows
and motion blur remain conservatively marked not hot. The terrain topology
overlay is hot and can be toggled with `G`.
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
- `map-test`, `terrain-pipeline-demo`, and related command-line tools —
  developer-only world/terrain consumers.
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
