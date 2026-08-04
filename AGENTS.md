# Moppe Development Guidelines

## Build Commands
- Configure: `cmake -B build -G Ninja`
- Build everything: `cmake --build build`
- Unit tests: `ctest --test-dir build --output-on-failure`
- WebAssembly/WebGPU: `make web-serve`, then open
  `http://localhost:8080` (renderer testbed: `/moppe-web-testbed.html`)
- Run the game: `./build/moppe.app/Contents/MacOS/moppe`
  (or `open build/moppe.app`)
  - Game controller: left stick drives and steers; right trigger boosts; `A`
    deploys the glider or restarts; `B` mounts/dismounts; `X` cycles the camera;
    and `Y` boosts, flares, or skips the cinematic. The D-pad navigates Terrain
    Lab. Xbox, PlayStation, and compatible MFi controllers use this layout.
  - Hang glider: boost the bike into the air and press `E` once the deploy
    prompt appears. `A`/`D` bank, `W`/`S` select airspeed, and `Space` flares;
    the motocross stays tethered beneath the wing. Press `E` again to drop it
    and reduce wing loading; otherwise touching down folds the wing and
    continues on the bike. On iOS, the mount/dismount corner deploys the wing
    while airborne and drops the attached bike while gliding.
  - Modes: `--fullscreen`, `--windowed`,
    `--graphics-quality low|balanced|high`
    - Finished worlds automatically load from and save to a cache keyed by the
      executable build and complete world recipe. `--world-cache-key <name>`
      keeps a stable developer cache namespace across rebuilds;
      `--refresh-world-cache` replaces its selected entry, and
      `--no-world-cache` bypasses finished-world caching for one launch.
    - `--upscaling temporal|spatial|linear` requests MetalFX temporal or
      spatial reconstruction, or the exact linear fallback. Temporal is the
      default; startup prints the requested and backend-resolved mode.
      Temporal uses a single-sample jittered scene with persistent depth,
      motion vectors, exposure, and a reactive mask rather than scene MSAA.
    - `--frame-interpolation on|off` controls macOS MetalFX frame generation.
      It is opt-in on supported Metal 4 devices at 90 Hz or above: Moppe renders
      and simulates at half refresh, then alternates a generated midpoint and
      the retained real frame at the full display cadence. Support does not
      guarantee that a full-resolution interpolation pass fits the GPU budget.
    - On macOS, `--drawable-scale <0.25..1>` selects the final drawable as a
      fraction of display backing resolution. `--render-scale <0.25..1>`
      independently selects the 3D scene as a fraction of that drawable;
      `MOPPE_RENDERSCALE` remains its environment equivalent.
    - `--msaa 1|2|4` fixes the scene sample count before pipeline creation.
      `--scene-megapixels <0..64>` controls the desktop scene-area safety cap;
      zero disables it. Explicit flags override their legacy environment
      equivalents.
    - Override Boolean graphics features with comma-separated
      `--graphics-enable <names>` and `--graphics-disable <names>` lists.
    - `--window-size WIDTHxHEIGHT` picks the windowed size, and `--inactive`
      keeps a hand-started run behind the active app. Together they profile a
      large surface without taking over the display.
  - Deterministic opening-cinematic video:
    `tools/capture-cinematic /tmp/cinematic.mp4 12`. Set `MOPPE_SEED`,
    `MOPPE_TERRAIN_PROFILE`, or `MOPPE_CINEMATIC_CAPTURE_FPS` to override the
    defaults.
  - Representative still survey along the cinematic drone route:
    `tools/capture-terrain-survey /tmp/terrain-survey 12`. This writes the
    individual frames, a contact sheet, and the deterministic capture settings.
  - Frozen landscape gazetteer from gameplay, habitat, landform, freshwater,
    coast, and aerial viewpoints:
    `make gazetteer GAZETTEER_OUT=/tmp/moppe-gazetteer`. Unlike the route
    survey, this composes each camera directly over one finished world without
    advancing a demo. It writes named PNGs, dimensional `gazetteer.csv`, a
    contact sheet, and an HTML atlas. Overrides are `MOPPE_SEED`,
    `MOPPE_TERRAIN_PROFILE`, `MOPPE_GAZETTEER_GRAPHICS`,
    `MOPPE_GAZETTEER_WINDOW`, and `MOPPE_GAZETTEER_SETTLE`.
  - Feature-targeted water capture: `tools/capture-water /tmp/mouth.png mouth`.
    Feature names are `stream`, `river`, `confluence`, `mouth`, `waterfall`,
    and `lake`;
    set `MOPPE_SEED` and `MOPPE_TERRAIN_PROFILE` for reproducible comparisons.
  - Automated screenshots and graphics benchmarks keep their macOS windows
    inactive, so repeated captures do not steal focus from the current app.
  - Partitioned hot-feature GPU benchmark (64 configurations; prefix with
    `MOPPE_DEMO=1` so it measures a ride rather than the spawn point):
    `./build/moppe.app/Contents/MacOS/moppe --graphics-benchmark /tmp/gpu.csv
    --windowed --seed 123 --terrain-quality fast`. Development overrides are
    `--benchmark-prelude`, `--benchmark-settle`, `--benchmark-frames`, and
    `--benchmark-pass-timing` for precise Metal 4 pass columns (with profiling
    overhead).
    Analyze a completed CSV with
    `tools/graphics-benchmark-analyze INPUT.csv [OUTPUT_DIR]`.
  - Dev env vars: `MOPPE_ASSETS=<repo>` (asset override), `MOPPE_DEMO=1`
    (autopilot for screenshots), `MOPPE_SUNHEIGHT=<0..1>`, `MOPPE_NOSHADOW=1`,
    `MOPPE_RENDERSCALE=<0.25..1>`, `MOPPE_SCENEPIXELS=<megapixels>` (the
    scene-resolution budget; `0` restores the point-relative rule alone), and
    `MOPPE_MSAA=1|2|4` (sample count, fixed before the pipelines are built)
  - The desktop scene resolution is the smaller of the point-relative rule and
    `scene_megapixel_budget`, so a display attached at 1x — a 7680x2160 one
    asks for twice a 4K frame — costs resolution rather than frame rate.
- Renderer smoke test: `./build/moppe-testbed`
- iOS (simulator): `cmake -B build-ios -G Xcode -DCMAKE_SYSTEM_NAME=iOS
  -DCMAKE_OSX_SYSROOT=iphonesimulator` then build the `moppe-ios` target
  with `CODE_SIGNING_ALLOWED=NO`
- tvOS (simulator): `cmake -B build-tvos -G Xcode -DCMAKE_SYSTEM_NAME=tvOS
  -DCMAKE_OSX_SYSROOT=appletvsimulator` then build the `moppe-tvos` target
  with `CODE_SIGNING_ALLOWED=NO`
- Apple TV (build, install, launch): `make tv`
  - Pair the Apple TV in Xcode's Device Hub first. Overrides:
    `MOPPE_TVOS_DEVICE`, `MOPPE_TVOS_TEAM`, `MOPPE_TVOS_CONFIGURATION`, and
    `MOPPE_TVOS_BUILD_DIR`.
- iPhone (build, install, launch): `make phone`
  - The paired phone must be unlocked and reachable; its Personal Hotspot
    works when other Wi-Fi networks isolate clients.
  - Overrides: `MOPPE_IOS_DEVICE`, `MOPPE_IOS_TEAM`,
    `MOPPE_IOS_CONFIGURATION`, and `MOPPE_IOS_BUILD_DIR`.

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
- `moppe/render/` — portable renderer API (DrawList immediate mode,
  MeshBuilder-baked meshes, game-shaped Renderer interface); no GL/Metal
  types in headers. `moppe/render/metal/` and `moppe/render/webgpu/` own the
  native Metal and browser WGSL/WebGPU backends.
- `moppe/shaders/metal/` — MSL shaders, built into moppe.metallib per SDK.
- `moppe/platform/` — Game interface, input, assets, speech; `mac/`, `ios/`,
  `web/`, and shared `apple/` layers. The browser host uses Canvas2D glyph
  rasterization and a `requestAnimationFrame` loop.
- `moppe/game/` — the game systems, one file each (terrain, city, wildlife,
  dust, HUD, vehicle rendering; glue in game.cc).
  Mutable replay state is gathered incrementally in `game/game_state.hh`; see
  `docs/game-state.md` for the checkpoint boundary and remaining systems.
- `moppe/mov/` is simulation only; `moppe/map/` is terrain generation.
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
- Reversed-Z scene pass (MSAA→resolve), post chain (underwater grade,
  motion-blur feedback), then HUD in point coordinates.
- World generation runs on a background thread behind a loading screen.

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
