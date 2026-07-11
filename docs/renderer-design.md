# Moppe rendering & platform architecture (Metal port)

Status: design for the GLUT/OpenGL → Metal port (macOS + iOS), written to keep
the door open for WebGPU/Android backends later.

## Goals

1. Render through Metal on macOS and iOS with one shared game codebase.
2. Keep the game's look and feel: same pass order, same haze/lighting math,
   same HUD, same physics.
3. Abstract the renderer and platform behind small, game-shaped interfaces so
   a WebGPU (or other) backend is an additive job, not a rewrite.
4. Refactor as we go: split the 4000-line main.cc into modules, remove dead
   code, de-boost, kill hidden global state where cheap.
5. Modernize where it pays: vertex-pulled terrain from a height texture
   (replaces ~100 MB of CPU-built triangle-strip soup), explicit pass graph
   for the post effects, background-thread world generation with a loading
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
  drawable pixels per point may be lower than the screen backing scale when
  macOS halves extreme resolutions above 12 MP. `MOPPE_RENDERSCALE` overrides
  that heuristic. Safe-area insets offset HUD anchors and touch zones on iOS,
  and "Times" maps to "Times New Roman" on iOS.
- Underwater + motion blur no longer alias one texture (the GL build's
  shared m_blur_tex made submerged ghosts zoom the *current* frame); the
  port keeps an independent prevFrame. Divergence is deliberate.
- Mid-flight build policy is **copy-alongside**: game/ modules are new
  files; main.cc and mov/vehicle.cc keep their GL code compiling under
  scons until the step-4 cutover, when GL is deleted in one commit.
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

## Module layout

    moppe/gfx/math.hh        vectors + quaternions (de-boosted), Mat4 added
    moppe/render/            portable renderer API (no GL/Metal in headers)
      types.hh               handles, formats, draw-state structs, vertex layout
      draw.hh/.cc            DrawList: immediate-mode recorder + matrix stack
      mesh.hh                MeshBuilder (same recording API) → retained Mesh
      renderer.hh            Renderer: resources, passes, frame lifecycle
      text.hh/.cc            font atlas + text layout (uses platform rasterizer)
      solids.cc              cube/sphere/cone/torus generators (glutSolid* repl.)
    moppe/render/metal/      Metal backend (Objective-C++ .mm)
    moppe/shaders/moppe.metal   all MSL shaders, built into moppe.metallib
    moppe/platform/          platform API: app run loop, input, assets, speech
      platform.hh            Game interface, KeyCode enum, asset_path(), say()
      mac/                   AppKit + MTKView
      ios/                   UIKit + MTKView + touch controls
    moppe/game/              the game, split out of main.cc
      world.hh               WorldParams (ex-globals: map_size, water_level, …)
      game.cc                Game impl: setup, tick, render pass order, input
      hud.cc, vegetation.cc, stars.cc, dust.cc, blob_shadow.cc, fish.cc,
      wildlife.cc, city.cc, walker.cc, vehicle_render.cc, sky.cc, ocean.cc,
      terrain.cc, loading.cc
    moppe/map/               unchanged (already GL-free); boost → std
    moppe/mov/vehicle.*      simulation only; all GL removed

## Renderer API shape

Not a general RHI. It exposes exactly what the game needs; backends implement
this, not Vulkan-esque generality. Three tiers:

### 1. Retained resources

- `Texture` — 2D, formats: RGBA8, R32F, RG16S(norm), BGRA8 (render targets),
  Depth32F. Mip generation on request. Sampler state fixed per texture
  (repeat/clamp, filter, anisotropy, optional depth-compare).
- `Mesh` — immutable vertex (+ optional index) buffer with a list of
  `(DrawState, range)` runs. Built by `MeshBuilder`. Replaces display lists;
  used for vegetation/city sectors, sky dome, ocean grid, solid primitives.
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

`MeshBuilder` records through the same API but bakes to an immutable Mesh —
this is the display-list replacement, so Vegetation::generate / City::load_gl
port almost mechanically. State changes inside a bake (e.g. unlit lamp glow
spheres) become run boundaries.

### 3. Passes & frame graph

Fixed pass structure per frame, expressed as explicit API on `Renderer`:

    shadow pass (once per world)  → 4096² Depth16, terrain only
    scene pass  (MSAA 4x → resolve into sceneA, Depth32F reversed-Z)
       terrain → sky → city sectors → vegetation sectors → immediate world
       draw list (stars, wildlife, fish, vehicles, walker, people, cars,
       blob shadows) → river ribbons → ocean → dust
    post passes (ping-pong sceneA/sceneB as needed)
       underwater grade (when camera submerged)
       motion-blur ghosts: current += 3 zoomed alpha quads of prevFrame
       blit current → prevFrame (feedback persists across frames)
    present pass (MSAA → drawable): fullscreen quad of final scene + HUD
       draw list (2D ortho, y-down, top-left origin) + text

Conventions: reversed-Z (clear 0, GREATER_EQUAL; near 0.5, far 9000/30000
demands it), Metal [0,1] clip z, non-sRGB formats everywhere to preserve the
original gamma-space look. The sky shader forces depth to the far plane
(z = 0 under reversed-Z) and tests against cleared depth, so terrain still
occludes the expensive cloud shader.

River ribbons are retained `DrawList` meshes but use a dedicated translucent
scene pipeline.  Their UVs encode across-stream position and cumulative
downstream distance; packed vertex color carries rapid strength and a
logarithmic discharge signal, plus clustered fall-candidate strength.  The
shader derives moving ribs, restrained cascade foam, Fresnel response, and
sun glint from those readings.  It depth-tests
without writing depth and is drawn before the standing-water grid.  The mesh
is presentation-only: the ordered `RiverNetwork` and terrain remain the
authoritative routing and bed data.

Before baking, each ordered reach is resampled twice per receiver edge with a
cubic Hermite curve. Horizontal tangents are capped at one edge length, exact
source and downstream endpoints are retained, and dry height/normals are read
back from the terrain at every new point. Width, discharge, rapid, cascade,
and water-boundary height interpolate on the same curve. This is presentation
geometry only; it rounds D8 corners without moving a confluence, lake inlet,
or outlet away from its authoritative cell.

At a standing-water boundary the final cross-section widens and fades into the
standing surface. It follows one additional wet receiver only when that
receiver is D8-adjacent; the body-level graph is allowed to transfer directly
to a distant outlet and must not be interpreted as a drawable local segment.
Upstream reach endpoints also reduce opacity at a confluence so independently
blended strips do not all claim the shared receiver at full strength. A tested
fan overlay was discarded because translucent overlap made the join darker.

Feature-targeted visual checks use
`tools/capture-water /tmp/water.png FEATURE`, where `FEATURE` is `river`,
`confluence`, `mouth`, `waterfall`, or `lake`. The selector consumes the same
hydrology data, logs the chosen cell and score, uses a fixed clean camera, and
still runs the empty HUD pass because that pass performs the final post-chain
composite into the screenshot drawable.

## Terrain: vertex pulling (the big modernization)

Today: regenerate() builds ~8.4 M vertices of non-indexed strip soup (fine +
4x-decimated LOD, ~100 MB in 4 VBOs), rebuilt on every world change.

New: heights live in an R32F 2049² texture; per-vertex normals (the same ones
the CPU computes for physics) live in an RG16Snorm texture (y reconstructed).
The terrain vertex shader derives grid (x,z) from the index-buffer value and
a per-chunk origin, samples height, and computes world position — no vertex
buffers at all. Five shared index templates cover a bilinearly subdivided
257×257 near grid followed by native 129×129 and stride-2/4/8 grids. Each
finer level morphs onto the exact triangle surface of its parent before the
chunk changes LOD, avoiding pops and boundary cracks without skirts. Per
frame the CPU culls the same 128×128-cell chunks (distance + conservative
behind-camera tests) and issues at most one tiny indexed draw per visible
chunk. World regeneration = re-upload two textures.

Physics keeps its authoritative CPU copy (HeightMap::interpolated_height/
normal, ~10 samples/frame) — heights and rendered normals come from the same
arrays, so nothing can diverge. City generation still bakes into the CPU
heightmap and just re-uploads.

The splat/shadow/haze fragment shader ports 1:1 from shaders/test.frag with
explicit uniforms (sun dir, fog color/scale, heightScale, seaLevel, texScale,
light matrix, shadow strength) instead of gl_* built-ins. Lighting moves to
world space (the eye-space detour existed only because fixed-function GL
transformed lights by the modelview).

The pointwise terrain algebra now lowers to a Metal 4 function-stitching graph
and runs in a compute kernel.  Terrain Lab currently reads the result back to
the authoritative CPU heightmap before normalization, erosion, and texture
upload.  Interactive previews derive normals from the height texture, reuse
terrain GPU resources, and morph old and new height textures over 120 ms.
They restore exact CPU normals when leaving the lab.
The next renderer boundary is to keep pointwise results GPU-resident through
global normalization and rendering.  Hydraulic erosion remains a separate
iterative problem rather than part of the pointwise graph.

## Other shader ports

- sky.frag (228 lines, fully procedural) → MSL 1:1; uniforms time, sunHeight,
  cloudiness, sunDir, fogColor.
- ocean.vert/frag → MSL on a regular grid mesh; the vertex stage samples the
  priority-flood water surface for ocean and inland lake elevation, while
  waves fade at shore and dry fragments are discarded.
- underwater.vert/frag → fullscreen-triangle post pass.
- Immediate/baked geometry uses one "uber" forward shader: Lambert + modest
  Blinn specular for lit runs, plus the terrain's exact haze formula (fog was
  previously fixed-function GL_EXP2 for vegetation only and absent for other
  props — unifying on the terrain curve makes the world consistent, a
  deliberate small visual improvement).
- HUD uses a separate 2D pipeline (no lighting/fog, y-down ortho, scissor
  none, blend on).

## Shadow map

The gameplay technique is one ortho depth render of the terrain from the fixed
sun, 4096² Depth16, hardware PCF via compare sampler, and a 5-tap weighted
kernel with slope-scaled bias in the terrain shader. The Terrain Lab uses an
explicit preview quality: 1024² at source stride 2. Lab shadow maps are
double-buffered and their visibility results crossfade on the same 120 ms
clock as heightfield transitions, so a lowering mountain never receives the
stale shadow of its old geometry. `MOPPE_PROFILE_SHADOW=1` prints the GPU time
for the pass.

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

## Game-code refactor

- main.cc splits into moppe/game/ one file per system; the file-scope mutable
  globals (map_size, water_level, fog_scale, fog color, mode flags) become a
  `WorldParams` + per-frame `FrameEnv` (fog color, sun dir, time, camera)
  passed explicitly.
- Vehicle loses its render()/set_camera()/draw_debug_text() — simulation
  stays put; a game/vehicle_render.cc draws bikes/cars from the public pose
  API via DrawList. Same for Walker.
- Vegetation/City keep their generate() logic but record into MeshBuilder
  (and retain their plant/prop data instead of discarding it after baking).
- Dead code goes: render_vertex_arrays, TerrainRenderer::render_directly,
  gl::FrameBufferObject, InterpolatingHeightMap, Camera (the unused one),
  sky texture fallback, MouseCameraController (wired but its output is never
  read), draw_debug_text trio, GLUT text, GLEW/GLUT/boost vendoring, enet.
- boost → std: mt19937/<random> (terrain-per-seed changes; acceptable — the
  default seed is time(0)), shared_ptr, multi_array → small Array2D,
  operators.hpp → hand-written operators, format → snprintf.

## Build

CMake (replaces SCons):

- `moppe` — macOS .app bundle (also runs from the build tree; assets copied
  next to the binary), links Metal/MetalKit/AppKit/AVFoundation.
- `moppe-ios` — iOS app bundle, same sources, UIKit platform layer; built
  with the iOS toolchain (simulator target for CI/verification).
- `map-test` — the existing heightmap CLI tool, unchanged.
- .metal → .metallib via xcrun at build time; metallib + textures + data
  ship as bundle resources; `asset_path()` resolves per platform.

## Porting order (each step verified by running the game)

1. Foundation: Mat4, de-boosted math, render API headers, Metal device/
   swapchain/uber pipeline/streaming buffers, mac platform shell, CMake —
   proven with a spinning test scene + HUD text.
2. Terrain (height-texture pipeline + shadow pass), sky, ocean.
3. Game systems, one agent-portable module at a time: vehicle split, walker,
   dust, blob shadow, stars, fish, wildlife, vegetation, city, HUD, post
   effects, game glue/input.
4. Parity check against the GL build (still building from scons until
   cutover), then delete GL path + scons + vendor, update CLAUDE.md.
5. iOS target: platform/ios, touch controls, loading screen, simulator build.

## Non-goals (for now)

- GPU terrain generation & erosion (noted above).
- Networking (enet was never wired up).
- Changing the art direction, physics feel, or game rules.
