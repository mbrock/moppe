# Moppe Development Guidelines

## Build Commands
- Configure: `cmake -B build -G Ninja`
- Build everything: `cmake --build build`
- Unit tests: `ctest --test-dir build --output-on-failure`
- Scalar-field visual check: `./build/terrain-field-demo /tmp/field.png 512`
- Terrain pipeline check: `./build/terrain-pipeline-demo /tmp/terrain.png
  257 123 combined world`
  (further stages: `orogeny=duration,dt,uplift,k,m,D[,sea,land_relief,
  coast,bathymetry]`, `analytical=age[,uplift,k,m,sea,iters,relax]`, and
  `trails[=min_m2,max_m2,width,shoulder,cut,fill,max_grade,sea,designed_grade,
  highland_preference,alpine_avoidance,crossfall]`)
- Run the game: `./build/moppe.app/Contents/MacOS/moppe`
  (or `open build/moppe.app`)
  - Hang glider: boost the bike into the air and press `E` once the deploy
    prompt appears. `A`/`D` bank, `W`/`S` select airspeed, and `Space` flares;
    touching down folds the wing and continues on foot. On iOS, the
    mount/dismount corner deploys the wing while airborne.
  - Modes: `--terrain-lab`, `--terrain-lab-preview`, `--fullscreen`, `--windowed`,
    `--graphics-quality low|high`
    - Override Boolean graphics features with comma-separated
      `--graphics-enable <names>` and `--graphics-disable <names>` lists.
  - Fast deterministic UI capture: `make terrain-lab-shot` (writes
    `terrain-lab.png`), or `tools/capture-terrain-lab /tmp/lab.png`.
  - Deterministic opening-cinematic video:
    `tools/capture-cinematic /tmp/cinematic.mp4 12`. Set `MOPPE_SEED`,
    `MOPPE_TERRAIN_PROFILE`, or `MOPPE_CINEMATIC_CAPTURE_FPS` to override the
    defaults.
  - Feature-targeted water capture: `tools/capture-water /tmp/mouth.png mouth`.
    Feature names are `river`, `confluence`, `mouth`, `waterfall`, and `lake`;
    set `MOPPE_SEED` and `MOPPE_TERRAIN_PROFILE` for reproducible comparisons.
  - Partitioned hot-feature GPU benchmark (32 configurations):
    `./build/moppe.app/Contents/MacOS/moppe --graphics-benchmark /tmp/gpu.csv
    --windowed --seed 123 --terrain-quality fast`. Development overrides are
    `--benchmark-prelude`, `--benchmark-settle`, and `--benchmark-frames`.
    Analyze a completed CSV with
    `tools/graphics-benchmark-analyze INPUT.csv [OUTPUT_DIR]`.
  - Dev env vars: `MOPPE_ASSETS=<repo>` (asset override), `MOPPE_DEMO=1`
    (autopilot for screenshots), `MOPPE_SUNHEIGHT=<0..1>`, `MOPPE_NOSHADOW=1`,
    `MOPPE_RENDERSCALE=<0.25..1>`
- Renderer smoke test: `./build/moppe-testbed`
- Heightmap tool: `./build/map-test [seed]` (writes test.tga)
- iOS (simulator): `cmake -B build-ios -G Xcode -DCMAKE_SYSTEM_NAME=iOS
  -DCMAKE_OSX_SYSROOT=iphonesimulator` then build the `moppe-ios` target
  with `CODE_SIGNING_ALLOWED=NO`
- iPhone (build, install, launch): `make phone`
  - The paired phone must be unlocked and reachable; its Personal Hotspot
    works when other Wi-Fi networks isolate clients.
  - Overrides: `MOPPE_IOS_DEVICE`, `MOPPE_IOS_TEAM`,
    `MOPPE_IOS_CONFIGURATION`, and `MOPPE_IOS_BUILD_DIR`.

## Architecture (see docs/renderer-design.md)
- `moppe/render/` — portable renderer API (DrawList immediate mode,
  MeshBuilder-baked meshes, game-shaped Renderer interface); no GL/Metal
  types in headers. `moppe/render/metal/` — the Metal backend (.mm).
- `moppe/shaders/metal/` — MSL shaders, built into moppe.metallib per SDK.
- `moppe/platform/` — Game interface, input, assets, speech; `mac/`,
  `ios/`, and shared `apple/` layers (MTKView; CoreText glyph rasterizer).
- `moppe/game/` — the game systems, one file each (terrain, city, wildlife,
  dust, HUD, vehicle rendering; glue in game.cc).
  Mutable replay state is gathered incrementally in `game/game_state.hh`; see
  `docs/game-state.md` for the checkpoint boundary and remaining systems.
- `moppe/mov/` is simulation only; `moppe/map/` is terrain generation.
  Both are GL-free and portable.
- `moppe/terrain/` is the portable runtime field-expression DAG, recipe and
  pipeline values, evaluator backends, and artifact writers; see
  `docs/terrain-expressions.md`.
- `moppe/spatial/` contains finite typed quantity bundles and generic local or
  interpolated sampling operations. `moppe/map/surface.*` materializes the
  heightmap's elevation and normal columns over one shared surface domain.
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
