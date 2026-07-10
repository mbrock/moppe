# Moppe Development Guidelines

## Build Commands
- Configure: `cmake -B build -G Ninja`
- Build everything: `cmake --build build`
- Run the game: `./build/moppe.app/Contents/MacOS/moppe` (or `open build/moppe.app`)
  - Modes: `--city`, `--pico`, `--fullscreen`
  - Dev env vars: `MOPPE_ASSETS=<repo>` (asset override), `MOPPE_DEMO=1`
    (autopilot for screenshots), `MOPPE_SUNHEIGHT=<0..1>`, `MOPPE_NOSHADOW=1`,
    `MOPPE_MAPCACHE=<file>` (load the heightfield from the file if present,
    else generate and save it -- skips the ~30 s erosion at boot; landscape
    mode only)
- Renderer smoke test: `./build/moppe-testbed`
- Heightmap tool: `./build/map-test [seed]` (writes test.tga)
- iOS (simulator): `cmake -B build-ios -G Xcode -DCMAKE_SYSTEM_NAME=iOS
  -DCMAKE_OSX_SYSROOT=iphonesimulator` then build the `moppe-ios` target
  with `CODE_SIGNING_ALLOWED=NO`

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

## C++ Features
- C++11 standard
- RAII for resource management
- Enable compiler warnings (-Wall)
