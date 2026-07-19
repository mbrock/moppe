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

## Backend scope

The WebGPU backend owns browser-native WGSL and resource lifetimes; it does not
translate Metal shaders. It currently favors a stable gameplay preset over
Metal visual parity: terrain materials and standing water are simplified, and
terrain shadows, particles, bloom, motion blur, and other post effects are
disabled by the default low profile. The simulation, world recipe, renderer
interface, game state, input semantics, and generated assets remain shared
with the native builds.
