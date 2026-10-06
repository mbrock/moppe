# NHAL: the next renderer's hardware layer

NHAL is a small C++ hardware layer for moppe's renderer, in the spirit
of Luv's WebGPU-shaped HAL but shaped by what the targets actually offer. It
has three backends: Metal 4 on Apple platforms, Direct3D 12 for Xbox Series
consoles in Developer Mode (UWP, feature level 11_0, shader model 6.4), and
Vulkan 1.3 on Linux. It lives in `moppe/nhal/`, and its renderer is the
game's only one: the Metal and WebGPU renderers it grew beside are retired.

Shaders are written in Luv's mathematical shader language and lowered ahead of
time by `luv-shaderc` (a package of the Luv flake) into MSL, HLSL, and SPIR-V
(through Luv's own Vulkan lowering), plus the reflection NHAL builds its
pipelines from.

## The renderer

`moppe/nhal/renderer/` implements the game's `render::Renderer` over NHAL
on every platform. Its programs live in one Lisp file, `shaders/world.lisp`,
which CMake lowers into `build/nhal-world/` (`moppe_lower_world_shaders`)
and embeds in each backend's form (`world_shaders_metal.cc`,
`world_shaders_vulkan.cc`, by `#embed`; `world_shaders_d3d12.cc`, from
DXC's headers). A frame draws the scene into a
half-size RGBA16F colour, RG16F motion, and reversed-Z depth, jittered;
resolves it temporally into a drawable-size history; then tonemaps into the
drawable and draws the HUD over it.

So far it draws the terrain (vertex-pulled chunk strips morphing onto their
parent levels, the ground material ported from `terrain.metal`), a
provisional sky, the game's draw lists and meshes through one "uber" program
with a pipeline per draw state, and the HUD: its draw lists, then its Slug
text and vector shapes, whose band walk is Luv's own (`luv/slug-shader`,
called as `luv.slug::slug-horizontal-band-step` and friends).

Water is a placeholder until it is designed properly, rather than a port
of the old renderer's: `draw_ocean` draws the terrain's chunks again with
the `water` program, each vertex lifted to the water sheet (RG32F on the
terrain grid: the surface and the swell's amplitude), dry fragments
discarded, and the rest a depth tint under the sky's Fresnel reflection
with a sun glint from two drifting ripple fields, over the opaque scene.
Waterfalls are not drawn.

The trunk forest keeps `forest_trunks.metal`'s trees -- tapered trunks,
stacked spruce cones, birch clumps on branches, the same twelve (species,
tier) topologies -- but the GPU chooses them: `forest-cull` tests every
individual against the view in a compute pass at `begin_frame`, appends it
to its class's candidate list with an atomic, and counts the class's indexed
indirect draw, which `draw_forest` issues twelve times. There is no stand
canopy yet, so closed stands stay individuals until their crowns are four
pixels across, and the stand closure is approximated by the habitat's canopy
cover.

The sun's shadow is a 2048-texel depth map over the 160 metres around the
rider, rendered each frame by `render_local_shadow`: terrain chunks at
native detail, then the trees, culled for the sun by the same compute
program into two classes (each species at its coarsest tier, with a
smaller, porous crown). The tree geometry is one Luv function returning a
struct (`tree-vertex-at`), shared by the scene and the shadow programs as
`tree_vertex` is in Metal. Terrain samples it with five comparison taps,
trees with four.

Grass is undergrowth.metal's blades without its mesh stages. A window of
0.6-metre tiles anchored to the world lattice surrounds the camera; at
`begin_frame`, `grass-tiles` keeps those in view that the grass medium says
carry blades (leaf area, priced by the blades' projected width, thinned by a
per-cell phase) and counts each into one indexed indirect draw. Each tile is
an instance of 32 four-section blades that the vertex stage grows from hashes
and the terrain's own fields, with the medium's tint, wind, the mover's
parting, and the fragment stage's transmission, glint, and ensemble limits.
The cull uses the previous frame's `UndergrowthParams`, since compute may not
interrupt the scene pass. Flower drifts are a shoot family of the same
draw: an 11-metre warped lattice of single-species colonies (daisy,
buttercup, harebell, campion) claims shoots in open, damp sward, whose last
two sections become a petal head that widens and washes toward its drift's
colour as it retires.

The mesoscale sward canopy is undergrowth.metal's middle rung: `sward-patches`
keeps the 16-metre patches in view out to 180-1200 metres (the reach follows
the sward's projected height), each an instance of a 4x4 grid lifted to the
sward's height, drawn translucent before the blades. Its fragments march
four samples down through the canopy column and integrate the leaf-normal
distribution's radiance front to back, with the ground texture's grain, so
fields read as grass to the horizon after single blades have retired.
Ferns are not ported yet. The ground under the grass is the terrain's turf
palette, as in Metal.

Bloom is post.metal's: a bright pass at a quarter of the drawable, a
separable nine-tap Gaussian, added before the tonemap.

Boulders are boulders.metal's cleaved icosahedra, culled on the GPU into
coarse and fine classes. Ambient occlusion and sun shafts (post.metal's)
render after the temporal resolve into their own targets, which the
present pass multiplies and adds, so the history stays clean; the present
also draws Metal's veil around the sun. The stand's rasters -- canopy
closure splatted from the crowns, and the game's fallen leaves -- shade
the forest floor and thin and dry the grass beneath them. Trunks root into
the soil with flared buttresses.

Weather (`MOPPE_WEATHER`, `game/weather.hh`) is authored: mist and drizzle
grey and lower the sky, dim the sun, and raise the sky's light; the
renderer lays a valley mist into its haze (below about two fifths of the
land's relief, thickening with distance) and draws a drizzle of
world-anchored streaks around the camera.

Falling leaves are leaf_fall.metal's: a window of world-anchored
1.2-metre cells around the camera, one leaf each, whose fall, drift, and
tumble are a function of the cell and the time, wherever the fallen-leaf
raster says gold crowns stand (the `leaves` program, 64 x 64 instances).

Dust and the old underwater, motion-blur, and scene-blur passes are not
drawn yet.

## The game on Xbox

`nix build .#moppe-xbox` builds the whole game for the console with the NHAL
renderer on Direct3D 12 (`moppe/platform/uwp/game.nix`, CMake's
`MOPPE_XBOX_GAME`): luv-shaderc lowers `world.lisp` to HLSL, DXC compiles each
stage to a DXIL header, and `world_shaders_d3d12.cc` gathers them.
The host is the desktop's SDL host, on nixbox's SDL3 with its C++/WinRT
UWP backend (`xbox.pkgsXbox.SDL3`): SDL's entry point runs the
CoreApplication and calls `moppe/platform/uwp/main_uwp.cc`'s main, the
console's prelude, which calls `main.cc`'s main (compiled as `moppe_main`).
`device_d3d12.cc` takes the window's CoreWindow and makes a 3840x2160
swapchain, two pixels per HUD point; `platform_uwp.cc` supplies the
package's assets, LocalCache, and background work.
`nix run .#deploy-moppe-xbox` installs and launches it under the console
lease. `LocalState/log.txt` holds everything the game logs, and
`environment.txt` there (or in the package) sets `moppe::environment`
variables, `MOPPE_ARGS` being the command line. The prelude turns on the
pass timings and the frame-rate report (`MOPPE_FPS_REPORT`), points the
remote-control file at `LocalState/control.txt`, and watches for stalled
frames.

The console does not run the geology simulator if it need not:
`tools/deploy-xbox [DEPLOY ARGS]` (or `make xbox`) bakes the default world
on the Mac -- the same 2048-sample Play world, seed 123, that the Mac plays
-- and ships it in the package at `world/default`, where the game finds it
(`bundled_world_cache` in `world_loading.cc`) and starts in it. On the
Series X the bundled world reads in 6.5 s and the game is ready to play
7.3 s after loading begins; generating the same world there took about
five minutes, and every reinstall threw it away with LocalCache.
`tools/bake-world [RESOLUTION [PROFILE [SEED]]]` does the baking: it builds
the native `terrain-cache-bake` in `build-bake/`, runs it (about 100 s on
an M5), and keeps the result in `~/Library/Caches/Moppe/baked/` under the
baker's hash, so the world is baked again only when the terrain code that
made it changes. The finished-world cache is portable data -- fixed-width
little-endian scalars, and Arrow bundles whose schema names match on both
compilers -- so the console loads the Mac's world as-is. The package grows
to 274 MB (557 MB installed), and a whole deploy takes under a minute.

The bake reaches the sandboxed Nix build through `MOPPE_XBOX_BAKED_WORLD`,
which `flake.nix` reads only under `--impure`: the script runs
`MOPPE_XBOX_BAKED_WORLD=<dir> nix run --impure .#deploy-moppe-xbox`, and
setting the variable yourself ships another bake. A plain, pure
`nix run .#deploy-moppe-xbox` ships no world, and the console generates a
1024-sample one (seed 123) at first launch, about a minute, cached in
LocalCache until the next reinstall.

`tools/xbox-control` drives the running game from the Mac, with the lease
the last deploy took: `send 'tap Space' 'wait 1' 'tap F' 'stick 0 1 1'`
uploads `LocalState/control.txt`, whose commands the host plays as a
timeline (`tap`, `hold`, `down`, `up` with Mac key names, `stick STEER
DRIVE BOOST` or `stick off`, `look DX DY`, `wait SECONDS`) and logs as they
run; `shot` saves the console's screen and `log` tails its log. Space skips
the opening cinematic, F mounts the bike.

The log reports the frame rate every ten seconds and, since the host turns
`MOPPE_NHAL_TIMINGS` on, each pass's GPU time. On the Series X at 3840x2160
(the scene at 1920x1080) a frame in the valley costs about 4 ms: scene 2.0,
sun shadow 0.6, temporal resolve 0.7, present 0.3, both tree culls 0.17, at
a steady 60 fps. The same view on an M5 MacBook at 2498x1600 costs 12-13 ms.

## The desktop host

macOS and Linux run the same host, `moppe/platform/sdl/`: an SDL3 window,
sized in points, whose pixels are the drawable, with the keyboard, the
captured mouse, and the first gamepad driving the game (`host.cc`), and the
platform services (`services.cc`). The device is the only per-platform part
(`sdl.hh`): `device_metal.mm` makes a Metal 4 device on the layer of an SDL
Metal view, in extended linear sRGB for EDR, and `device_vulkan.cc` a Vulkan
device on the window's surface. Assets come from `MOPPE_ASSETS`, then the
app bundle's resources or the executable's folder, then `share/moppe` beside
it, then the source tree; caches live in `~/Library/Caches/Moppe` on the Mac
and `$XDG_CACHE_HOME/moppe` on Linux. Where SDL3 is not installed, CMake
builds its release. The Xbox runs the same host (below).

The gamepad's mapping onto the game and the remote-control file are shared
by every host (`moppe/platform/input.hh`): `MOPPE_CONTROL_FILE` names a file
whose `seq N` header and timeline of `tap`, `hold`, `stick`, `look`, `wait`,
and `env` commands drive the game, as `tools/xbox-control` does the
console's.

## The game on Linux

`nix develop` gives a shell with Clang, CMake, Ninja, SDL3, the Vulkan
loader, headers, and validation layers, and `luv-shaderc`
(`moppe/platform/sdl/shell.nix`). The usual configure and
`cmake --build build --target moppe` then build `build/moppe`: the whole
game, drawn by the NHAL renderer through `moppe/nhal/vulkan/`, with
luv-shaderc's SPIR-V embedded, in the desktop host above.

`cmake --build build --target nhal-demo` builds the demo for Vulkan;
`./build/nhal-demo --capture /tmp/nhal.tga --frames 30` renders without a
window and writes the last frame. `MOPPE_VULKAN_VALIDATION=1` turns on the
Khronos validation layer, `MOPPE_VULKAN_DEVICE=N` picks a physical device,
and `MOPPE_VULKAN_PRESENT=mailbox|immediate` replaces FIFO presentation.

The Vulkan device needs 1.3's dynamic rendering and synchronization2,
timeline semaphores, push descriptors, and robustness2's null descriptors.
Each program's resources form one push-descriptor set (luv-shaderc folds
the families into set 0, below), its samplers immutable, and every draw
pushes its bindings; unbound slots are null. Image layouts are tracked per
texture, and a full memory barrier precedes every render pass, dispatch,
and copy, carrying the layout changes the command needs. A pass's sampled
textures are known only as its draws arrive, so each render pass records
into a secondary command buffer that the frame's primary executes after
the pass's barriers. A timeline semaphore counts frames.

## What the Xbox allows

Measured by nixbox's `probes/d3d12-caps` and `probes/swapchain`:

- Shader model 6.4, compiled by DXC on the build host (it signs DXIL). No 6.5
  or 6.6, so no `ResourceDescriptorHeap` indexing and no buffer pointers in
  shaders. No mesh shaders, ray tracing, enhanced barriers, or 16-bit math.
- Resource binding tier 3: root descriptors by GPU address, unbounded SRV
  tables, a 1,000,000-descriptor shader-visible heap, `ExecuteIndirect` with
  root constants. Waves are 64 lanes.
- Every scene format renders, blends, multisamples, and resolves.
- A 3840x2160 swapchain for the 1080p CoreWindow reaches the TV natively;
  HDR10 and scRGB swapchains are accepted.

## Sharing the Xbox

The console is shared with other agents. Deploying through nixbox takes the
console lease that its Device Portal proxy grants (`tools/xbox-proxy` in
nixbox): `nix run .#deploy-nhal-xbox` waits its turn, installs, and holds the
console for `--hold` seconds (default 300) so the demo can be watched;
`nix run github:mbrock/nixbox#xbox-lease -- release` gives it back sooner and
`-- status` shows who has it.

## The API

`moppe/nhal/nhal.hh` is the whole surface. Resources are handles into the
device's tables; destroying one retires it once the frames that might use
it have completed. Between `begin_frame` and `end_frame` the device records
one command stream:

- render passes (colour attachments with load, store, clear, and MSAA
  resolve; a depth attachment) and compute passes, each labelled;
- pipelines made from a program's reflection plus each backend's code (MSL
  source compiled at pipeline creation, DXIL compiled ahead of time);
- bindings by binding number, in the families below: buffers by GPU
  address (device buffers or slices of the frame's upload arena), textures,
  and storage textures;
- draws and indexed draws, direct or indirect, and dispatches, direct or
  indirect, with arguments a compute pass may have written;
- `copy_to_buffer` from the arena, `capture_frame` for readback, and
  `pass_timings`, the GPU time of each labelled pass of the last completed
  frame.

Barriers are the device's: Metal 4 orders passes with queue-stage barriers
and dispatches with encoder barriers; Direct3D 12 tracks texture states,
and each buffer's state within the frame (buffers decay to COMMON between
command lists), transitioning where a binding, an index buffer, or an
indirect argument needs it, with UAV barriers between writes.

The demo (`moppe/nhal/demo`) exercises all of it: a compute pass sways
9000 spruces, culls their bounding spheres to the view frustum, and appends
the visible ones through an atomic into the camera's indirect draw (the
host zeroes its instance count first); a depth-only pass casts every tree's
sun shadow from a second record; the scene draws terrain, trees, and sky in
RGBA16F with reversed-Z; and the tonemap writes the drawable.

The scene has two modes (U on the Mac, X on a controller):

- **Temporal upscaling**, the default: the scene renders at half the
  drawable's size per axis with a 16-step Halton jitter, single-sampled, and
  writes each pixel's screen motion (RG16F, unjittered, last frame's place
  minus this frame's, in texture coordinates): the terrain by the camera's
  movement, the trees by the camera's and the wind's (the wind pass keeps
  last frame's sway too), the sky by the camera's turning. A resolve pass at
  the drawable's size follows the motion of the nearest surface in each
  pixel's 3x3 neighbourhood back into the history, fetches it through a
  five-tap Catmull-Rom filter, clamps it to the neighbourhood in YCoCg, and
  blends in a tenth of the new sample, more as the pixel moves faster (as
  Luft's resolve does). It is portable Luv code, so the Xbox, which has no
  MetalFX, gets it too. It is softer than 4x MSAA.
- **Native**: the scene at the drawable's size with 4x MSAA, resolved.

GPU time per pass, measured by NHAL:

| Pass | Xbox Series X, 3840x2160 | Apple M5, 2560x1440 |
| --- | --- | --- |
| wind and culling (compute) | 0.01 ms | 0.03 ms |
| sun shadow, 4096x4096 | 0.60 ms | 1.9-3.7 ms |
| scene, temporal (half size, with motion) | 0.75 ms | 1.0-2.4 ms |
| temporal resolve | 0.70 ms | 0.9-3.8 ms |
| scene, native 4x MSAA | 6.8 ms | 3.1-3.4 ms |
| tonemap | 0.30 ms | 0.3-0.5 ms |

At 4K with 4x MSAA the Xbox's scene is bound by fill, which culling barely
helps and upscaling cuts from 6.8 ms to about 1.5 ms with its resolve; the
M5's timings vary with its clocks.

## The binding contract

A program is a set of stages (vertex + fragment, or compute) whose resources
are linked by name. Every resource has a kind and a binding number. Binding
numbers are per kind family, and the families map onto each backend like this:

| Luv resource | Family | Metal 4 | Direct3D 12 HLSL | D3D12 root signature | Vulkan (set 0) |
| --- | --- | --- | --- | --- | --- |
| `:uniform-block` | buffer | `constant B& n [[buffer(i)]]` | `cbuffer n : register(b i)` | root CBV | uniform buffer at `i` |
| `:storage-buffer` (read) | buffer | `device const T* n [[buffer(i)]]` | `StructuredBuffer<T> n : register(t i, space0)` | root SRV | storage buffer at `i` |
| `:storage-buffer :access :read-write` | buffer | `device T* n [[buffer(i)]]` | `RWStructuredBuffer<T> n : register(u i)` | root UAV | storage buffer at `i` |
| `:texture-2d`, `:depth-texture-2d`, `:uint-texture-2d` | texture | `texture2d<…> n [[texture(i)]]` | `Texture2D<…> n : register(t i, space1)` | one descriptor table, t0–t15 space1 | sampled image at `16 + i` |
| `:texture-2d-array`, `:depth-texture-2d-array`, `:texture-cube`, `:texture-3d` | texture | `texture2d_array<…>`, `depth2d_array<float>`, `texturecube<…>`, `texture3d<…>` at `[[texture(i)]]` | `Texture2DArray`, `TextureCube`, `Texture3D` at `register(t i, space1)` | same table | sampled image at `16 + i` |
| `:read-write-texture-2d` | storage texture | `texture2d<…, access::read_write> n [[texture(16 + i)]]` | `RWTexture2D<…> n : register(u i, space1)` | a UAV table, u0–u15 space1 | storage image at `32 + i` |
| `:sampler` | sampler | `sampler n [[sampler(i)]]` | `SamplerState` or `SamplerComparisonState n : register(s i)` | static samplers | immutable sampler at `48 + i` |

- Buffer binding numbers are unique across uniform blocks and storage buffers
  (Metal shares one buffer index space); 0–15.
- Texture binding numbers are 0–15, and so are storage texture binding
  numbers, which are a separate family. NHAL copies each draw's textures into a
  contiguous range of the shader-visible heap; true bindless (an unbounded
  table indexed by the shader) is a later, separate resource kind.
- Samplers are a fixed standard set, chosen by binding number. A sampler used
  with `sample-compare` is a comparison sampler in HLSL.

  | Binding | Sampler |
  | --- | --- |
  | 0 | linear filtering, clamp to edge |
  | 1 | linear filtering, repeat |
  | 2 | nearest, clamp to edge |
  | 3 | linear comparison (`less-equal`), clamp to edge |

- The descriptor `:set` is 0 or absent.
- Uniform blocks hold `vec4` lanes at 16-byte offsets and `mat4` members as
  four consecutive column lanes (`std::array<float, 16>` in the header,
  element `[4c + r]`; HLSL declares them `row_major` and multiplies with the
  operands swapped, so the meaning is column-major everywhere). The C++ struct,
  MSL `constant`, and HLSL `cbuffer` layouts agree without packing rules.
- Storage buffer elements are 1-, 2-, or 4-component scalars or vectors,
  `mat4`, or a structure declared with `define-shader-struct` whose fields
  follow the same rule and start aligned (32-bit scalars at 4, 2-vectors at 8,
  4-vectors and `mat4` at 16) with a size that is a multiple of its alignment.
  No padding is implied, so SPIR-V std430, MSL device memory, HLSL
  `StructuredBuffer`, and C++ agree; the header emits each such structure with
  `static_assert`s on its size and every field's offset.
- There are no vertex buffers or input layouts: vertex stages pull from
  storage buffers with the `:vertex-index` and `:instance-index` built-ins.
  Index buffers are allowed.


Inter-stage values: a vertex output or fragment input at `:location n` is HLSL
semantic `LOCATIONn` and MSL `[[user(locnN)]]`; the `:position` built-in is
`SV_Position`; fragment output `:location n` is `SV_Target n`. The HLSL
fragment input signature is always `SV_Position` followed by every vertex
output, since Direct3D matches stages by layout.

Clip space: shaders write `:position` in the language's convention, y down
(Vulkan's), and the MSL and HLSL lowerings negate y, so what reaches Metal
and Direct3D is their y up with depth 0..1; SPIR-V keeps it as written, under
an ordinary viewport. Renderers use reversed-Z.

In fragment stages `:frag-coord` is pixels from the top-left at pixel
centres, depth in z, and 1/clip-w in w on every backend (HLSL rebuilds w from
`SV_Position`). Which winding is front, for `:front-facing` and culling, is
pipeline state (`front_counter_clockwise`).

Storage textures declare a format (rgba32f, rgba16f, rgba8, r32f, r32ui),
which the JSON records. Reading rgba16f or rgba8 storage textures needs typed
UAV loads beyond the base formats on Direct3D 12: the Xbox reports the
additional-formats option, though its per-format query lists typed loads
only for R32F and R32 uint, so prefer r32f/r32ui for read-modify-write. The
storage texture table is visible to every stage.

## What `luv-shaderc` produces

`nix run .#luv-shaderc -- --out DIR [--target msl|hlsl|spirv]... FILE.lisp`
(this repository's flake pins the Luv version). Source files are plain
`define-shader`, `define-shader-function`, and
`(define-shader-program NAME :vertex V :fragment F)` (or `:compute C`) forms
in the `luv.shader-user` package. Entry points are `NAME_STAGE`; file names
and namespaces are the program name in snake_case. For each program, into the
output directory:

- `NAME.STAGE.metal`, one MSL document per stage.
- `NAME.STAGE.hlsl`, one HLSL document per stage, compiled by DXC with
  `-T vs_6_0` / `ps_6_0` / `cs_6_0` (or higher, up to 6.4) and the entry point
  named in the manifest.
- `NAME.STAGE.spv`, one SPIR-V module per stage, from Luv's Vulkan lowering,
  its entry point named as in the other languages and every resource in
  set 0 at its family's base plus its binding (the table above).
- `NAME.json`, the reflection manifest: stages and entry points, every
  resource (name, kind, binding, stages, byte size, uniform members and
  offsets, storage element type), and the fragment outputs.
- `NAME.hh`, the same reflection as C++ for NHAL, and one struct per uniform
  block whose members are `std::array<float, 4>` lanes named after the
  shader's members, with a `static_assert` on its size:

```cpp
// Generated by luv-shaderc from terrain.lisp; do not edit.
#pragma once
#include <moppe/nhal/reflection.hh>
#include <array>

namespace moppe::nhal::shaders::terrain {
  struct FrameState {
    std::array<float, 4> camera;
    std::array<float, 4> sun_direction;
  };
  static_assert(sizeof(FrameState) == 32);

  inline constexpr Resource resources[] = {
    {"frame_state", ResourceKind::uniform_block, 0,
     stage_vertex | stage_fragment, sizeof(FrameState)},
    {"heights", ResourceKind::storage_buffer, 1, stage_vertex, 0},
    {"albedo", ResourceKind::texture_2d, 0, stage_fragment, 0},
    {"linear_clamp", ResourceKind::sampler, 0, stage_fragment, 0},
  };
  inline constexpr Program program {
    .name = "terrain",
    .vertex_entry = "terrain_vertex",
    .fragment_entry = "terrain_fragment",
    .compute_entry = nullptr,
    .resources = resources,
    .color_outputs = 1,
  };
}
```

`moppe/nhal/reflection.hh` defines `Resource`, `ResourceKind`, the stage bits,
and `Program`; it is the C++ half of this contract.
