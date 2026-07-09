# Moppe Development Guidelines

## Build Commands
- Configure: `cmake -B build -G Ninja`
- Build everything: `cmake --build build`
- Run the game: `./build/moppe.app/Contents/MacOS/moppe` (or `open build/moppe.app`)
  - Modes: `--city`, `--pico`, `--fullscreen`
  - Dev env vars: `MOPPE_ASSETS=<repo>` (asset override), `MOPPE_DEMO=1`
    (autopilot for screenshots), `MOPPE_SUNHEIGHT=<0..1>`, `MOPPE_NOSHADOW=1`
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
- `moppe/game/` — the game systems, one file each (terrain, city,
  vegetation, wildlife, dust, HUD, vehicle rendering; glue in game.cc).
- `moppe/mov/` is simulation only; `moppe/map/` is terrain generation.
  Both are GL-free and portable.
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
- C++11 standard
- RAII for resource management
- Enable compiler warnings (-Wall)
