# WebAssembly and WebGPU

Moppe has a supported browser target built with Emscripten and Dawn WebGPU.
It runs the real game, including background world generation, terrain LODs,
retained and immediate geometry, water, the loading presentation, HUD, and
keyboard and pointer input.

## Build and run

Install Emscripten and Bun once on macOS:

```sh
brew install emscripten bun
```

Then build and serve from the repository root:

```sh
make web-serve
```

Open <http://localhost:8080>. The server sends the COOP and COEP headers that
shared-memory WebAssembly needs, so opening the generated HTML directly from
disk does not work. The standalone renderer testbed remains available at
<http://localhost:8080/moppe-web-testbed.html>.

`make web` only builds. It produces `moppe-web.html` and its `.wasm`, `.js`,
worker, asset, and data companions under `build-web/`.

## Browser launch profile

Cold browser loads deliberately default to the fast terrain profile, low
graphics, ocean enabled, and seed 123. This keeps world generation responsive
while exercising the complete game and a representative water surface. The
profile can be changed without rebuilding:

```text
http://localhost:8080/?terrain=play&graphics=balanced&seed=456
```

`terrain` accepts `fast`, `play`, or `research`; `graphics` accepts `low`,
`balanced`, or `high`; and `seed` is an integer. Higher profiles share the
native algorithms and can take substantially longer in WebAssembly.

The browser needs WebGPU and WebAssembly threads. Current Chrome and Safari on
macOS are the primary development browsers. Keyboard controls are the native
controls: `W`/`S` drive, `A`/`D` steer, `Space` boosts or skips the opening
cinematic, and `E` performs contextual actions. Clicking the canvas requests
pointer lock for pointer-driven modes.

## Public releases

The latest browser build is published at <https://moppe.less.rest>. To build
and deploy a new release to Igloo:

```sh
make web-deploy
```

The deployer uploads the game to a unique path under `/releases/` and then
atomically replaces the small homepage that points at it. The homepage is
served with `Cache-Control: no-store`; versioned HTML, JavaScript, WebAssembly,
and game data are served with a one-year immutable cache. Consequently a visit
to the stable URL always discovers the latest release while an already-loaded
release never mixes files from different builds.

The one-time Caddy site from `tools/moppe.Caddyfile` lives in
`/etc/caddy/Caddyfile` on Igloo. It serves `/home/mbrock/moppe-web`, enables
zstd and gzip, and supplies the COOP and COEP headers required by WebAssembly
threads. `MOPPE_WEB_HOST` and `MOPPE_WEB_ROOT` can override the deployer's
`igloo` and `/home/mbrock/moppe-web` defaults.

## Backend scope

The WebGPU backend owns browser-native WGSL and resource lifetimes; it does not
translate Metal shaders. Terrain uses the same five-level render lattice as
Metal, including the quarter-cell near field, parent-surface LOD morphing,
fragment-rate normals, and mipmapped grass, dirt, rock, and snow materials.
The procedural sky carries the same atmospheric gradient, cloud layers, sun,
stars, and fog-matched horizon as the Metal pass. Terrain also casts a filtered
heightfield shadow map using the shared sun transform; the browser shell enables
this and the ocean on top of its default low profile. Standing water remains
simplified, while hydrology-informed ground materials, particles, bloom, motion
blur, and other post effects remain absent. The simulation, world recipe,
renderer interface, game state, input semantics, and generated assets remain
shared with the native builds.
