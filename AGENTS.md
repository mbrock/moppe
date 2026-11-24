# Moppe Development Guidelines

## Build Commands
- Configure: `cmake -B build -G Ninja -DCMAKE_BUILD_TYPE=RelWithDebInfo`
  (without a build type the project defaults to Debug, whose world
  generation is many times slower)
- Build everything: `cmake --build build`. The engine is a library built
  once (`moppe-engine`, with the generated models apart in `moppe-models`),
  so editing a source rebuilds its unity batch and relinks (about 9 s on 16
  cores; `-DMOPPE_UNITY_BUILD=OFF` makes it 5 s and a full build twice the
  CPU). There is no precompiled header: Emscripten's and MinGW's clang break
  mp-units' units when they load one.
- Unit tests: `cmake --build build --target moppe-tests && ctest --test-dir
  build --output-on-failure` (the test binary is excluded from the default
  build, so plain `ctest` otherwise runs a stale one)
- Run the game: `./build/moppe.app/Contents/MacOS/moppe`
  (or `open build/moppe.app`) on macOS, `./build/moppe` on Linux. Both are
  the same SDL3 host (`moppe/platform/sdl/`) drawing through the NHAL
  renderer, on Metal 4 or Vulkan; on Linux, run cmake inside `nix develop`.
- Metal shader costs on the M5: [capture, Xcode replay/export, and CLI
  analysis](docs/metal-shader-profiling.md) with `tools/metal-profile`.
- Renderer smoke test: `./build/moppe-testbed` (configure with
  `-DMOPPE_BUILD_DEVELOPER_TOOLS=ON`)
- What the game does and how to stage each feature for a capture or a
  recording (`tools/record`, `MOPPE_WALK`, `MOPPE_DEMO`, ...) is in
  docs/features.md; read the part you are working on.
- NHAL, the renderer's hardware layer (Metal 4, Direct3D 12 for Xbox,
  Vulkan on Linux, WebGPU in the browser; `docs/nhal.md`): `cmake --build
  build --target nhal-demo`, then `./build/nhal-demo`, or
  `./build/nhal-demo --capture /tmp/nhal.png --frames 30` to write one frame
  without taking focus. On Xbox: `nix build .#nhal-xbox` and
  `UWP_DEVICE_URL=https://xbox.whale-justice.ts.net nix run .#deploy-nhal-xbox`;
  the app writes `nhal.txt` (and one captured `nhal.tga`) to its LocalState,
  readable through Device Portal's file API.
- Linux (NHAL on Vulkan, in an SDL3 window): `nix develop`, then the usual
  configure and `cmake --build build --target moppe`, and `./build/moppe`
  (the same controls as the Mac). `./build/nhal-demo --capture
  /tmp/nhal.tga --frames 30` renders the demo without a window.
  `MOPPE_VULKAN_VALIDATION=1` turns on the Khronos validation layer. The
  device is `moppe/nhal/vulkan/`; see docs/nhal.md. With no GPU or display
  (an Amp orb, set up by `.agents/setup`), Mesa's lavapipe
  (`VK_DRIVER_FILES`) and SDL's offscreen video
  (`SDL_VIDEO_DRIVER=wayland,x11,offscreen`) run the real game headless on
  the CPU: `--screenshot`, captures, and `tools/record` all work, slowly.
- Browser (NHAL on WebGPU, the SDL host built by Emscripten; docs/web.md):
  `make web-serve` builds `build-web/moppe.html` and serves it at
  <http://localhost:8080> (`make web` only builds; `brew install emscripten
  bun`). The page's address carries the command line and switches:
  `?MOPPE_FPS_REPORT=1&MOPPE_NHAL_TIMINGS=1&args=--graphics-quality+low`,
  reporting to the browser's console. The device is `moppe/nhal/webgpu/`,
  the page `moppe/platform/web/`. The page has one thread, so it starts
  from baked land and never generates by default. On a phone the page
  draws touch controls (`touch.js`: a stick under the left thumb, look
  under the right, and a button for each thing the game says can be done
  just now, `platform::offer_actions`). WebGPU validates what the
  other devices let pass, so a renderer change is worth a look at the
  console there. Every push to master is published at
  <https://moppe.swa.sh> (a timer on swa running `tools/publish-web`) and
  on GitHub Pages (`.github/workflows/pages.yml`).
- CPU reference world generation is bit-identical across platforms (docs/determinism.md):
  the project builds with `-ffp-contract=off`, generation code calls
  `moppe::cr::` (correctly rounded CORE-MATH) instead of `std::sin`,
  `std::pow`, `std::hypot` and the like, and `Vec3`'s reductions are spelled
  out. `world-fingerprint` (a developer tool) hashes every stage so two
  machines can be diffed line by line. Local Metal evolution is validated by
  geomorphology and material balance. Use CPU bakes for shared assets.
- Linux package and Steam Deck: `nix build .#moppe` (x86_64-linux) builds
  the game with its own Mesa Vulkan driver for hosts without NixOS's;
  docs/steam-deck.md installs it on a Deck (`nix profile install
  github:mbrock/moppe#moppe`, a Steam shortcut) and tests it Deck-like on
  chapel (an RDNA2 GPU) under headless gamescope at 1280x800.
- Windows: `nix build .#moppe-windows` (x86_64-linux) cross-builds the
  desktop game (`-DMOPPE_WINDOWS_DESKTOP=ON`: the SDL host on Vulkan,
  through volk) with Zig's Clang for MinGW-w64 into
  `result/moppe-windows-x86_64.zip`, a `moppe.exe` importing only
  Windows' own DLLs with its assets and land built in
  (`moppe/platform/sdl/windows.nix`). In an orb it runs under Wine
  (`wineWow64Packages.stable`, Xvfb, `WINEDLLOVERRIDES="mscoree,mshtml="`)
  on lavapipe.
- Releases (`.github/workflows/release.yml`): pushing a `v*` tag builds
  the Linux tarball (`moppe-steamdeck`), the Windows zip and its lone
  `moppe.exe`, and the macOS app (Developer ID signed, notarized, and carrying
  Sparkle updates; docs/mac-releases.md) and publishes them as a GitHub release;
  running the workflow
  by hand keeps them as artifacts. Each executable carries the assets and
  the default land (`-DMOPPE_EMBED_ASSETS=ON` with `MOPPE_LANDS_DIR`;
  `tools/pack-assets` compresses them, and the SDL host's `asset_path`
  unpacks one into the cache's `assets/` folder when the disk has none).
- The game's core on Xbox, before NHAL renders it: `nix build
  .#moppe-core-xbox` and `UWP_DEVICE_URL=https://xbox.whale-justice.ts.net
  nix run .#deploy-moppe-core-xbox -- --hold 600` (package
  `Moppe.core-xbox`) build the whole game except a renderer for x86_64 UWP
  from CMakeLists.txt (`moppe/platform/uwp/`). It runs the core probe
  (`moppe/game/core_probe.hh`): generates the default world from scratch,
  reloads it from the world cache, walks and rides it, then drives
  `game.cc` against `tests/recording_renderer.hh`, writing
  `LocalState/report.txt` (phase timings, peak memory) and `log.txt`; the
  screen turns green on a pass. Read them with `curl
  "$UWP_DEVICE_URL/api/filesystem/apps/file?knownfolderid=LocalAppData&packagefullname=<PFN>&path=%5C%5CLocalState&filename=report.txt"`.
  `LocalState/environment.txt` (`NAME=VALUE` lines) stands in for the
  environment UWP lacks; game code reads switches through
  `moppe::environment` (`moppe/environment.hh`), never `getenv`. For
  comparison, `cmake --build build --target moppe-core-probe && ./build/moppe-core-probe`
  runs the same probe on macOS. Incremental Xbox builds: `nix develop
  .#moppe-core-xbox`, then `cmake -B build-xbox -G Ninja
  -DBUILD_TESTING=OFF` and `cmake --build build-xbox`.
- iOS (simulator): `cmake -B build-ios -G Xcode -DCMAKE_SYSTEM_NAME=iOS
  -DCMAKE_OSX_SYSROOT=iphonesimulator` then build the `moppe-ios` target
  with `CODE_SIGNING_ALLOWED=NO`
- tvOS (simulator): `cmake -B build-tvos -G Xcode -DCMAKE_SYSTEM_NAME=tvOS
  -DCMAKE_OSX_SYSROOT=appletvsimulator` then build the `moppe-tvos` target
  with `CODE_SIGNING_ALLOWED=NO`
- Xbox Series (Developer Mode, via nixbox): `nix run .#deploy-moppe-xbox`
  builds the game with the NHAL renderer on Direct3D 12, installs it under
  the shared console lease, and launches it (see docs/nhal.md);
  `nix run .#deploy-moppe-core-xbox` runs the headless core probe.
  `tools/deploy-xbox [DEPLOY ARGS]` (or `make xbox`) is the same deploy
  with the console's 2048-sample world baked on the Mac (`tools/bake-world`, kept
  in `~/Library/Caches/Moppe/baked/` until the terrain code changes) in
  the package, so the console starts in seconds instead of generating
  for minutes; the pure `nix run` ships none and generates a 1024 world.
  `tools/xbox-control send 'tap Space' 'tap F' 'stick 0 1 1'`, `shot`, and
  `log` drive the running game, take its screen, and read its log.
- Apple TV (build, install, launch): `make tv`
  - Pair the Apple TV in Xcode's Device Hub first. Overrides:
    `MOPPE_TVOS_DEVICE`, `MOPPE_TVOS_TEAM`, `MOPPE_TVOS_CONFIGURATION`, and
    `MOPPE_TVOS_BUILD_DIR`.
- iPhone (build, install, launch): `make phone` (Release by default, on
  the first connected iPhone). Touch: left thumb walks/steers, dragging on
  the right looks around, the bottom-right button jumps or boosts.
  - The paired phone must be unlocked and reachable; its Personal Hotspot
    works when other Wi-Fi networks isolate clients.
  - Overrides: `MOPPE_IOS_DEVICE`, `MOPPE_IOS_TEAM`,
    `MOPPE_IOS_CONFIGURATION`, and `MOPPE_IOS_BUILD_DIR`.

## Visual Validation: Look for the GPU First
- Agents run either on the developer's machines (a Mac, or a Linux runner
  with a GPU) or in Amp orbs: Linux VMs with no GPU and no display, where
  Mesa's lavapipe (llvmpipe) draws every frame on the CPU (see the Linux
  entry above). Before choosing how to see a change, find out which:
  `uname`; on Linux, no `/dev/dri/renderD*` or a `VK_DRIVER_FILES` naming
  `lvp_icd` means the CPU, and the game's first line says for certain
  (`moppe: NHAL renderer on Vulkan (llvmpipe ...)`).
- On the CPU, frames are dear: a 640x360 still of the cached default world
  takes about half a minute, a 1280x720 one about a minute, and two
  seconds of 640x360 video nearly two minutes, so a scene's default
  `tools/record` would take hours. There, as a rule of thumb:
  - Settle what can be settled without pixels: unit tests,
    `world-fingerprint`, logs, and analysis of the fields and state.
  - Use a baked world (desktop default 4096 samples, 10 km): its land
    comes from `tools/bake-land` or a published `tools/fetch-land` asset and the finished world loads from the
    cache after the first launch. Generate another, or pass
    delete the cache, only when generation is
    what changed. Pin the scene (`MOPPE_OPENING=none`, `MOPPE_CLOCK`,
    a `MOPPE_WALK` or `MOPPE_DEMO` script) so a before
    and after differ only by the change.
  - Take a few targeted stills, small: `--screenshot` with `--window-size
    640x360` (into a directory that exists: a missing one is reported
    written but nothing is saved), `--water-screenshot`,
    `tools/capture-water`; for the renderer alone, `./build/nhal-demo
    --capture` takes seconds.
  - Render one thing at a time: lavapipe already fills every core, so
    parallel captures only slow each other. Run long ones in the
    background with a timeout.
  - Record video only when motion is the question, and then briefly:
    `tools/record OUT.mp4 SCENE --size 640x360 --seconds 2` (it skips
    gamescope without a GPU), with `--start` to reach the moment cheaply
    and `--sheet` for a contact sheet to read.
- With a GPU the workflows above run at their ordinary speeds: full-length
  recordings at 720p or 1080p and the trailer are reasonable ways to check work.
- These are defaults, not rules. When only a long or full-size capture can
  answer the question and there is no GPU, say what it would cost, or ask
  for it to be run on a machine with one.

## Research Library
- The hosted Sheaf research library is available through `tools/sheaf`; start
  with `tools/sheaf help`, then use `search`, `read`, and `note`.
- The helper defaults to `https://m.sheaf.less.rest` and obtains its token over
  `ssh igloo` when `SHEAF_TOKEN` is not already set.

## Architecture (see docs/renderer-design.md)
- `moppe/quantities.hh` is the registry of every quantity specification, in
  namespace sections. Declare new ones there with the `QUANTITY_SPEC` macro,
  never by hand-writing the struct: mp-units switches between a CRTP and a
  deducing-this formulation per toolchain and only the macro writes both. The
  concrete `quantity`/`quantity_point` aliases built on a spec stay with the
  code that owns the concept, since those need units and vectors the registry
  has no dependency on.
- Text and HUD vector shapes render with Slug, straight from quadratic
  outlines: `render/truetype.*` reads TrueType glyphs, `render/slug.*` builds
  the band/curve buffers (and a CPU coverage mirror for tests),
  `render/text.*` lays out a `Font` into a `TextList`, and the NHAL
  renderer's `slug-text` program draws it via `Renderer::draw_hud_text`. The
  bundled face is Geomini (`fonts/Geomini-Regular.ttf`, and SemiBold for
  the signs; SIL OFL, licence beside them), a geometric sans after
  Swedish road signs, kerned from its GPOS. Lettering in the world
  (the fingerposts) is the same `GlyphQuad`s placed in metres
  (`TextList::place`), uploaded once by `Renderer::create_world_text` and
  drawn by the `world-text` program into the scene, depth-tested, lit,
  and fogged (`Renderer::draw_world_text`).
- `moppe/render/` — the game-shaped renderer API (DrawList immediate mode,
  MeshBuilder-baked meshes, the Renderer interface); no GPU API types.
  `moppe/nhal/renderer/` implements it over NHAL (`moppe/nhal/`), the one
  renderer on every platform, with its shaders in Luv's Lisp
  (`moppe/nhal/renderer/shaders/world.lisp`) lowered by luv-shaderc to MSL,
  HLSL, SPIR-V, and WGSL.
- `moppe/platform/` — the Game interface, input, assets, speech. `sdl/` is
  the desktop host (macOS and Linux) and the browser's: an SDL3 window, its
  input, and the NHAL device (`device_metal.mm`, `device_vulkan.cc`,
  `device_webgpu.cc`). `ios/` and `tvos/` are UIKit hosts over NHAL's Metal
  device, `uwp/` the Xbox's, `apple/` services the Apple hosts share, and
  `web/` the browser's page.
- `moppe/game/` — the game systems, one file each (terrain, forest, water,
  dust, HUD, vehicle rendering; glue in game.cc and the game_*.cc files
  beside it, which split the application shell, `MoppeGame` in game.hh,
  by topic).
  Mutable replay state is gathered incrementally in `game/game_state.hh`; see
  `docs/game-state.md` for the checkpoint boundary and remaining systems.
- The on-foot body is `mov::Character` (`moppe/mov/character.*`): a
  kinematic capsule floating a step above the feet, moved with Box3D's mover
  planes against `TrunkField` obstacles and heightfield planes, with a ground
  probe for snapping and stepping, a walkable-slope limit, coyote time, and
  jump buffering. `game::Walker` turns controls into its intent and counts
  the stride phase; `game/avatar.*` solves the articulated figure's skeleton
  (two-bone leg IK, planted stance feet) and `walker_render.cc` draws it.
- `moppe/mov/` is simulation only; `moppe/map/` is terrain generation.
  The bike is a Box3D assemblage (`mov/rigid_bike.*`: chassis, two sphere
  wheels on wheel joints, a streamed height-field ground patch, trunk
  capsules, arcade assists), ridden through `mov::Vehicle` (controls, jets,
  landings). An unridden bike is parked (wheels locked, held still once
  settled). Every machine steps in substeps of at most 1/120 s
  (`mov::in_substeps`), so captures at 1/60 s run play's physics. Vendored Box3D carries one local fix
  (`third_party/box3d/VENDOR.md`).
  Both are GL-free and portable.
- `moppe/terrain/` owns finite terrain algorithms and typed analysis values;
  see `docs/terrain-expressions.md`.
- `moppe/spatial/` contains finite typed quantity bundles and generic local or
  interpolated sampling operations. `moppe/map/surface.*` is the whole of
  `moppe/map/`: the ground surface's typed quantities, the generation passes
  that fill them over one shared surface domain, the readings analysed back
  out, and the surface cache. `terrain::WaterSheets` carries a distinct water
  bundle in the same elevation frame. Game-side presentation bridges are the
  only place those quantities become renderer texture lanes.
- Terrain renders by vertex-pulling from an R32F height texture +
  RG16Snorm normals; physics keeps the authoritative CPU heightmap.
- A jittered, reversed-Z scene at a fraction of the drawable, a temporal
  resolve to the drawable, occlusion, shafts, bloom, and the tonemap, then
  the HUD in point coordinates (docs/nhal.md).
- World generation runs on a background thread behind a loading screen.

## Typed Quantities
- Physical and dimensionless values are typed with mp-units, never bare
  floats: lengths `meters_t`, places `position_t`, map points
  `plan_position_t`, elevations `terrain::SurfaceElevation`, ratios by
  kind (`proportion_t`, `grade_t`, `blend_weight_t`, ...). Raw floats stay
  only at storage boundaries (GPU layout, Box3D, noise internals).
  docs/typed-quantities.md has the conventions.
- `tools/units-report` prints the typed share per area (`--files AREA`
  for the worst files); CI's `--check` fails when an area's share drops.
  A change that raises one runs `tools/units-report --update` and commits
  `tools/units-baseline.json`.

## Code Style Guidelines
- **Namespaces**: Use nested namespaces (`moppe::render`, `moppe::game`)
- **Function names**: Use snake_case (`render_directly()`)
- **Member variables**: Prefix with `m_` (`m_width`)
- **Indentation**: 2 spaces
- **Braces**: Opening brace on same line for functions
- **Line Length**: Keep under 80 characters
- **Includes**: Group in order: 1) Project headers 2) STL 3) External libraries

## Error Handling
- Use exceptions for error conditions
- Catch in main function or event handlers
- Use `std::cerr` for error messages
- Graceful exit on errors with code -1

## Version Control
- After completing a task or request, generally commit and push proactively as
  a checkpoint unless the user asks not to. This is a single-developer repo;
  commits are cheap save points and do not need to represent a final design.
- When asked to "commit and push," commit all non-ignored changes in the
  worktree, including unrelated work, and push the current branch with plain
  `git push`.
- Keep generated files and build products out of commits; add appropriate
  ignore rules when necessary.
- Write clear commit messages. Split changes into multiple commits when there
  is a useful, natural separation and the changes are not entangled, but do
  not over-optimize for a pristine commit history.
- Finish with a clean worktree synchronized with its upstream branch.
- Do not create a pull request or use a publishing workflow unless explicitly
  requested.

## C++ Features
- C++23 standard; newer C++26 features may be enabled per target when the
  active Apple and CI toolchains support them without compatibility shims
- RAII for resource management
- Enable compiler warnings (-Wall)
