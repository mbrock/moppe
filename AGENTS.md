# Moppe Development Guidelines

## Build Commands
- Configure: `cmake -B build -G Ninja -DCMAKE_BUILD_TYPE=RelWithDebInfo`
  (without a build type the project defaults to Debug, whose world
  generation is many times slower)
- Build everything: `cmake --build build`
- Unit tests: `cmake --build build --target moppe-tests && ctest --test-dir
  build --output-on-failure` (the test binary is excluded from the default
  build, so plain `ctest` otherwise runs a stale one)
- WebAssembly/WebGPU: `make web-serve`, then open
  `http://localhost:8080` (renderer testbed: `/moppe-web-testbed.html`)
- Run the game: `./build/moppe.app/Contents/MacOS/moppe`
  (or `open build/moppe.app`)
  - Game controller: left stick drives and steers; right trigger boosts; `A`
    deploys the glider or restarts; `B` mounts/dismounts; `X` cycles the camera;
    and `Y` boosts, flares, or skips the opening. The D-pad navigates Terrain
    Lab. Xbox, PlayStation, and compatible MFi controllers use this layout.
  - The game starts on foot beside the parked bike, in first person: the
    mouse looks around (captured; `M` frees it), `W`/`S` walk, `A`/`D`
    strafe, `Shift` runs, `Space` jumps, `Tab` cycles to the third-person
    views, and `F` near the bike mounts it (and steps off again).
    `MOPPE_DEMO`, benchmarks, and the gazetteer still start riding.
  - Scripted walks for captures: `MOPPE_WALK=walk|run|jump|tour` starts on
    foot (even with `MOPPE_DEMO=forest`) and drives a deterministic script;
    `MOPPE_WALK_CAMERA=chase|front|side` picks the view (`side` locks a
    camera beside the figure to judge the gait). Pair with
    `MOPPE_RIDE_CAPTURE_DIR` for consecutive frames.
  - The HUD is deliberately one quiet speedometer plus fading prompts. `H`
    toggles the diagnostic overlay (frame rate, stunt and score readings,
    trail map); `MOPPE_HUD=debug` starts with it on.
  - `P` captures the current frame to `screenshots/run-<timestamp>/shot-NNN.png`
    (gitignored, one directory per run; `MOPPE_SCREENSHOT_DIR` overrides the
    base). Start the game, walk around, press `P` at each view worth
    discussing, then point at the latest run directory. Each press also
    logs the camera as a pasteable `shot` line for the opening (`moppe:
    pose: shot shot-NNN eye X Y Z heading H pitch P fov F clock T sun S`),
    gathered in the run directory's `shots.txt`; on the Xbox,
    `tools/xbox-control send 'tap P'` then `log` reads it back.
  - The opening is authored: `data/opening.txt` lists a few held shots of the
    default world (seed 123, 2048 samples, play), each drifting gently
    (`push`/`truck`/`rise` metres, `pan`/`tilt`/`zoom` degrees, a breathing
    `sway`), joined by cuts or a dip through black (`fade`), with the title
    and a credit set in Slug fading over them (`caption`), ending with an
    `arrival` into the player's first-person view beside the parked bike.
    The grammar is in `moppe/game/opening.hh`. Author by framing a view in
    play or `tools/spectate` and pressing `P`; try a list without
    rebuilding with `MOPPE_OPENING=path/to/list.txt` (or
    `MOPPE_ASSETS=<repo>`). Another world plays one generated still of the
    trailhead (`MOPPE_OPENING=generated` forces it); `MOPPE_OPENING=flight`
    flies the old drone route through the planned landmarks. `Space` skips.
  - Hang glider: boost the bike into the air and press `E` once the deploy
    prompt appears. `A`/`D` bank, `W`/`S` select airspeed, and `Space` flares;
    the motocross stays tethered beneath the wing. Press `E` again to drop it
    and reduce wing loading; otherwise touching down folds the wing and
    continues on the bike. On iOS, the mount/dismount corner deploys the wing
    while airborne and drops the attached bike while gliding.
  - Modes: `--fullscreen`, `--windowed`,
    `--graphics-quality low|balanced|high`
    - Finished worlds automatically load from and save to a stable cache keyed
      by the complete world recipe. `--world-cache-key <name>` selects an
      additional developer cache namespace;
      `--refresh-world-cache` replaces its selected entry, and
      `--no-world-cache` bypasses finished-world caching for one launch.
    - `--upscaling temporal|spatial|linear` requests MetalFX temporal or
      spatial reconstruction, or the exact linear fallback. Temporal is the
      default; startup prints the requested and backend-resolved mode.
      Temporal uses a single-sample jittered scene with persistent depth,
      motion vectors, exposure, and a reactive mask rather than scene MSAA.
    - `--frame-interpolation on|off` controls macOS MetalFX frame generation.
      It defaults off, with ordinary play paced directly at 60 Hz. Explicit
      `on` requests the high-refresh display cadence and alternates a generated
      midpoint with the retained real frame when supported.
    - `--renderer nhal` draws the game with the next renderer
      (`moppe/nhal/renderer/`, shaders in `moppe/nhal/renderer/shaders/
      world.lisp`) instead of the Metal one: terrain, sky, meshes, and draw
      lists so far, temporally upscaled. See `docs/nhal.md`.
    - On macOS, `--drawable-scale <0.25..1>` selects the final drawable as a
      fraction of display backing resolution. `--render-scale <0.25..1>`
      independently selects the 3D scene as a fraction of that drawable;
      `MOPPE_RENDERSCALE` remains its environment equivalent. Ordinary play
      keeps the drawable native up to a 4.2 MP area cap, then scales it down;
      the 3D scene defaults to half that drawable. Explicit flags override
      either, and an explicit quality preset replaces the ordinary graphics
      baseline.
    - `--msaa 1|2|4` fixes the scene sample count before pipeline creation.
      `--scene-megapixels <0..64>` controls the desktop scene-area safety cap;
      zero disables it. Explicit flags override their legacy environment
      equivalents.
    - Override Boolean graphics features with comma-separated
      `--graphics-enable <names>` and `--graphics-disable <names>` lists.
    - `--window-size WIDTHxHEIGHT` picks the windowed size, and `--inactive`
      keeps a hand-started run behind the active app. Together they profile a
      large surface without taking over the display.
  - Deterministic opening video: `tools/capture-cinematic
    /tmp/opening.mp4` (it stops when the opening ends; a second argument
    caps the seconds). Set `MOPPE_SEED`, `MOPPE_TERRAIN_PROFILE`,
    `MOPPE_CINEMATIC_CAPTURE_FPS`, or `MOPPE_OPENING` to override the
    defaults, and add `--renderer nhal` after the arguments for NHAL.
  - Temporal-stability verification of the riding experience:
    `tools/ride-judge /tmp/ride-judge` captures a deterministic autopilot
    ride as consecutive frames (`MOPPE_RIDE_CAPTURE_DIR`, with `_START` and
    `_FRAMES` overrides), encodes `ride.mp4`, and -- when `GEMINI_API_KEY`
    is set -- asks a video-capable model to rate whether trees morph or
    restructure in motion. Still frames cannot verify this; only video (or
    a human) judges temporal behaviour.
  - Tree laboratory: `tools/tree-lab [--fullscreen]` rides the real game on a
    rolling plain with the forest replaced by a spruce, a birch, and a tall
    spruce ahead of the spawn (`MOPPE_TREE_LAB=1` with `--uplift-years 0`).
  - Spectator: `tools/spectate [--fullscreen]` (`MOPPE_SPECTATOR=1`) starts
    a free camera with no rider in the world's densest conifer stand: WASD
    moves (letters or QWERTY positions, so any layout works), Space/Tab
    rise and sink, and the captured mouse or the arrow keys look around; M
    frees the mouse (e.g. for a ⌘⇧5 window recording) and takes it back.
  - Temporal inspection cameras: `MOPPE_PAN=<seconds>` stands at the rider
    and sweeps the view left and right; `MOPPE_ORBIT=<radius>,<height>,
    <seconds>` circles the loneliest tall tree near the rider. Both, and
    `MOPPE_RIDE_CAPTURE_DIR` captures, advance exactly 1/60 s of world time
    per rendered frame so captured motion is even.
  - Representative still survey along the cinematic drone route:
    `tools/capture-terrain-survey /tmp/terrain-survey 12`. This writes the
    individual frames, a contact sheet, and the deterministic capture settings.
  - Frozen landscape gazetteer from gameplay, habitat, landform, freshwater,
    coast, and aerial viewpoints:
    `make gazetteer GAZETTEER_OUT=/tmp/moppe-gazetteer`. Unlike the route
    survey, this composes each camera directly over one finished world without
    advancing a demo. It writes named PNGs, dimensional `gazetteer.csv`, a
    contact sheet, and an HTML atlas. The four `grass-gradient-*` frames are
    deliberately uncomposed diagnostics: five-metre-high views across the
    world's most ordinary open turf, headed sunward, crosslit, antisun, and
    steeply down, so the whole grass representation gradient (blades,
    clumps, canopy material) is inspectable at known lighting angles.
  - Grass laboratory: `make grass-lab GRASS_LAB_OUT=/tmp/moppe-grass-lab`
    captures the same program on a rolling plain (`--uplift-years 0`) with
    grass cover saturated everywhere the medium can root and no trees
    (`MOPPE_GRASS_LAB=1`), so the grass-gradient frames show the pure LOD
    gradient of the grass system without habitat confounds. Overrides are `MOPPE_SEED`,
    `MOPPE_TERRAIN_PROFILE`, `MOPPE_GAZETTEER_GRAPHICS`,
    `MOPPE_GAZETTEER_WINDOW`, and `MOPPE_GAZETTEER_SETTLE`.
    `MOPPE_GAZETTEER_ENABLE` and `MOPPE_GAZETTEER_DISABLE` forward
    `--graphics-enable` and `--graphics-disable` lists.
  - The forest is the trunk forest (`moppe/shaders/metal/forest_trunks.metal`):
    tiered spruce and birch with leaf clumps on branches, at mature-stand
    heights with long clear trunks in closed stands. Autumn reaches the
    uplands first: birch there turns gold, drops a leaf carpet
    (`leaf_fall.metal` adds falling leaves), and the high heath turns.
    Trees are drawn by instanced vertex pulling: the CPU culls them by
    tile and sorts them by (species, detail tier), and each class shares
    one index buffer built from the topology in `shader_types.h`.
  - The on-foot hiker is a rigged model, `models/hiker.blend`: smooth
    subdivided parts, each a separate object with its modifiers and skin
    weights, exported to `moppe/game/figure_mesh.cc` (generated; do not
    edit) and skinned on the CPU from `pose_avatar`'s skeleton
    (`game/figure.*`). `make figure` re-exports it and renders a pose
    lineup to `FIGURE_PREVIEW` (default `/tmp/hiker.png`);
    `make figure-model` regenerates the .blend from
    `tools/figure/build.py`, discarding hand edits. Bone joints and lengths
    must match `avatar_size`.
  - The bike and glider are modelled the same way, `models/bike.blend` and
    `models/glider.blend` (scripts `tools/figure/bike.py`, `glider.py`), as
    rigid assemblies: empties marked `moppe_mesh` export their children in
    the empty's frame, and empties marked `moppe_point` export model-space
    points (axles, pivots, grips, pegs, saddle, harness) that the game
    places parts and the rider by. `tools/figure/export_model.py` writes
    the generated `moppe/game/{bike,glider}_model.{hh,cc}`; `make models`
    re-exports both (`MODELS_REBUILD=1` regenerates the .blend files).
    Ridden, the bike carries the hiker posed by `pose_holding` (hands on
    the grips, feet on the pegs), drawn 1.3x because the bike's physics is
    half again life size; the glider's pilot hangs prone at the basebar.
    `MOPPE_DEMO=glide` deploys the wing on the autopilot's first high leap,
    and `MOPPE_RIDE_CAMERA=side|front` locks a capture camera beside or
    ahead of the bike or glider.
  - Boulders (`moppe/game/boulders.cc`, `moppe/shaders/metal/boulders.metal`)
    are planned from the surface fields when a world activates -- talus,
    scree, stream cobbles, upland erratics -- drawn as faceted flat-shaded
    rocks, and the larger ones collide; `--graphics-disable boulders` hides
    them but keeps their colliders.
  - Feature-targeted water capture: `tools/capture-water /tmp/mouth.png mouth`.
    Feature names are `stream`, `river`, `confluence`, `mouth`, `waterfall`,
    and `lake`;
    set `MOPPE_SEED` and `MOPPE_TERRAIN_PROFILE` for reproducible comparisons.
  - Automated screenshots and graphics benchmarks keep their macOS windows
    inactive, so repeated captures do not steal focus from the current app.
  - Partitioned hot-feature GPU benchmark (32 configurations by default;
    prefix with `MOPPE_DEMO=1` so it measures a ride rather than the spawn
    point):
    `./build/moppe.app/Contents/MacOS/moppe --graphics-benchmark /tmp/gpu.csv
    --windowed --seed 123 --terrain-quality fast`. Development overrides are
    `--benchmark-prelude`, `--benchmark-settle`, `--benchmark-frames`,
    `--benchmark-partition detailed` for the 128-configuration refinement, and
    `--benchmark-pass-timing` for precise Metal 4 pass columns (with profiling
    overhead).
    Analyze a completed CSV with
    `tools/graphics-benchmark-analyze INPUT.csv [OUTPUT_DIR]`.
  - Weather is authored, not simulated: `MOPPE_WEATHER=clear|mist|drizzle`
    (`moppe/game/weather.hh`) sets the sky, fog, and light, and the NHAL
    renderer draws valley mist and a fine rain for it.
  - Dev env vars: `MOPPE_ASSETS=<repo>` (asset override), `MOPPE_DEMO=1`
    (autopilot for screenshots; use `MOPPE_DEMO=forest` to start the same
    rider at the world's selected forest-floor site),
    `MOPPE_SUNHEIGHT=<0..1>`, `MOPPE_NOSHADOW=1`,
    `MOPPE_RENDERSCALE=<0.25..1>`, `MOPPE_SCENEPIXELS=<megapixels>` (the
    scene-resolution budget; `0` restores the point-relative rule alone), and
    `MOPPE_MSAA=1|2|4` (sample count, fixed before the pipelines are built)
  - The desktop scene resolution is the smaller of the point-relative rule and
    `scene_megapixel_budget`, so a display attached at 1x — a 7680x2160 one
    asks for twice a 4K frame — costs resolution rather than frame rate.
- Renderer smoke test: `./build/moppe-testbed`
- NHAL, the next renderer's hardware layer (Metal 4 + Direct3D 12 for Xbox;
  `docs/nhal.md`): `cmake --build build --target nhal-demo` (configure with
  `-DMOPPE_BUILD_DEVELOPER_TOOLS=ON`), then `./build/nhal-demo`, or
  `./build/nhal-demo --capture /tmp/nhal.png --frames 30` to write one frame
  without taking focus. On Xbox: `nix build .#nhal-xbox` and
  `UWP_DEVICE_URL=https://xbox.whale-justice.ts.net nix run .#deploy-nhal-xbox`;
  the app writes `nhal.txt` (and one captured `nhal.tga`) to its LocalState,
  readable through Device Portal's file API.
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
  with the default 2048 world baked on the Mac (`tools/bake-world`, kept
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
  `render/text.*` lays out a `Font` into a `TextList`, and
  `shaders/metal/slug.metal` draws it via `Renderer::draw_hud_text`. The
  bundled face is `fonts/IosevkaAile-Regular.ttf` (SIL OFL, licence beside
  it). `GlyphQuad` carries 3D axes so world-space text can reuse it.
- `moppe/render/` — portable renderer API (DrawList immediate mode,
  MeshBuilder-baked meshes, game-shaped Renderer interface); no GL/Metal
  types in headers. `moppe/render/metal/` and `moppe/render/webgpu/` own the
  native Metal and browser WGSL/WebGPU backends.
- `moppe/shaders/metal/` — MSL shaders, built into moppe.metallib per SDK.
- `moppe/platform/` — Game interface, input, assets, speech; `mac/`, `ios/`,
  `web/`, and shared `apple/` layers. The browser host uses Canvas2D glyph
  rasterization and a `requestAnimationFrame` loop.
- `moppe/game/` — the game systems, one file each (terrain, forest, water,
  dust, HUD, vehicle rendering; glue in game.cc).
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
  The bike is a Box3D assemblage by default (`mov/rigid_bike.*`: chassis,
  two sphere wheels on wheel joints, a streamed height-field ground patch,
  trunk capsules, arcade assists); `--bike-physics classic` selects the old
  point mass in `mov/vehicle.cc`. An unridden bike is parked (wheels locked,
  held still once settled). Vendored Box3D carries one local fix
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
